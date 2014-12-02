#include <sys/mman.h>

#define DEFAULT_HEAP_PARITION_SIZE 0x100000

typedef struct block {
    size_t size;
    union {
        size_t gcFlag;
        /* We reuse the GC flag for compact pointer, since compactPtr cannot be zero */
        struct block *compactPtr;
    };
    norlit_finalizer_t *finalizer;
    char data[];
} block;

typedef struct heapParition {
    size_t size;
    size_t compactPtr;
    /* Pointer used for compact */
    size_t endPtr;
    struct heapParition *next;
    block blocks[0];
} heapParition;

static heapParition *oldest = NULL;
static heapParition *active = NULL;

static void addHeapParition(size_t heapSize);
static void ensureSize(size_t size);
static void ensureSizeC(heapParition **p, size_t size);
static void markRef(void **ref, int flags);
static void updateRef(void **ref, int flags);

static void *allocate(size_t heapSize) {
    return mmap(NULL, heapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static void freeMemory(void *ptr, size_t size) {
    munmap(ptr, size);
}

static void addHeapParition(size_t heapSize) {
    /* This could result in GC, and we will reuse these paritions */
    if (active && active->next) {
        active = active->next;
        return;
    }
    heapParition *new = allocate(heapSize);
    new->size = heapSize;
    new->compactPtr = sizeof(heapParition);
    new->endPtr = sizeof(heapParition);
    new->next = NULL;
    if (!active) {
        oldest = new;
        active = new;
    } else {
        active->next = new;
        active = new;
    }
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] A new heap parition at %p of size %d is initialized\n", new, heapSize);
}

static void ensureSize(size_t size) {
    /* Make sure there is enough size in the active parition, otherwise, allocate a new one.
     * Notice that this might cause inefficiency in memory, for example, a big enough object
     * may make the rest of the space in the previous active parition to be wasted.
     * However, this will not be a big problem since in most situations this will not be very
     * serious and can be solved after a GC
     */
    if (!active || size + active->endPtr + sizeof(block) > active->size) {
        if (size + sizeof(block) + sizeof(heapParition) > DEFAULT_HEAP_PARITION_SIZE) {
            assert(0);
        }
        addHeapParition(DEFAULT_HEAP_PARITION_SIZE);
    }
}

static void ensureSizeC(heapParition **p, size_t size) {
    /* Like ensureSize, but used when calculating the compacting address */
    if (size + (*p)->compactPtr + sizeof(block) > (*p)->size) {
        if (size + sizeof(block) + sizeof(heapParition) > DEFAULT_HEAP_PARITION_SIZE) {
            assert(0);
        }
        *p = (*p)->next;
    }
}

static void markRef(void **ref, int flags) {
    /* Very simple and standard mark procedure */
    if (!*ref)
        return;
    if (flags == REFERENCE_WEAK) {
        return;
    }
    block *b = *ref - sizeof(block);
    if (b->gcFlag) {
        return;
    } else {
        b->gcFlag = 1;
        if (b->finalizer && b->finalizer->mark) {
            if (DISPLAY_DEBUG_MSG)
                printf("[NorlitGC] [Debug] Block %p of size %d is marked with finalizer %p\n", b->data, b->size, b->finalizer);
            b->finalizer->mark(*ref, markRef);
        } else if (DISPLAY_DEBUG_MSG)
            printf("[NorlitGC] [Debug] Block %p of size %d is marked without finalizer\n", b->data, b->size);
    }
}

static void updateRef(void **ref, int flags) {
    /* Update reference, no difficulty */
    if (!*ref) {
        return;
    }
    block *b = *ref - sizeof(block);
    if (!b->gcFlag) {
        *ref = NULL;
        return;
    }
    void *newRef = b->compactPtr->data;
    if (DISPLAY_DEBUG_MSG) {
        if (newRef == *ref)
            printf("[NorlitGC] [Debug] No need to update reference to %p at %p\n", *ref, ref);
        else
            printf("[NorlitGC] [Debug] Update reference at %p from %p to %p\n", ref, *ref, b->compactPtr->data);
    }
    *ref = newRef;
}

void *norlit_gcAlloc(size_t size, norlit_finalizer_t *finalizer) {
    size = (size + sizeof(void *) - 1) / sizeof(void *)*sizeof(void *);
    ensureSize(size);
    /* First ensure there is enough size and then take a block, quiet easy */
    block *b = (block *)((char *)active + active->endPtr);
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] A block of size %d is allocated to %p\n", size, b->data);
    b->size = size;
    b->gcFlag = 0;
    b->finalizer = finalizer;
    active->endPtr += size + sizeof(block);
    memset(b->data, 0, size);
    return b->data;
}

