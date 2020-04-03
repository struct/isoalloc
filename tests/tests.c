/* iso_alloc tests.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"
#include <time.h>

uint32_t allocation_sizes[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128,
                               ZONE_256, ZONE_512, ZONE_1024,
                               ZONE_2048, ZONE_4096, ZONE_8192};

uint32_t array_sizes[] = {16, 32, 64, 128, 256, 512, 1024,
                          2048, 4096, 8192, 16384, 32768, 65536};

int32_t alloc_count;

#if MALLOC_PERF_TEST
#define alloc_mem malloc
#define free_mem free
#else
#define alloc_mem iso_alloc
#define free_mem iso_free
#endif

int allocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        p[i] = alloc_mem(allocation_size);

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations", allocation_size, alloc_count);
        }

        alloc_count++;

        /* Randomly free some allocations */
        if((rand() % 5) > 1) {
            free_mem(p[i]);
            p[i] = NULL;
        }
    }

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL) {
            free_mem(p[i]);
        }
    }

    return OK;
}

int main(int argc, char *argv[]) {
    clock_t start, end;

    start = clock();

    for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
        for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
            allocate(array_sizes[i], allocation_sizes[z]);
        }
    }

    for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
        allocate(array_sizes[i], 0);
    }

    end = clock();

#if DEBUG
    double total = ((double) (end - start)) / CLOCKS_PER_SEC;

#if MALLOC_PERF_TEST
    LOG("malloc/free tests completed in %f seconds", total);
#else
    LOG("iso_alloc/iso_free tests completed in %f seconds", total);
#endif
#endif

    return 0;
}
