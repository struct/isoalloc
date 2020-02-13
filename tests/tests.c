/* iso_alloc tests.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

uint32_t allocation_sizes[] = {ZONE_32, ZONE_64, ZONE_128,
                               ZONE_256, ZONE_512, ZONE_1024,
                               ZONE_2048, ZONE_4096, ZONE_8192};

uint32_t array_sizes[] = {32, 64, 128, 256, 512, 1024,
                          2048, 4096, 8192, 16384, 32768, 65536};

int allocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }
#if USE_MALLOC
        p[i] = malloc(allocation_size);
#else
        p[i] = iso_alloc(allocation_size);
#endif
        /* Randomly free some allocations */
        if((rand() % 5) > 1) {
#if USE_MALLOC
            free(p[i]);
#else
            iso_free(p[i]);
#endif
            p[i] = NULL;
        }
    }

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL) {
#if USE_MALLOC
            free(p[i]);
#else
            iso_free(p[i]);
#endif
        }
    }

    return OK;
}

int main(int argc, char *argv[]) {
    for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
        for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
            allocate(array_sizes[i], allocation_sizes[z]);
        }
    }

    for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
        allocate(array_sizes[i], 0);
    }

#ifndef USE_MALLOC
    void *p = iso_calloc(10, 2);

    if(p == NULL) {
        LOG_AND_ABORT("iso_calloc failed")
    }

    iso_free(p);

    p = iso_alloc(128);

    if(p == NULL) {
        LOG_AND_ABORT("iso_alloc failed")
    }

    p = iso_realloc(p, 1024);

    if(p == NULL) {
        LOG_AND_ABORT("iso_realloc failed")
    }

    iso_free(p);
#endif

    iso_verify_zones();

    return 0;
}
