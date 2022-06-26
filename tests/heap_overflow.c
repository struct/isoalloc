/* iso_alloc heap_overflow.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    uint8_t *p = NULL;

    for(int32_t i = 0; i < 1024; i++) {
        p = (uint8_t *) iso_alloc(32);
        iso_free(p);
    }

    p = (uint8_t *) iso_alloc(32);

#if MEMCPY_SANITY
    const char *A = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    memcpy(p, A, strlen(A));
#else
    memset(p, 0x41, 65535);
#endif

    iso_free(p);
    iso_verify_zones();

    return OK;
}
