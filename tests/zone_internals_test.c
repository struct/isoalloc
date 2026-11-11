/* iso_alloc zone_internals_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression tests that need to look inside the root.
 *
 * Canary placement: the random bitmap index used to place
 * canaries was aligned down to SZ_ALIGNMENT, which limited
 * canaries to every 32nd qword of the bitmap. A 1024 byte
 * zone has 128 bitmap qwords and should get about 32
 * canaries but could only ever get 4.
 *
 * Shared small bitmaps: the 16 byte bitmap bucket was never
 * backed by a page so zones with 16 byte bitmaps always fell
 * back to a dedicated mapping, and the in_use field was 32
 * bits wide while a page holds up to 1024 slices of that
 * size, so slot indexes past 31 shifted out of range */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !UNIT_TESTING
#error "This test needs the library built with -DUNIT_TESTING=1"
#endif

/* g_page_size is hidden inside the library. ROUND_DOWN_PAGE
 * expands to it by name, so point that name at a local copy
 * to exercise the real macro from here */
static uintptr_t local_page_size;
#define g_page_size local_page_size

/* 65536 byte chunks give 64 chunks per zone and a 16 byte bitmap */
#define SMALL_BITMAP_CHUNK_SZ SMALL_SIZE_MAX
#define SMALL_BITMAP_ZONES 70

static iso_alloc_zone_t *find_zone(iso_alloc_root *root, size_t chunk_size, bool internal) {
    for(size_t i = 0; i < root->zones_used; i++) {
        iso_alloc_zone_t *zone = &root->zones[i];

        if(zone->chunk_size == chunk_size && zone->internal == internal) {
            return zone;
        }
    }

    return NULL;
}

static size_t count_canaries(iso_alloc_zone_t *zone) {
    const bitmap_index_t *bm = UNMASK_BITMAP_PTR(zone);
    size_t canaries = 0;

    for(size_t i = 0; i < zone->max_bitmap_idx; i++) {
        /* A canary chunk has both of its bits set. Only the
         * first chunk of a qword is ever made a canary */
        if((bm[i] & 3) == 3) {
            canaries++;
        }
    }

    return canaries;
}

/* Walks every zone with a 16 byte bitmap and checks that the
 * slices they hold are in range, distinct, and exactly the
 * bits set in the bucket's in_use field */
static void verify_slices(iso_alloc_root *root, size_t *shared_out, size_t *dedicated_out) {
    const size_t slices = (sizeof(root->bitmaps[0].in_use) * BITS_PER_BYTE);
    const size_t buckets = (sizeof(small_bitmap_sizes) / sizeof(int)) - 1;
    iso_alloc_bitmap_t *bucket = NULL;

    for(size_t i = 0; i < buckets; i++) {
        if(root->bitmaps[i].bucket == BITMAP_SIZE_16) {
            bucket = &root->bitmaps[i];
        }
    }

    if(bucket == NULL || bucket->bitmap == NULL) {
        LOG_AND_ABORT("The %d byte bitmap bucket has no page", BITMAP_SIZE_16);
    }

    uint64_t seen = 0;
    size_t shared = 0;
    size_t dedicated = 0;

    for(size_t i = 0; i < root->zones_used; i++) {
        iso_alloc_zone_t *z = &root->zones[i];

        if(z->chunk_size != SMALL_BITMAP_CHUNK_SZ) {
            continue;
        }

        if(z->bitmap_size != BITMAP_SIZE_16) {
            LOG_AND_ABORT("Zone[%d] has a %d byte bitmap, expected %d", z->index, z->bitmap_size, BITMAP_SIZE_16);
        }

        if(z->preallocated_bitmap_idx < 0) {
            dedicated++;
            continue;
        }

        const size_t idx = (size_t) z->preallocated_bitmap_idx;

        if(idx >= slices) {
            LOG_AND_ABORT("Zone[%d] has bitmap slot %d but in_use only has %d bits", z->index, idx, slices);
        }

        if(seen & (1ULL << idx)) {
            LOG_AND_ABORT("Zone[%d] shares bitmap slot %d with another zone", z->index, idx);
        }

        if(UNMASK_BITMAP_PTR(z) != (bucket->bitmap + (idx * BITMAP_SIZE_16))) {
            LOG_AND_ABORT("Zone[%d] bitmap does not match its slot %d", z->index, idx);
        }

        seen |= (1ULL << idx);
        shared++;
    }

    if(bucket->in_use != seen) {
        LOG_AND_ABORT("Bucket in_use is %lu but zones hold %lu", bucket->in_use, seen);
    }

    *shared_out = shared;
    *dedicated_out = dedicated;
}

