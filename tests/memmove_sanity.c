/* iso_alloc memmove_sanity.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMCPY_SANITY
#error "This test intended to be run with -DMEMCPY_SANITY=1"
#endif

int main(int argc, char *argv[]) {
    uint8_t *p = NULL;
    p = (uint8_t *) iso_alloc(SMALLEST_CHUNK_SZ);

    const char *A = "ABCABCABCABCABCABCABCABCABCABCABCABCABCAA"
                    "ABCABCABCABCABCABCABCABCABCABCABCABCABCAA"
                    "ABCABCABCABCABCABCABCABCABCABCABCABCABCAA";
    size_t len = strlen(A);
    memcpy(p, A, SMALLEST_CHUNK_SZ);

    memmove(&p[0], &p[64], len - 64);

    iso_free(p);
    iso_verify_zones();

    return OK;
}
