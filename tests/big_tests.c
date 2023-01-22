/* iso_alloc big_tests.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {

    void *p = iso_alloc(SMALL_SIZE_MAX + 1);

    if(p == NULL) {
        LOG_AND_ABORT("Failed to allocate %d bytes", SMALL_SIZE_MAX + 1);
    }

    iso_free(p);

    p = iso_alloc(ZONE_USER_SIZE * 2);

    if(p == NULL) {
        LOG_AND_ABORT("Failed to allocate a big zone of %d bytes", ZONE_USER_SIZE * 2);
    }

    iso_free(p);

    void *q = iso_alloc(ZONE_USER_SIZE + (ZONE_USER_SIZE / 2));

    if(q == NULL) {
        LOG_AND_ABORT("Failed to allocate a big zone of %d bytes", ZONE_USER_SIZE + (ZONE_USER_SIZE / 2));
    }

    void *r = iso_alloc(ZONE_USER_SIZE + (ZONE_USER_SIZE / 4));

    if(r == NULL) {
        LOG_AND_ABORT("Failed to allocate a big zone of %d bytes", ZONE_USER_SIZE + (ZONE_USER_SIZE / 4));
    }

    iso_free_permanently(r);
    iso_free(q);

    void *ptrs[64];
    srand(time(NULL));

    iso_verify_zones();

    for(int32_t i = 0; i < 64; i++) {
        ptrs[i] = iso_alloc(ZONE_USER_SIZE + (rand() % ZONE_USER_SIZE));

        /* Randomly free some allocations */
        if((rand() % 5) > 1) {
            iso_free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }

    iso_verify_zones();

    LOG("[Big Zone Test] Megabytes used: %lu", iso_alloc_mem_usage());

    for(int32_t i = 0; i < 64; i++) {
        iso_free(ptrs[i]);
    }

    return 0;
}
