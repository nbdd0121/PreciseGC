#include "gc.h"

static void singleRefMark(void *data, reference_indicator_t i) {
    i(data);
}

int main(void) {
    norlit_finalizer_t singleRef = {
        .mark = singleRefMark
    };
    norlit_gcAlloc(800000, NULL);
    norlit_gcAlloc(800000, &singleRef);
    norlit_gcAlloc(80000, NULL);
    void *** ref = (void ** *)norlit_allocReference(norlit_gcAlloc(80000, &singleRef));
    norlit_gc();
    norlit_gcAlloc(80000, NULL);
    norlit_gcAlloc(80000, &singleRef);
    **ref = norlit_gcAlloc(80000, NULL);
    norlit_gc();
    norlit_freeReference((void **)ref);
    norlit_gc();
    return 0;
}
