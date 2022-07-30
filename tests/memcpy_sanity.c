/* iso_alloc memcpy_sanity.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMCPY_SANITY
#error "This test intended to be run with -DMEMCPY_SANITY=1"
#endif

int main(int argc, char *argv[]) {
    uint8_t *p = NULL;

    for(int32_t i = 0; i < 1024; i++) {
        p = (uint8_t *) iso_alloc(8);
        iso_free(p);
    }

    p = (uint8_t *) iso_alloc(8);

    const char *A = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    memcpy(p, A, strlen(A));

    iso_free(p);
    iso_verify_zones();

    return OK;
}
