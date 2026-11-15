/* iso_alloc pointer_map_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* The zone map answers which zone owns a pointer with one hash
 * probe. That relies on zone user pages being aligned to their
 * own size so a pointers 4 MB window names its zone. Big zones
 * have a map of their own keyed on their first page. This test
 * checks the alignment, that the maps track zone creation and
 * destruction exactly, and that big zones are found however
 * many are live and whichever order they are freed in */
#include <stdint.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !UNIT_TESTING
#error "This test needs the library built with -DUNIT_TESTING=1"
#endif

#define BIG_COUNT 1500
#define BIG_SIZE 70000
#define PRIVATE_ZONES 100

static void check_maps(iso_alloc_root *root) {
    if(root->zone_map.count != root->zones_used) {
        LOG_AND_ABORT("Zone map holds %lu entries for %d zones", root->zone_map.count, root->zones_used);
    }

    for(size_t i = 0; i < root->zones_used; i++) {
        iso_alloc_zone_t *zone = &root->zones[i];
        const uintptr_t start = (uintptr_t) UNMASK_USER_PTR(zone);

        if((start & (ZONE_USER_SIZE - 1)) != 0) {
            LOG_AND_ABORT("Zone[%d] user pages at 0x%p are not aligned to %d", zone->index, start, ZONE_USER_SIZE);
        }
    }

    if(root->big_zone_map.count != (uint64_t) root->big_zone_used_count) {
        LOG_AND_ABORT("Big zone map holds %lu entries for %d used big zones", root->big_zone_map.count, root->big_zone_used_count);
    }
}

int main(int argc, char *argv[]) {
    void *p = iso_alloc(64);
    iso_free(p);

    iso_alloc_root *root = _get_root();

    if(root == NULL) {
        LOG_AND_ABORT("No root");
    }

    check_maps(root);

    /* Pointers anywhere in a zone must resolve to it, including
     * the first and last chunk. Private zones give a handle to
     * check against */
    iso_alloc_zone_handle *handles[PRIVATE_ZONES];
    void *chunks[PRIVATE_ZONES];

    for(size_t i = 0; i < PRIVATE_ZONES; i++) {
        const size_t size = 32 + (i % 8) * 16;
        handles[i] = iso_alloc_new_zone(size);

        if(handles[i] == NULL) {
            LOG_AND_ABORT("Failed to create zone %d", i);
        }

        chunks[i] = iso_alloc_from_zone(handles[i]);

        if(chunks[i] == NULL || iso_chunksz(chunks[i]) != size) {
            LOG_AND_ABORT("Zone %d handed out %p with chunk size %d, expected %d", i, chunks[i], iso_chunksz(chunks[i]), size);
        }
    }

    check_maps(root);

    /* A thread cache full of other zones must not matter,
     * every lookup goes through the map */
    iso_flush_caches();

    for(size_t i = 0; i < PRIVATE_ZONES; i++) {
        if(iso_chunksz(chunks[i]) != 32 + (i % 8) * 16) {
            LOG_AND_ABORT("Lookup of %p failed after the cache was flushed", chunks[i]);
        }

        iso_free_from_zone(chunks[i], handles[i]);
    }

    /* Destroying a zone removes its window and the replacement
     * zone adds its own, so the count stays one per zone */
    for(size_t i = 0; i < PRIVATE_ZONES; i++) {
        iso_alloc_destroy_zone(handles[i]);
        check_maps(root);
    }

    /* Big zones. Freeing oldest first used to walk the whole
     * used list for every free */
    void *big[BIG_COUNT];

    for(size_t i = 0; i < BIG_COUNT; i++) {
        big[i] = iso_alloc(BIG_SIZE);

        if(big[i] == NULL || iso_chunksz(big[i]) < BIG_SIZE) {
            LOG_AND_ABORT("Big allocation %d failed", i);
        }

        ((char *) big[i])[0] = 1;
    }

    check_maps(root);

    for(size_t i = 0; i < BIG_COUNT; i++) {
        if(iso_chunksz(big[i]) < BIG_SIZE) {
            LOG_AND_ABORT("Big zone %d at %p was not found", i, big[i]);
        }

        iso_free(big[i]);
    }

    /* Frees wait in the quarantine until it is flushed */
    iso_flush_caches();

    if(root->big_zone_used_count != 0) {
        LOG_AND_ABORT("%d big zones still in use", root->big_zone_used_count);
    }

    check_maps(root);

    /* A big zone taken back off the free list must be found
     * again, and freeing newest first must work too */
    for(size_t i = 0; i < 64; i++) {
        big[i] = iso_alloc(BIG_SIZE);

        if(big[i] == NULL || iso_chunksz(big[i]) < BIG_SIZE) {
            LOG_AND_ABORT("Reused big allocation %d failed", i);
        }
    }

    check_maps(root);

    for(size_t i = 64; i-- > 0;) {
        iso_free(big[i]);
    }

    iso_flush_caches();
    check_maps(root);

    /* A permanent free unmaps the big zone outright */
    p = iso_alloc(BIG_SIZE);
    iso_free_permanently(p);
    check_maps(root);

    iso_verify_zones();
    return 0;
}
