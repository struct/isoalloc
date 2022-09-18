/* iso_alloc chunk_entropy.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

/* Number of allocation requests to test */
#define ALLOCATIONS 32

/* Size of each allocation */
#define SIZE 32

/* Failure threshold */
#define FAIL 4

/* This test requires that SHUFFLE_FREE_BIT_SLOTS is
 * enabled or the test will return 0. This is off by
 * default. The test will look for returned chunks
 * that are adjacent and increment a counter when
 * they're found. This test is probabilistic and may
 * fail so it should not be enabled for CI */
int main(int argc, char *argv[]) {
#if SHUFFLE_FREE_BIT_SLOTS
    uint8_t *p;
    uint8_t *q;
    size_t adj_count = 0;

    for(int32_t i = 0; i < ALLOCATIONS; i++) {
        p = (uint8_t *) iso_alloc(SIZE);
        q = (uint8_t *) iso_alloc(SIZE);

        if(q > p && q - p <= SIZE) {
            adj_count++;
        } else if(p - q <= SIZE) {
            adj_count++;
        }

        iso_free(p);
        iso_free(q);
    }

    if(adj_count > FAIL) {
        LOG_AND_ABORT("First allocation %p adjacent to second %p", p, q);
    }
#endif
    return 0;
}
