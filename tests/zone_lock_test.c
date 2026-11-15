/* iso_alloc zone_lock_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Allocation and free take a per zone lock, the root lock is
 * only taken to find, create, retire or destroy zones. This
 * test drives the paths that interact across threads: chunks
 * freed by a thread other than the one that allocated them,
 * zones retiring and being replaced in place while other
 * threads allocate from them, and many threads contending for
 * one size class. Afterwards nothing may be leaked and every
 * zone must verify */
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !UNIT_TESTING
#error "This test needs the library built with -DUNIT_TESTING=1"
#endif

#define THREADS 8
#define ROUNDS 20000
#define RING 1024

/* A ring of chunks shared by every thread. Each thread puts
 * the chunk it allocated in and frees whatever was there, so
 * most frees are of chunks allocated by another thread */
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
static void *ring[RING];

static void *cross_thread(void *arg) {
    const size_t id = (size_t) (uintptr_t) arg;
    static const size_t sizes[] = {16, 32, 48, 64, 128, 256, 1000, 4096};

    for(size_t i = 0; i < ROUNDS; i++) {
        const size_t size = sizes[(i + id) % (sizeof(sizes) / sizeof(sizes[0]))];
        uint8_t *p = iso_alloc(size);

        if(p == NULL) {
            LOG_AND_ABORT("Allocation of %d bytes failed", size);
        }

        p[0] = (uint8_t) id;
        p[size - 1] = (uint8_t) id;

        pthread_mutex_lock(&ring_lock);
        void *old = ring[(i * THREADS + id) % RING];
        ring[(i * THREADS + id) % RING] = p;
        pthread_mutex_unlock(&ring_lock);

        if(old != NULL) {
            iso_free(old);
        }
    }

    return NULL;
}

/* Retire zones under contention. 8192 byte chunks fill a zone
 * of 512 chunks and iso_free_size releases them at once, so the
 * zone retires every (512 * ZONE_ALLOC_RETIRE) cycles while the
 * other threads keep allocating from it */
static void *retire(void *arg) {
    const size_t cycles = (512 * ZONE_ALLOC_RETIRE) + 512;

    for(size_t i = 0; i < cycles; i++) {
        void *p = iso_alloc(8192);

        if(p == NULL) {
            LOG_AND_ABORT("Allocation of 8192 bytes failed");
        }

        memset(p, 0x41, 64);
        iso_free_size(p, 8192);
    }

    return NULL;
}

/* Every thread hammers one class */
static void *contend(void *arg) {
    void *held[64];

    for(size_t i = 0; i < ROUNDS; i++) {
        for(size_t j = 0; j < 64; j++) {
            held[j] = iso_alloc(32);

            if(held[j] == NULL) {
                LOG_AND_ABORT("Allocation of 32 bytes failed");
            }
        }

        for(size_t j = 0; j < 64; j++) {
            iso_free(held[j]);
        }
    }

    return NULL;
}

static void run(void *(*fn)(void *), const char *name) {
    pthread_t threads[THREADS];

    for(size_t i = 0; i < THREADS; i++) {
        if(pthread_create(&threads[i], NULL, fn, (void *) (uintptr_t) i) != 0) {
            LOG_AND_ABORT("pthread_create failed for %s", name);
        }
    }

    for(size_t i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
}

int main(int argc, char *argv[]) {
#if !THREAD_SUPPORT
    return 0;
#endif

    iso_alloc_root *root = _get_root();

    if(root == NULL) {
        void *p = iso_alloc(16);
        iso_free(p);
        root = _get_root();
    }

    /* Everything runs twice. With the malloc hooks in place libc
     * allocates from this allocator too, and glibc caches thread
     * stacks and their TLS after a thread exits, so the test is
     * that the second round leaves no more live chunks than the
     * first rather than none at all */
    uint64_t leaks_after_first = 0;

    for(int round = 0; round < 2; round++) {
        run(cross_thread, "cross_thread");

        for(size_t i = 0; i < RING; i++) {
            if(ring[i] != NULL) {
                iso_free(ring[i]);
                ring[i] = NULL;
            }
        }

        run(retire, "retire");
        run(contend, "contend");
        iso_flush_caches();

        if(round == 0) {
            leaks_after_first = iso_alloc_detect_leaks();
        }
    }

    /* Contending threads should have spread over more than one
     * zone of the class, up to the cap, so the class table keeps
     * them apart afterwards */
    size_t zones_32 = 0;

    for(size_t i = 0; i < root->zones_used; i++) {
        if(root->zones[i].chunk_size == 32 && root->zones[i].internal == true) {
            zones_32++;
        }
    }

    if(zones_32 == 0 || zones_32 > root->zones_per_class_cap) {
        LOG_AND_ABORT("%d zones hold 32 byte chunks, the cap is %d", zones_32, root->zones_per_class_cap);
    }

    /* The worker threads exited and flushed their quarantines
     * and this thread flushed its own */
    const uint64_t leaks = iso_alloc_detect_leaks();

    if(leaks > leaks_after_first) {
        LOG_AND_ABORT("%lu chunks leaked, %lu live after the first round", leaks, leaks_after_first);
    }

    iso_verify_zones();
    return 0;
}
