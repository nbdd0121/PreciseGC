#ifndef NORLIT_GC_H
#define NORLIT_GC_H

#include <stddef.h>

/* Make it a trusy value to display debug messages */
#define DISPLAY_DEBUG_MSG 0
#define GC_MODE MARK_COMPACT_MODE

#define REF_TABLE_SIZE (1024/sizeof(void*))
#define MARK_SWEEP_MODE 0
#define MARK_COMPACT_MODE 1

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

#endif
