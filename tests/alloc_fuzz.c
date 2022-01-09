/* iso_alloc alloc_fuzz.c
 * Copyright 2021 - chris.rohlf@gmail.com */

/* This test is not meant to be run as a part of the IsoAlloc
 * test suite. It should be run stand alone during development
 * work to catch any bugs you introduce */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"
#include <time.h>

uint32_t allocation_sizes[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128,
                               ZONE_256, ZONE_512, ZONE_1024,
                               ZONE_2048, ZONE_4096, ZONE_8192,
                               SMALL_SZ_MAX / 4, SMALL_SZ_MAX / 2, SMALL_SZ_MAX};

uint32_t array_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

/* Parameters for controlling probability of leaking a chunk.
 * This will add up very quickly with the speed of allocations.
 * This should exercise all code including new internally
 * managed zone allocation. Eventually we get OOM and SIGKILL */
#define LEAK_K 1000
#define LEAK_V 8

static __thread iso_alloc_zone_handle *private_zone;
#define NEW_ZONE_K 1000
#define NEW_ZONE_V 1

#define DESTROY_ZONE_K 1000
#define DESTROY_ZONE_V 8

#define MAYBE_VALIDATE_ZONES() \
    if((rand() % 10) == 1) {   \
        iso_verify_zones();    \
    }

int reallocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        void *d = iso_alloc(allocation_size / 2);
        memset(d, 0x0, allocation_size / 2);
        p[i] = iso_realloc(d, allocation_size);

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes", allocation_size);
        }

        /* Free every other allocation */
        if(i % 2) {
            iso_free(p[i]);
            p[i] = NULL;
        }
    }

    MAYBE_VALIDATE_ZONES();

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL && ((rand() % LEAK_K) > LEAK_V)) {
            iso_free(p[i]);
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
            LOG_AND_ABORT("Failed to allocate %ld bytes", allocation_size);
        }

        /* Free every other allocation */
        if(i % 2) {
            iso_free(p[i]);
            p[i] = NULL;
        }
    }

    MAYBE_VALIDATE_ZONES();

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL && ((rand() % LEAK_K) > LEAK_V)) {
            iso_free(p[i]);
        }
    }

    return OK;
}

int allocate(size_t array_size, size_t allocation_size) {
    void *p[array_size];
    memset(p, 0x0, array_size);

    if(rand() % 100 == 1) {
        if(private_zone != NULL) {
            iso_alloc_destroy_zone(private_zone);
        }

        private_zone = iso_alloc_new_zone(allocation_size);
    }

    for(int i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        if(rand() % 100 == 1 && private_zone != NULL && allocation_size < SMALL_SZ_MAX) {
            p[i] = iso_alloc_from_zone(private_zone, allocation_size);
        } else {
            p[i] = iso_alloc(allocation_size);
        }

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes", allocation_size);
        }

        /* Free every other allocation */
        if(i % 2) {
            iso_free(p[i]);
            p[i] = NULL;
        }
    }

    MAYBE_VALIDATE_ZONES();

    /* Free the remaining allocations */
    for(int i = 0; i < array_size; i++) {
        if(p[i] != NULL && ((rand() % LEAK_K) > LEAK_V)) {
            iso_free(p[i]);
        }
    }

    if(rand() % 10 == 1) {
        iso_alloc_destroy_zone(private_zone);
        private_zone = NULL;
    }

    return OK;
}

void *start() {
    uint64_t loop = 1;

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

        LOG("Thread ID (%d) looped %d times", pthread_self(), loop++);
    }
}

int main(int argc, char *argv[]) {
    private_zone = NULL;

    pthread_t t;
    pthread_t tt;
    pthread_t ttt;
    pthread_t tttt;
    pthread_create(&t, NULL, start, NULL);
    pthread_create(&tt, NULL, start, NULL);
    pthread_create(&ttt, NULL, start, NULL);
    pthread_create(&tttt, NULL, start, NULL);

    pthread_join(t, NULL);
    pthread_join(tt, NULL);
    pthread_join(ttt, NULL);
    pthread_join(tttt, NULL);
    pthread_exit(NULL);

    return 0;
}
