/* iso_alloc init_destroy.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if AUTO_CTOR_DTOR
#error "This test should have AUTO_CTOR_DTOR disabled"
#endif

#if !ISO_DTOR_CLEANUP
#error "Enable ISO_DTOR_CLEANUP before running this test"
#endif

int main(int argc, char *argv[]) {
    /* Manually initialize IsoAlloc root */
    iso_alloc_initialize();

    void *p = iso_alloc(1024);

    if(p == NULL) {
        LOG_AND_ABORT("iso_alloc failed")
    }

    iso_free(p);

    /* Manually destroy IsoAlloc root */
    iso_alloc_destroy();
    return 0;
}
