#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

#include "gc.h"

static void *refTable[REF_TABLE_SIZE] = {};
static size_t refTablePtr = 0;

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

#if GC_MODE==MARK_SWEEP_MODE
#include "mark-sweep.inc"
#elif GC_MODE==MARK_COMPACT_MODE
#include "mark-compact.inc"
#else
#error No Such GC Mode
#endif
