/* iso_alloc interfaces_test.c
 * Copyright 2023 - chris.rohlf@gmail.com */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <assert.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if HEAP_PROFILER
#include "iso_alloc_profiler.h"
#endif

int main(int argc, char *argv[]) {
    /* Test iso_calloc() */
    void *p = iso_calloc(10, 2);

    if(p == NULL) {
        LOG_AND_ABORT("iso_calloc failed")
    }

    p = iso_calloc(INT_MAX - 1, 4);

    if(p != NULL) {
        LOG_AND_ABORT("iso_calloc over check failed")
    }

    iso_free(p);

    /* Test iso_alloc() */
    p = iso_alloc(128);

    if(p == NULL) {
        LOG_AND_ABORT("iso_alloc failed")
    }

    memset(p, 0x41, 128);

    uint8_t *pv = p;

    if(pv[10] != 0x41 || pv[100] != 0x41) {
        LOG_AND_ABORT("Chunk allocated at %p does not contain expected data! %x %x", p, pv[10], pv[100]);
    }

    /* Test iso_realloc */
    p = iso_realloc(p, 1024);

    if(p == NULL) {
        LOG_AND_ABORT("iso_realloc failed")
    }

    /* Test iso_reallocarray */
    if(iso_reallocarray(NULL, SIZE_MAX, SIZE_MAX) != NULL) {
        LOG_AND_ABORT("iso_reallocarray should have overflown");
    }

    p = iso_reallocarray(p, 16, 16);

    if(p == NULL) {
        LOG_AND_ABORT("iso_reallocarray failed")
    }

    iso_free(p);

    p = iso_alloc(1024);

    assert((iso_chunksz(p)) >= 1024);

    iso_free_permanently(p);

    iso_alloc_zone_handle *zone = iso_alloc_new_zone(256);

    if(zone == NULL) {
        LOG_AND_ABORT("Could not create a zone");
    }

    p = iso_alloc_from_zone(zone);

    if(p == NULL) {
        LOG_AND_ABORT("Could not allocate from private zone");
    }

    iso_free_from_zone(p, zone);

    iso_alloc_destroy_zone(zone);

    p = iso_alloc(1024);

    if(p == NULL) {
        LOG_AND_ABORT("iso_alloc failed");
    }

    memset(p, 0x0, 1024);

    void *r = iso_strdup(p);

    if(r == NULL) {
        LOG_AND_ABORT("iso_strdup failed");
    }

    iso_free(p);
    iso_free(r);

    void *sz = iso_alloc(8192);
    iso_free_size(sz, 8192);

    uint8_t *ap = NULL;
    int rr = 0;
    rr = posix_memalign((void**) &ap, 16, 64);
    if(ap == NULL || rr != 0 || ((uintptr_t) ap % 16) != 0) {
        LOG_AND_ABORT("ap %p | %d != 0", ap, (uintptr_t) ap % 16);
    }
    free(ap);

    rr = posix_memalign((void**) &ap, 256, 16);
    if(ap == NULL || rr != 0 || ((uintptr_t) ap % 256) != 0) {
        LOG_AND_ABORT("ap %p | %d != 0", ap, (uintptr_t) ap % 256);
    }
    free(ap);

#if HEAP_PROFILER
    iso_alloc_traces_t at[BACKTRACE_DEPTH_SZ];
    size_t alloc_trace_count = iso_get_alloc_traces(at);

    for(int32_t i = 0; i < alloc_trace_count; i++) {
        iso_alloc_traces_t *abts = &at[i];
        LOG("alloc_backtrace=%d,backtrace_hash=0x%x,calls=%d,lower_bound_size=%d,upper_bound_size=%d,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
            i, abts->backtrace_hash, abts->call_count, abts->lower_bound_size, abts->upper_bound_size, abts->callers[0], abts->callers[1],
            abts->callers[2], abts->callers[3], abts->callers[4], abts->callers[5], abts->callers[6], abts->callers[7]);
    }

    iso_free_traces_t ft[BACKTRACE_DEPTH_SZ];
    size_t free_trace_count = iso_get_free_traces(ft);

    for(int32_t i = 0; i < free_trace_count; i++) {
        iso_free_traces_t *fbts = &ft[i];
        LOG("free_backtrace=%d,backtrace_hash=0x%x,calls=%d,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
            i, fbts->backtrace_hash, fbts->call_count, fbts->callers[0], fbts->callers[1], fbts->callers[2], fbts->callers[3],
            fbts->callers[4], fbts->callers[5], fbts->callers[6], fbts->callers[7]);
    }

    iso_alloc_reset_traces();
#endif

    iso_flush_caches();
    iso_verify_zones();

    return 0;
}
