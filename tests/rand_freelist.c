/* iso_alloc rand_freelist.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

/* Number of allocation requests to test */
#define ALLOCATIONS 32

/* Size of each allocation */
#define CHUNK_SIZE 32

/* Failure threshold */
#define FAIL 4

/* This test requires that RANDOMIZE_FREELIST is
 * enabled or the test will return 0.
 * The test will look for returned chunks
 * that are adjacent and increment a counter when
 * they're found. This randomization is probabilistic
 * so this test may fail from time to time. */
int main(int argc, char *argv[]) {
    uint8_t *p;
    uint8_t *q;
    size_t adj_count = 0;

    for(int32_t i = 0; i < ALLOCATIONS; i++) {
        p = (uint8_t *) iso_alloc(CHUNK_SIZE);
        q = (uint8_t *) iso_alloc(CHUNK_SIZE);

        if(q > p && (q - p) == CHUNK_SIZE) {
            adj_count++;
        } else if(p - q == CHUNK_SIZE) {
            adj_count++;
        }

        if(iso_option_get(RANDOMIZE_FREELIST)) {
            if(adj_count > FAIL) {
                LOG_AND_ABORT("First allocation %p adjacent to second %p", p, q);
            }
        }
        iso_free(p);
        iso_free(q);
    }

    return 0;
}
