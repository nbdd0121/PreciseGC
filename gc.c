#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/************** Start of public API **************/

/* I cannot think of a proper name for this
 * In mark-compact GC, this could either be a function to mark object or update reference.
 */
typedef void (*reference_indicator_t)(void **);

typedef struct {
    /* The mark functions will mark all references (pointers to hosted objects)
     * It will be used to do two things in mark-compact GC
     * 1. Standard Marking Procedure: Find reachable objects
     * 2. Update Reference
     * This mark function should pass the POINTER TO THE REFERENCE (which means you probably need a addressof operator)
     * Only pointers to the FIRST BYTE of hosted object or null pointers are accepted
     * This field can be null
     */
    void (*mark)(void *, reference_indicator_t);
    /* Finalize a object. There is no chance to revitalize the object as can a finalizer do in Java
     * This field can be null
     */
    void (*finalize)(void *);
} norlit_finalizer_t;

/**
 * Allocate a chunk of memory with given size and finalizer.
 * Notice: In this version, GC will not be triggered through norlit_gcAlloc,
 *         but it is better to use directly with norlit_alloReference.
 *
 * arg0: size_t size of the memory. If size is greater than the limit, allocator will abort.
 * arg1: norlit_finalizer_t* finalizer, can be NULL.
 * return: pointer to the allocated memory. Guaranteed to be non-NULL.
 */
void *norlit_gcAlloc(size_t, norlit_finalizer_t *);

/**
 * Allocate a reference. The reference can be updated with the compact of GC.
 * Use this is highly recommanded since it is dangerous to use direct pointer to
 * the hosted memory, because the memory might be dangling if GC occured. It is
 * the caller's responsiblity to free the reference otherwise memory leak might occur
 *
 * arg0: void* pointer to the memory allocated via norlit_gcAlloc
 * return: pointer to the reference
 */
void **norlit_allocReference(void *);

/**
 * Free a reference.
 *
 * arg0: pointer to the reference
 */
void norlit_freeReference(void **);

/**
 * Trigger GC maually. In this version, GC could be triggered maually, and it is
 * recommanded to use heuristic algorithms to trigger GC automatically when exceed
 * a limit or so
 */
void norlit_gc(void);

/*************** End of public API ***************/


#define DEFAULT_HEAP_PARITION_SIZE 0x100000
#define REF_TABLE_SIZE (1024/sizeof(void*))

/* Make it a trusy value to display debug messages */
#define DISPLAY_DEBUG_MSG 1

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
static void *refTable[REF_TABLE_SIZE] = {};
static size_t refTablePtr = 0;

static void addHeapParition(size_t heapSize);
static void ensureSize(size_t size);
static void ensureSizeC(heapParition **p, size_t size);
static void markRef(void **ref);
static void updateRef(void **ref);

static void addHeapParition(size_t heapSize) {
    /* This could result in GC, and we will reuse these paritions */
    if (active && active->next) {
        active = active->next;
        return;
    }
    heapParition *new = malloc(heapSize);
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

static void markRef(void **ref) {
    /* Very simple and standard mark procedure */
    if (!*ref)
        return;
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

static void updateRef(void **ref) {
    /* Update reference, no difficulty */
    if (!*ref) {
        return;
    }
    block *b = *ref - sizeof(block);
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

void **norlit_allocReference(void *ptr) {
    /* Just used a sightly optimized algorithm to find free slots in reference table */
    for (; refTablePtr < REF_TABLE_SIZE; refTablePtr++) {
        if (!refTable[refTablePtr]) {
            refTable[refTablePtr] = ptr;
            return &refTable[refTablePtr++];
        }
    }
    for (refTablePtr = 0; refTablePtr < REF_TABLE_SIZE; refTablePtr++) {
        if (!refTable[refTablePtr]) {
            refTable[refTablePtr] = ptr;
            return &refTable[refTablePtr++];
        }
    }
    assert(!"No enought reference spaces");
}

void norlit_freeReference(void **ptr) {
    /* In this implementation, we just look for empty slots in reference table, so just set to zero */
    *ptr = NULL;
}

void norlit_gc(void) {
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] GC starts\n");
    /* Initial marking: recursive marking from root, which is the reference table */
    for (int i = 0; i < REF_TABLE_SIZE; i++) {
        markRef(&refTable[i]);
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
        updateRef(&refTable[i]);
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
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] GC finished\n");
}

/***************** A simple example */

static void singleRefMark(void *data, reference_indicator_t i) {
    i(data);
}

int main(void) {
    norlit_finalizer_t singleRef = {
        .mark = singleRefMark
    };
    norlit_gcAlloc(8, NULL);
    norlit_gcAlloc(8, &singleRef);
    norlit_gcAlloc(8, NULL);
    void *** ref = (void ** *)norlit_allocReference(norlit_gcAlloc(8, &singleRef));
    norlit_gc();
    norlit_gcAlloc(8, NULL);
    norlit_gcAlloc(8, &singleRef);
    **ref = norlit_gcAlloc(8, NULL);
    norlit_gc();
    norlit_freeReference((void **)ref);
    norlit_gc();
}
