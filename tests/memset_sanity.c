/* iso_alloc memset_sanity.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMSET_SANITY
#error "This test intended to be run with -DMEMSET_SANITY=1"
#endif

int main(int argc, char *argv[]) {
    uint8_t *p = NULL;

    for(int32_t i = 0; i < 1024; i++) {
        p = (uint8_t *) iso_alloc(32);
        iso_free(p);
    }

    p = (uint8_t *) iso_alloc(32);
    memset(p, 0x41, 65535);

    iso_free(p);
    iso_verify_zones();

    return OK;
}
