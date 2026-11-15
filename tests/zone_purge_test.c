/* iso_alloc zone_purge_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* A zone with no chunks in use is reset in place: its pages go
 * back to the OS, its bitmap is cleared, and it gets new secrets.
 * This happens when a zone retires and, after a delay, to any
 * zone that stays empty. This test fills a zone, frees it, and
 * checks that the purge drops its resident pages, that the zone
 * still verifies, and that chunks handed out afterwards are
 * zero. The timed trigger on the free path is checked too */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !UNIT_TESTING
#error "This test needs the library built with -DUNIT_TESTING=1"
#endif

#define CHUNK 1024
#define CHUNKS 2048

static size_t page_size;

/* Resident pages in the user pages of a zone */
static size_t resident_pages(iso_alloc_zone_t *zone) {
    void *start = UNMASK_USER_PTR(zone);
    const size_t pages = (ZONE_USER_SIZE / page_size);
    char vec[ZONE_USER_SIZE / 4096];

    if(mincore(start, ZONE_USER_SIZE, (void *) vec) != 0) {
        LOG_AND_ABORT("mincore failed for zone[%d]", zone->index);
    }

    size_t resident = 0;

    for(size_t i = 0; i < pages; i++) {
        if(vec[i] & 1) {
            resident++;
        }
    }

    return resident;
}

static iso_alloc_zone_t *private_zone(iso_alloc_root *root, size_t chunk_size) {
    for(size_t i = 0; i < root->zones_used; i++) {
        if(root->zones[i].chunk_size == chunk_size && root->zones[i].internal == false) {
            return &root->zones[i];
        }
    }

    LOG_AND_ABORT("No private zone with %d byte chunks", chunk_size);
}

static void fill_and_free(iso_alloc_zone_handle *handle, void **chunks) {
    for(size_t i = 0; i < CHUNKS; i++) {
        chunks[i] = iso_alloc_from_zone(handle);

        if(chunks[i] == NULL) {
            LOG_AND_ABORT("Allocation %d failed", i);
        }

        memset(chunks[i], 0x41, CHUNK);
    }

    for(size_t i = 0; i < CHUNKS; i++) {
        iso_free_from_zone(chunks[i], handle);
    }
}

int main(int argc, char *argv[]) {
    page_size = (size_t) sysconf(_SC_PAGESIZE);
    void *chunks[CHUNKS];

    iso_alloc_zone_handle *handle = iso_alloc_new_zone(CHUNK);

    if(handle == NULL) {
        LOG_AND_ABORT("Cannot create a zone");
    }

    iso_alloc_root *root = _get_root();
    iso_alloc_zone_t *zone = private_zone(root, CHUNK);

#if __linux__ && defined(MADV_NOHUGEPAGE)
    /* With transparent huge pages set to always a single touch
     * makes 2 MB resident, which hides what the purge released.
     * Residency is counted in small pages for this zone */
    madvise(UNMASK_USER_PTR(zone), ZONE_USER_SIZE, MADV_NOHUGEPAGE);
#endif

    const uint64_t secret = zone->canary_secret;
    const uint64_t mask = zone->pointer_mask;
    const void *start = UNMASK_USER_PTR(zone);
    const uint16_t index = zone->index;

    fill_and_free(handle, chunks);

    const size_t before = resident_pages(zone);

    if(before < ((CHUNKS * CHUNK) / page_size) / 2) {
        LOG_AND_ABORT("Only %d pages resident after writing %d chunks", before, CHUNKS);
    }

    /* The zone just became empty so the first pass must leave
     * it alone, it may be about to be reused */
    _iso_alloc_purge_now();

    if(zone->alloc_count == 0) {
        LOG_AND_ABORT("Zone was purged in the pass right after it emptied");
    }

    /* The second pass finds it empty since the previous pass */
    _iso_alloc_purge_now();

    const size_t after = resident_pages(zone);

    if(zone->alloc_count != 0 || zone->af_count != 0) {
        LOG_AND_ABORT("Zone was not reset, alloc_count %d af_count %d", zone->alloc_count, zone->af_count);
    }

    /* Only the pages the fresh free list touched for canaries
     * should be resident */
    if(after >= before / 4) {
        LOG_AND_ABORT("%d pages resident after the purge, %d before", after, before);
    }

    /* Same mapping and index, new secrets */
    if(UNMASK_USER_PTR(zone) != start || zone->index != index) {
        LOG_AND_ABORT("Reset moved the zone");
    }

    if(zone->canary_secret == secret || zone->pointer_mask == mask) {
        LOG_AND_ABORT("Reset kept the old secrets");
    }

    if(root->zone_map.count != root->zones_used) {
        LOG_AND_ABORT("Zone map changed across a reset");
    }

    iso_verify_zones();

    /* Chunks from a reset zone must be usable and zero */
    for(size_t i = 0; i < 256; i++) {
        uint8_t *p = iso_alloc_from_zone(handle);

        if(p == NULL) {
            LOG_AND_ABORT("Allocation after reset failed");
        }

        for(size_t b = 0; b < CHUNK; b++) {
            if(p[b] != 0) {
                LOG_AND_ABORT("Chunk %p from a reset zone is not zero at byte %d", p, b);
            }
        }

        chunks[i] = p;
    }

    for(size_t i = 0; i < 256; i++) {
        iso_free_from_zone(chunks[i], handle);
    }

    iso_verify_zones();

#if ZONE_PURGE_DELAY_MS > 0
    /* The timed trigger. Fill and free again, wait past the
     * delay twice with a quarantine flush in between so two
     * passes run, and the zone must have been reset */
    fill_and_free(handle, chunks);
    const uint32_t count_before = zone->alloc_count;

    for(int pass = 0; pass < 2; pass++) {
        usleep((ZONE_PURGE_DELAY_MS + 200) * 1000);

        /* More frees than the quarantine holds forces a flush,
         * which is where the clock is checked */
        for(size_t i = 0; i <= CHUNK_QUARANTINE_SZ; i++) {
            void *p = iso_alloc(32);
            iso_free(p);
        }
    }

    if(zone->alloc_count >= count_before) {
        LOG_AND_ABORT("The timed purge did not reset the idle zone");
    }

    iso_verify_zones();
#endif

    /* A zone is also reset when it retires, which the
     * zone_retirement test covers. Destroying the private zone
     * must still work after resets */
    iso_alloc_destroy_zone(handle);
    iso_verify_zones();
    return 0;
}
