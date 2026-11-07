/* iso_alloc profiler_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for the heap profiler. The traces API
 * previously copied a single entry regardless of how many
 * had been recorded, the per chunk size map was one entry
 * too small for SMALL_SIZE_MAX byte zones, and the free
 * profiler ran without the root lock. The profiler data
 * file written at exit is checked by run_profiler_tests.sh */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"
#include "iso_alloc_profiler.h"

#if !HEAP_PROFILER
#error "This test intended to be run with -DHEAP_PROFILER=1"
#endif

/* Enough allocations that at least one is sampled at
 * PROFILER_ODDS with overwhelming probability */
#define CYCLES (PROFILER_ODDS * 50)
#define THREADS 4
#define THREAD_CYCLES (PROFILER_ODDS * 10)
#define HELD 50
#define FILL 0xab

/* Backtraces are keyed on the call path so each of these
 * is a distinct allocation or free site. They must not be
 * inlined or they collapse into a single trace */
#define ALLOC_SITE(name)                                       \
    __attribute__((noinline)) static void *name(size_t size) { \
        void *p = iso_alloc(size);                             \
        if(p == NULL) {                                        \
            LOG_AND_ABORT("iso_alloc failed in " #name);       \
        }                                                      \
        return p;                                              \
    }

#define FREE_SITE(name)                                   \
    __attribute__((noinline)) static void name(void *p) { \
        iso_free(p);                                      \
    }

ALLOC_SITE(alloc_site_a)
ALLOC_SITE(alloc_site_b)
ALLOC_SITE(alloc_site_c)
ALLOC_SITE(alloc_site_d)
FREE_SITE(free_site_a)
FREE_SITE(free_site_b)
FREE_SITE(free_site_c)
FREE_SITE(free_site_d)

static void *churn(void *arg) {
    for(size_t i = 0; i < THREAD_CYCLES; i++) {
        void *p = iso_alloc((i % 4096) + 1);

        if(p == NULL) {
            LOG_AND_ABORT("iso_alloc failed in thread");
        }

        iso_free(p);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    /* Hold most of a SMALL_SIZE_MAX zone so the profiler
     * records usage for the largest possible chunk size.
     * That index lands one past the end of the old map */
    void *held[HELD];

    for(size_t i = 0; i < HELD; i++) {
        held[i] = iso_alloc(SMALL_SIZE_MAX);

        if(held[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %d byte chunk", SMALL_SIZE_MAX);
        }
    }

    for(size_t i = 0; i < CYCLES; i++) {
        const size_t size = (i % 2048) + 1;
        free_site_a(alloc_site_a(size));
        free_site_b(alloc_site_b(size));
        free_site_c(alloc_site_c(size));
        free_site_d(alloc_site_d(size));
    }

    /* The traces API must copy every recorded entry. Fill
     * the output with a pattern so untouched entries show */
    iso_alloc_traces_t at[BACKTRACE_DEPTH_SZ];
    memset(at, FILL, sizeof(at));
    size_t alloc_trace_count = iso_get_alloc_traces(at);

    /* Four sites each sampled many times over must yield
     * more than one distinct trace */
    if(alloc_trace_count < 2 || alloc_trace_count > BACKTRACE_DEPTH_SZ) {
        LOG_AND_ABORT("Bad alloc trace count %d", alloc_trace_count);
    }

    iso_alloc_traces_t pattern;
    memset(&pattern, FILL, sizeof(pattern));

    for(size_t i = 0; i < alloc_trace_count; i++) {
        if(memcmp(&at[i], &pattern, sizeof(pattern)) == 0) {
            LOG_AND_ABORT("Alloc trace %d of %d was not copied", i, alloc_trace_count);
        }

        if(at[i].call_count == 0) {
            LOG_AND_ABORT("Alloc trace %d has no calls", i);
        }
    }

    iso_free_traces_t ft[BACKTRACE_DEPTH_SZ];
    memset(ft, FILL, sizeof(ft));
    size_t free_trace_count = iso_get_free_traces(ft);

    if(free_trace_count < 2 || free_trace_count > BACKTRACE_DEPTH_SZ) {
        LOG_AND_ABORT("Bad free trace count %d", free_trace_count);
    }

    iso_free_traces_t fpattern;
    memset(&fpattern, FILL, sizeof(fpattern));

    for(size_t i = 0; i < free_trace_count; i++) {
        if(memcmp(&ft[i], &fpattern, sizeof(fpattern)) == 0) {
            LOG_AND_ABORT("Free trace %d of %d was not copied", i, free_trace_count);
        }

        if(ft[i].call_count == 0) {
            LOG_AND_ABORT("Free trace %d has no calls", i);
        }
    }

    iso_alloc_reset_traces();

    if(iso_get_alloc_traces(at) != 0 || iso_get_free_traces(ft) != 0) {
        LOG_AND_ABORT("Traces were not reset");
    }

#if THREAD_SUPPORT
    /* Concurrent frees exercise the lock around the free
     * profiler. A race here corrupts the backtrace arrays */
    pthread_t threads[THREADS];

    for(size_t i = 0; i < THREADS; i++) {
        if(pthread_create(&threads[i], NULL, churn, NULL) != 0) {
            LOG_AND_ABORT("pthread_create failed");
        }
    }

    for(size_t i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    free_trace_count = iso_get_free_traces(ft);

    if(free_trace_count == 0 || free_trace_count > BACKTRACE_DEPTH_SZ) {
        LOG_AND_ABORT("Bad free trace count %d after threads", free_trace_count);
    }
#endif

    for(size_t i = 0; i < HELD; i++) {
        iso_free(held[i]);
    }

    iso_flush_caches();
    iso_verify_zones();

    return 0;
}
