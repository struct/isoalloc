/* iso_alloc tag_refresh_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for _refresh_zone_mem_tags. Once a zone
 * has no live allocations and has seen a quarter of its
 * retirement threshold in allocations since the last
 * refresh, the next free regenerates every tag in the zone.
 * The condition for this was previously miscomputed so it
 * never fired, and when it did it wrote through the masked
 * user pages pointer. It also never recorded that a refresh
 * had happened, so every later free from an idle zone paid
 * for a full refresh. This test fills a small zone, drives
 * it to exactly the threshold, and checks that the next
 * free changes every other tag in the zone while the free
 * after that changes none of them */
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

static iso_alloc_zone_handle *_zone_handle;
static void *chunks[CHUNKS];
static size_t count;

/* Allocates one tagged chunk, snapshots the tags of every
 * other chunk, frees it, and returns how many of those
 * tags were left unchanged by the free */
static size_t cycle_and_count_unchanged(void) {
    void *q = iso_alloc_from_zone_tagged(_zone_handle);

    if(q == NULL) {
        LOG_AND_ABORT("Failed to allocate a tagged chunk");
    }

    iso_alloc_verify_ptr_tag(q, _zone_handle);
    void *uq = iso_alloc_untag_ptr(q, _zone_handle);
    uint8_t before[CHUNKS];

    for(size_t i = 0; i < count; i++) {
        before[i] = iso_alloc_get_mem_tag(chunks[i], _zone_handle);
    }

    iso_free_from_zone(uq, _zone_handle);

    /* The freed chunk's own tag always changes */
    if(iso_alloc_untag_ptr(q, _zone_handle) == uq) {
        LOG_AND_ABORT("Stale pointer %p untagged to its original address", q);
    }

    size_t unchanged = 0;

    for(size_t i = 0; i < count; i++) {
        if(chunks[i] == uq) {
            continue;
        }

        if(iso_alloc_get_mem_tag(chunks[i], _zone_handle) == before[i]) {
            unchanged++;
        }
    }

    return unchanged;
}

int main(int argc, char *argv[]) {
    _zone_handle = iso_alloc_new_zone(SIZE);

    if(_zone_handle == NULL) {
        abort();
    }

    /* Allocate every chunk in the zone so we know their
     * addresses, then free them all */
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
     * chunks and more than threshold allocations have been
     * made since the last refresh. Every allocation counts,
     * including the fill above, so bring the total to
     * exactly the threshold */
    const size_t threshold = (CHUNKS * ZONE_ALLOC_RETIRE) / 4;

    for(size_t i = count; i < threshold; i++) {
        void *p = iso_alloc_from_zone_tagged(_zone_handle);

        if(p == NULL) {
            LOG_AND_ABORT("Failed to allocate tagged chunk in cycle %d", i);
        }

        iso_alloc_verify_ptr_tag(p, _zone_handle);
        iso_free_from_zone(p, _zone_handle);
    }

    /* Allocation number threshold + 1 crosses it, so its
     * free must refresh every tag in the zone */
    size_t unchanged = cycle_and_count_unchanged();

    if(unchanged != 0) {
        LOG_AND_ABORT("Zone tag refresh left %d of %d chunk tags unchanged", unchanged, count);
    }

    /* The refresh must not repeat on the next free. Only the
     * freed chunk's own tag changes, every other tag stays */
    unchanged = cycle_and_count_unchanged();

    if(unchanged != (count - 1)) {
        LOG_AND_ABORT("Zone tags were refreshed again on the next free, %d of %d unchanged", unchanged, count - 1);
    }

    /* After another threshold's worth of allocations it
     * must fire again. The two cycles above already made
     * two allocations past the first refresh */
    for(size_t i = 0; i < threshold - 1; i++) {
        void *p = iso_alloc_from_zone_tagged(_zone_handle);

        if(p == NULL) {
            LOG_AND_ABORT("Failed to allocate tagged chunk in second cycle %d", i);
        }

        iso_free_from_zone(p, _zone_handle);
    }

    unchanged = cycle_and_count_unchanged();

    if(unchanged != 0) {
        LOG_AND_ABORT("Second zone tag refresh left %d of %d chunk tags unchanged", unchanged, count);
    }

    iso_flush_caches();
    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
