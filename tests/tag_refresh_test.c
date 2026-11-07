/* iso_alloc tag_refresh_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for _refresh_zone_mem_tags. Once a zone
 * has no live allocations and has seen more than a quarter
 * of its retirement threshold in allocations every free
 * regenerates every tag in the zone. The condition for this
 * was previously miscomputed so it never fired, and when it
 * did it wrote through the masked user pages pointer. This
 * test fills a small zone, drives it past the threshold, and
 * then checks that a single free changed the tag of every
 * other chunk in the zone */
#include <stdio.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMORY_TAGGING
#error "This test intended to be run with -DMEMORY_TAGGING=1"
#endif

/* Large chunks keep the zone small so the refresh
 * threshold is reached in a few hundred cycles */
#define SIZE SMALL_SIZE_MAX
#define CHUNKS (ZONE_USER_SIZE / SIZE)

int main(int argc, char *argv[]) {
    iso_alloc_zone_handle *_zone_handle = iso_alloc_new_zone(SIZE);

    if(_zone_handle == NULL) {
        abort();
    }

    /* Allocate every chunk in the zone so we know their
     * addresses, then free them all */
    void *chunks[CHUNKS];
    size_t count = 0;

    for(size_t i = 0; i < CHUNKS; i++) {
        void *p = iso_alloc_from_zone(_zone_handle);

        if(p == NULL) {
            break;
        }

        chunks[count++] = p;
    }

    if(count < (CHUNKS / 2)) {
        LOG_AND_ABORT("Only allocated %d of %d chunks", count, CHUNKS);
    }

    for(size_t i = 0; i < count; i++) {
        iso_free_from_zone(chunks[i], _zone_handle);
    }

    /* The refresh fires on a free once the zone has no live
     * chunks and alloc_count exceeds a quarter of the
     * retirement threshold. Overshoot it to be safe */
    const size_t threshold = (CHUNKS * ZONE_ALLOC_RETIRE) / 4;

    for(size_t i = 0; i < threshold + 64; i++) {
        void *p = iso_alloc_from_zone_tagged(_zone_handle);

        if(p == NULL) {
            LOG_AND_ABORT("Failed to allocate tagged chunk in cycle %d", i);
        }

        iso_alloc_verify_ptr_tag(p, _zone_handle);
        iso_free_from_zone(p, _zone_handle);
    }

    /* Allocate one chunk and snapshot the tags of every other
     * chunk. The free below must refresh all of them */
    void *q = iso_alloc_from_zone_tagged(_zone_handle);

    if(q == NULL) {
        LOG_AND_ABORT("Failed to allocate the final tagged chunk");
    }

    void *uq = iso_alloc_untag_ptr(q, _zone_handle);
    uint8_t before[CHUNKS];

    for(size_t i = 0; i < count; i++) {
        before[i] = iso_alloc_get_mem_tag(chunks[i], _zone_handle);
    }

    iso_free_from_zone(uq, _zone_handle);

    size_t unchanged = 0;

    for(size_t i = 0; i < count; i++) {
        if(chunks[i] == uq) {
            continue;
        }

        if(iso_alloc_get_mem_tag(chunks[i], _zone_handle) == before[i]) {
            unchanged++;
        }
    }

    if(unchanged != 0) {
        LOG_AND_ABORT("Zone tag refresh left %d of %d chunk tags unchanged", unchanged, count);
    }

    /* The stale pointer must no longer untag to its chunk
     * and a fresh pointer must still verify after a refresh */
    if(iso_alloc_untag_ptr(q, _zone_handle) == uq) {
        LOG_AND_ABORT("Stale pointer %p untagged to its original address after refresh", q);
    }

    void *fresh = iso_alloc_from_zone_tagged(_zone_handle);

    if(fresh == NULL) {
        LOG_AND_ABORT("Failed to allocate after refresh");
    }

    iso_alloc_verify_ptr_tag(fresh, _zone_handle);
    iso_free_from_zone(fresh, _zone_handle);

    iso_flush_caches();
    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
