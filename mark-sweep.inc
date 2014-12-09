typedef struct block {
    size_t size;
    size_t gcFlag;
    norlit_finalizer_t *finalizer;
    struct block *next;
    char data[];
} block;

static block *head = NULL;

static void markRef(void **ref, int flags);
static void updateRef(void **ref, int flags);

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
    if (flags == REFERENCE_WEAK) {
        if (!*ref)
            return;
        block *b = *ref - sizeof(block);
        if (!b->gcFlag) {
            *ref = NULL;
        }
        return;
    }
}

void *norlit_gcAlloc(size_t size, norlit_finalizer_t *finalizer) {
    block *b = malloc(sizeof(block) + size);
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] A block of size %d is allocated to %p\n", size, b->data);
    b->size = size;
    b->gcFlag = 0;
    b->finalizer = finalizer;
    b->next = head;
    head = b;
    memset(b->data, 0, size);
    return b->data;
}

void *norlit_gcAllocFixed(size_t size, norlit_finalizer_t *finalizer) {
    block *b = malloc(sizeof(block) + size);
    b->size = size;
    b->gcFlag = 2;
    b->finalizer = finalizer;
    b->next = head;
    head = b;
    memset(b->data, 0, size);
    return b->data;
}

void norlit_gcFreeFixed(void *ptr) {
    block *b = ptr - sizeof(block);
    b->gcFlag = 0;
}

void norlit_gc(void) {
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] GC starts\n");
    /* Initial marking: recursive marking from root, which is the reference table */
    for (int i = 0; i < REF_TABLE_SIZE; i++) {
        if (refTable[i]) {
            if (DISPLAY_DEBUG_MSG)
                printf("[NorlitGC] [Debug] Reference to %p found", refTable[i]);
            markRef(&refTable[i], REFERENCE_STRONG);
        }
    }
    for (block *b = head; b; b = b->next) {
        if (b->gcFlag == 2) {
            b->gcFlag = 0;
            void *ptr = b->data;
            markRef(&ptr, REFERENCE_STRONG);
            b->gcFlag = 2;
        }
    }
    for (block *b = head; b; b = b->next) {
        if (b->gcFlag) {
            if (DISPLAY_DEBUG_MSG)
                printf("[NorlitGC] [Debug] Find a marked block %p of size %d\n", b->data, b->size);
            if (b->finalizer && b->finalizer->mark)
                b->finalizer->mark(b->data, updateRef);
        }
    }
    for (block *b = head, **prev = &head; b;) {
        if (!b->gcFlag) {
            if (DISPLAY_DEBUG_MSG)
                printf("[NorlitGC] [Debug] Find a unmarked block %p of size %d\n", b->data, b->size);
            if (b->finalizer && b->finalizer->finalize) {
                b->finalizer->finalize(b->data);
                if (DISPLAY_DEBUG_MSG)
                    printf("[NorlitGC] [Debug] Called on the finalizer %p of object %p\n", b->finalizer, b->data);
            } else if (DISPLAY_DEBUG_MSG)
                printf("[NorlitGC] [Debug] Object %p has no finalizer\n", b->data);
            *prev = b->next;
            free(b);
            b = *prev;
            continue;
        }
        if (DISPLAY_DEBUG_MSG)
            printf("[NorlitGC] [Debug] Find a marked block %p of size %d\n", b->data, b->size);
        if (b->gcFlag != 2)
            b->gcFlag = 0;
        prev = &b->next;
        b = b->next;
    }
    if (DISPLAY_DEBUG_MSG)
        printf("[NorlitGC] [Debug] GC finished\n");
}
