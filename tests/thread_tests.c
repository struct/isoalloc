/* iso_alloc thread_tests.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

uint32_t allocation_sizes[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128,
                               ZONE_256, ZONE_512, ZONE_1024,
                               ZONE_2048, ZONE_4096, ZONE_8192};

uint32_t array_sizes[] = {16, 32, 64, 128, 256, 512, 1024,
                          2048, 4096, 8192, 16384, 32768};

/* This test can be repurposed for benchmarking
 * against other allocators using LD_PRELOAD */
#if MALLOC_PERF_TEST
#define alloc_mem malloc
#define calloc_mem calloc
#define realloc_mem realloc
#define free_mem free
#else
#define alloc_mem iso_alloc
#define calloc_mem iso_calloc
#define realloc_mem iso_realloc
#define free_mem iso_free
#endif

const int32_t ALLOC = 0;
const int32_t REALLOC = 1;
const int32_t CALLOC = 2;

uint32_t times;

void *allocate(void *_type) {
    size_t array_size;
    size_t allocation_size;
    int32_t alloc_count = 0;
    int32_t type = *((int32_t *) _type);

    for(int o = 0; o < times; o++) {
        for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
            for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
                array_size = array_sizes[i];
                allocation_size = allocation_sizes[z];
                void *p[array_size];
                memset(p, 0x0, array_size);

                for(int y = 0; y < array_size; y++) {
                    if(allocation_size == 0) {
                        allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
                    }

                    void *d = NULL;

                    if(type == ALLOC) {
                        p[y] = alloc_mem(allocation_size);
                    } else if(type == REALLOC) {
                        d = (void *) alloc_mem(allocation_size / 2);
                        p[y] = realloc_mem(d, allocation_size);
                    } else if(type == CALLOC) {
                        p[y] = calloc_mem(1, allocation_size);
                    }

                    if(p[y] == NULL) {
                        LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations", allocation_size, alloc_count);
                    }

                    alloc_count++;
                    memset(p[y], 0x41, allocation_size);

                    /* Randomly free some allocations */
                    if((rand() % 5) > 1) {
                        free_mem(p[y]);
                        p[y] = NULL;
                    }
                }

                /* Free the remaining allocations */
                for(int r = 0; r < array_size; r++) {
                    if(p[r] != NULL) {
                        free_mem(p[r]);
                    }
                }
            }
        }
    }

    iso_flush_caches();

    return OK;
}

void run_test_threads() {
#if THREAD_SUPPORT
    pthread_t t;
    pthread_t tt;
    pthread_t ttt;
    pthread_create(&t, NULL, allocate, (void *) &ALLOC);
    pthread_create(&tt, NULL, allocate, (void *) &REALLOC);
    pthread_create(&ttt, NULL, allocate, (void *) &CALLOC);

    pthread_join(t, NULL);
    pthread_join(tt, NULL);
    pthread_join(ttt, NULL);
    pthread_exit(NULL);
#endif
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        times = 1;
    } else {
        times = atol(argv[1]);
    }

    run_test_threads();
    iso_alloc_detect_leaks();
    iso_verify_zones();
    return OK;
}
