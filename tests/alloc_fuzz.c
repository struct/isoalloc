/* iso_alloc alloc_fuzz.c
 * Copyright 2020 - chris.rohlf@gmail.com */

/* This test is not meant to be run as a part of the IsoAlloc
 * test suite. It should be run stand alone during development
 * work to catch any bugs you introduce */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"
#include <time.h>

uint32_t allocation_sizes[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128,
                               ZONE_256, ZONE_512, ZONE_1024,
                               ZONE_2048, ZONE_4096, ZONE_8192};

uint32_t array_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

uint32_t alloc_count;

/* Parameters for controlling probability of leaking a chunk.
 * This will add up very quickly with the speed of allocations.
 * This should exercise all code including new internally
 * managed zone allocation. Eventually we get OOM and SIGKILL */
#define LEAK_K 10
#define LEAK_V 8

iso_alloc_zone_handle *custom_zone;
#define NEW_ZONE_K 10
#define NEW_ZONE_V 1

#define DESTROY_ZONE_K 10
#define DESTROY_ZONE_V 8

int reallocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        void *d = iso_alloc(allocation_size / 2);
        p[i] = iso_realloc(d, allocation_size);

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations", allocation_size, alloc_count);
        }

        alloc_count++;

        /* Free every other allocation */
        if(i % 2) {
            iso_free(p[i]);
            p[i] = NULL;
            alloc_count--;
        }
    }

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL && ((rand() % LEAK_K) > LEAK_V)) {
            iso_free(p[i]);
            alloc_count--;
        }
    }

    return OK;
}

int callocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        p[i] = iso_calloc(1, allocation_size);

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations", allocation_size, alloc_count);
        }

        alloc_count++;

        /* Free every other allocation */
        if(i % 2) {
            iso_free(p[i]);
            p[i] = NULL;
            alloc_count--;
        }
    }

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL && ((rand() % LEAK_K) > LEAK_V)) {
            iso_free(p[i]);
            alloc_count--;
        }
    }

    return OK;
}

int allocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    if(rand() % 100 == 1) {
        if(custom_zone != NULL) {
            iso_alloc_destroy_zone(custom_zone);
        }

        custom_zone = iso_alloc_new_zone(allocation_size);
    }

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        if(rand() % 100 == 1 && custom_zone != NULL) {
            p[i] = iso_alloc_from_zone(custom_zone, allocation_size);
        } else {
            p[i] = iso_alloc(allocation_size);
        }

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations", allocation_size, alloc_count);
        }

        alloc_count++;

        /* Free every other allocation */
        if(i % 2) {
            iso_free(p[i]);
            p[i] = NULL;
            alloc_count--;
        }
    }

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL && ((rand() % LEAK_K) > LEAK_V)) {
            iso_free(p[i]);
            alloc_count--;
        }
    }

    if(rand() % 100 == 1) {
        iso_alloc_destroy_zone(custom_zone);
        custom_zone = NULL;
    }

    return OK;
}

int main(int argc, char *argv[]) {
    alloc_count = 0;
    custom_zone = NULL;

    while(1) {
        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
                allocate(array_sizes[i], allocation_sizes[z]);
            }
        }

        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            allocate(array_sizes[i], 0);
        }

        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
                callocate(array_sizes[i], allocation_sizes[z]);
            }
        }

        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            callocate(array_sizes[i], 0);
        }

        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
                reallocate(array_sizes[i], allocation_sizes[z]);
            }
        }

        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            reallocate(array_sizes[i], 0);
        }

        LOG("Total leaked allocations: %d", alloc_count);
    }

    return 0;
}