int main(int argc, char *argv[]) {
    /* Prime the allocator so the default zones exist */
    void *p = iso_alloc(ZONE_1024);
    iso_free(p);

    iso_alloc_root *root = _get_root();

    if(root == NULL) {
        LOG_AND_ABORT("No root");
    }

    /* Both macros previously rounded up and subtracted one
     * unit, which was one unit short for aligned input and
     * underflowed for 0. The page macro is used to find the
     * page that holds big zone meta data, which can sit at
     * offset 0 of its page */
    local_page_size = (uintptr_t) sysconf(_SC_PAGESIZE);
    const uintptr_t page = local_page_size;
    const uintptr_t sz_aligned[] = {0, SZ_ALIGNMENT, SZ_ALIGNMENT * 3, page};
    const uintptr_t page_aligned[] = {0, page, page * 3};

    for(size_t i = 0; i < sizeof(sz_aligned) / sizeof(uintptr_t); i++) {
        const uintptr_t v = sz_aligned[i];

        if(ALIGN_SZ_DOWN(v) != v || ALIGN_SZ_DOWN(v + 1) != v || ALIGN_SZ_DOWN(v + SZ_ALIGNMENT - 1) != v) {
            LOG_AND_ABORT("ALIGN_SZ_DOWN is wrong near %lu", v);
        }
    }

    for(size_t i = 0; i < sizeof(page_aligned) / sizeof(uintptr_t); i++) {
        const uintptr_t v = page_aligned[i];

        if(ROUND_DOWN_PAGE(v) != v || ROUND_DOWN_PAGE(v + 1) != v || ROUND_DOWN_PAGE(v + page - 1) != v) {
            LOG_AND_ABORT("ROUND_DOWN_PAGE is wrong near %lu", v);
        }
    }

#if !DISABLE_CANARY
    iso_alloc_zone_t *zone = find_zone(root, ZONE_1024, true);

    if(zone == NULL) {
        LOG_AND_ABORT("No internal %d byte zone", ZONE_1024);
    }

    /* About 1 percent of chunks are canaries. Random picks
     * can collide so accept half of the target, but the old
     * placement could never exceed max_bitmap_idx / 32 */
    const size_t target = (zone->chunk_count >> CANARY_COUNT_DIV);
    const size_t canaries = count_canaries(zone);

    if(canaries < (target / 2) || canaries > target) {
        LOG_AND_ABORT("%d byte zone has %d canaries, expected about %d", ZONE_1024, canaries, target);
    }
#endif

    /* Every one of these zones has a 16 byte bitmap. The first
     * 64 must share one page of preallocated bitmaps, the
     * rest fall back to their own mapping */
    iso_alloc_zone_handle *handles[SMALL_BITMAP_ZONES];

    for(size_t i = 0; i < SMALL_BITMAP_ZONES; i++) {
        handles[i] = iso_alloc_new_zone(SMALL_BITMAP_CHUNK_SZ);

        if(handles[i] == NULL) {
            LOG_AND_ABORT("Failed to create zone %d", i);
        }

        /* Every zone must be usable regardless of where
         * its bitmap lives */
        void *q = iso_alloc_from_zone(handles[i]);

        if(q == NULL) {
            LOG_AND_ABORT("Failed to allocate from zone %d", i);
        }

        memset(q, 0x41, SMALL_BITMAP_CHUNK_SZ);
        iso_free_from_zone(q, handles[i]);
    }

    const size_t slices = (sizeof(root->bitmaps[0].in_use) * BITS_PER_BYTE);
    size_t shared = 0;
    size_t dedicated = 0;

    verify_slices(root, &shared, &dedicated);

    if(shared != slices || dedicated != (SMALL_BITMAP_ZONES - slices)) {
        LOG_AND_ABORT("%d zones share preallocated bitmaps and %d have their own, expected %d and %d", shared, dedicated, slices, SMALL_BITMAP_ZONES - slices);
    }

    /* Destroying a zone releases its slice and then creates a
     * replacement internal zone at the same index, which takes
     * a slice again. The bookkeeping must stay consistent
     * through that release and reacquire */
    for(size_t i = 0; i < SMALL_BITMAP_ZONES; i++) {
        iso_alloc_destroy_zone(handles[i]);
    }

    verify_slices(root, &shared, &dedicated);

    if(shared != slices) {
        LOG_AND_ABORT("%d zones share preallocated bitmaps after destroy, expected %d", shared, slices);
    }

    iso_verify_zones();

    return 0;
}
