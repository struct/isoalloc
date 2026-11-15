/* iso_alloc thread_exit_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Every thread keeps its quarantine in a state block the
 * allocator maps for it and flushes when the thread exits.
 * This test creates many short lived threads that exit with
 * chunks still in quarantine and checks that nothing stays
 * allocated. It also frees from a pthread key destructor,
 * which may run before or after the allocators own, so the
 * state has to be rebuilt and flushed again. The quarantine
 * used to live in __thread storage which on macOS can be
 * released by dyld before the flush ran */
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#define THREADS 64
#define ROUNDS 4
#define HELD 24

static pthread_key_t late_key;

static void churn(size_t seed) {
    static const size_t sizes[] = {16, 32, 48, 64, 128, 256, 1000, 4096};
    void *held[HELD];

    for(size_t i = 0; i < HELD; i++) {
        held[i] = iso_alloc(sizes[(seed + i) % (sizeof(sizes) / sizeof(sizes[0]))]);

        if(held[i] == NULL) {
            LOG_AND_ABORT("Allocation failed in thread");
        }

        memset(held[i], 0x41, 16);
    }

    /* Fewer chunks than the quarantine holds so the thread
     * exits with all of them still unflushed */
    for(size_t i = 0; i < HELD; i++) {
        iso_free(held[i]);
    }
}

/* Runs from a key destructor after the thread is already
 * exiting. The allocators own destructor may have run before
 * this and released the thread state */
static void late_free(void *arg) {
    churn((size_t) (uintptr_t) arg);
}

static void *worker(void *arg) {
    churn((size_t) (uintptr_t) arg);

    if(pthread_setspecific(late_key, arg) != 0) {
        LOG_AND_ABORT("pthread_setspecific failed");
    }

    return NULL;
}

int main(int argc, char *argv[]) {
#if !THREAD_SUPPORT
    /* Without THREAD_SUPPORT there is one unlocked quarantine
     * for the process and threads are not supported at all */
    return 0;
#endif

    if(pthread_key_create(&late_key, late_free) != 0) {
        LOG_AND_ABORT("pthread_key_create failed");
    }

    /* With the malloc hooks in place libc itself allocates
     * from this allocator, and glibc keeps thread stacks and
     * their TLS in a cache after a thread exits. Those chunks
     * are live, not leaked, so the check is that the count of
     * live chunks does not grow from one round to the next */
    uint64_t leaks_after_first = 0;

    for(size_t r = 0; r < ROUNDS; r++) {
        pthread_t threads[THREADS];

        for(size_t i = 0; i < THREADS; i++) {
            if(pthread_create(&threads[i], NULL, worker, (void *) (uintptr_t) (i + 1)) != 0) {
                LOG_AND_ABORT("pthread_create failed");
            }
        }

        for(size_t i = 0; i < THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        /* The main threads own quarantine is not empty, so flush
         * it. Every exited threads quarantine must have been
         * flushed by its destructor */
        iso_flush_caches();

        if(r == 0) {
            leaks_after_first = iso_alloc_detect_leaks();
        }
    }

    const uint64_t leaks = iso_alloc_detect_leaks();

    if(leaks > leaks_after_first) {
        LOG_AND_ABORT("%lu chunks are still allocated after every thread exited, %lu after the first round", leaks, leaks_after_first);
    }

    iso_verify_zones();
    return 0;
}