void norlit_gc(void) {
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] GC starts\n");
    /* Initial marking: recursive marking from root, which is the reference table */
    for (int i = 0; i < REF_TABLE_SIZE; i++) {
        markRef(&refTable[i], REFERENCE_STRONG);
    }
    /* Calculate the target address of a block */
    heapParition *freeHeap = oldest;
    for (heapParition *p = oldest; p; p = p->next) {
        for (block *b = p->blocks; (size_t)b < (size_t)p + p->endPtr; b = (block *)((size_t)b + sizeof(block) + b->size)) {
            if (!b->gcFlag) {
                if (DISPLAY_DEBUG_MSG)
                    printf("[NorlitGC] [Debug] Find a unmarked block %p of size %d\n", b->data, b->size);
                continue;
            }
            ensureSizeC(&freeHeap, b->size);
            b->compactPtr = (block *)((char *)freeHeap + freeHeap->compactPtr);
            freeHeap->compactPtr += b->size + sizeof(block);
            if (DISPLAY_DEBUG_MSG) {
                if (b->compactPtr == b)
                    printf("[NorlitGC] [Debug] Find a marked block %p of size %d, no need to compact\n", b->data, b->size);
                else
                    printf("[NorlitGC] [Debug] Find a marked block %p of size %d, compact to %p\n", b->data, b->size, b->compactPtr->data);
            }
        }
    }
    /* Update pointers in reference table and in heap objects */
    for (int i = 0; i < REF_TABLE_SIZE; i++) {
        updateRef(&refTable[i], REFERENCE_STRONG);
    }
    for (heapParition *p = oldest; p; p = p->next) {
        for (block *b = p->blocks; (size_t)b < (size_t)p + p->endPtr; b = (block *)((size_t)b + sizeof(block) + b->size)) {
            if (!b->gcFlag) {
                if (b->finalizer && b->finalizer->finalize) {
                    b->finalizer->finalize(b->data);
                    if (DISPLAY_DEBUG_MSG)
                        printf("[NorlitGC] [Debug] Called on the finalizer %p of object %p\n", b->finalizer, b->data);
                } else if (DISPLAY_DEBUG_MSG)
                    printf("[NorlitGC] [Debug] Object %p has no finalizer\n", b->data);
                continue;
            }
            if (b->finalizer && b->finalizer->mark) {
                b->finalizer->mark(b->data, updateRef);
            }
        }
    }
    /* Move blocks around */
    for (heapParition *p = oldest; p; p = p->next) {
        for (block *b = p->blocks; (size_t)b < (size_t)p + p->endPtr; b = (block *)((size_t)b + sizeof(block) + b->size)) {
            if (!b->gcFlag)
                continue;
            block *target = b->compactPtr;
            if (target != b) {
                if (DISPLAY_DEBUG_MSG)
                    printf("[NorlitGC] [Debug] Move block %p to %p\n", b->data, target->data);
                memmove(target, b, b->size + sizeof(block));
            }
            target->gcFlag = 0;
        }
        p->endPtr = p->compactPtr;
        p->compactPtr = sizeof(heapParition);
    }
    /* We move back 'active' pointer to reuse these heap paritions */
    active = freeHeap ? freeHeap : oldest;
    for (heapParition *p = active->next; p;) {
        heapParition *prefetch = p->next;
        if (DISPLAY_DEBUG_MSG)
            printf("[NorlitGC] [Debug] Heap parition at %p is freed\n", p);
        freeMemory(p, p->size);
        p = prefetch;
    }
    active->next = NULL;
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] GC finished\n");
}
