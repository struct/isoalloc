/* iso_alloc size_class_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Checks the chunk size rules. The minimum chunk size and
 * alignment is 16 bytes, every zones bitmap covers exactly
 * the chunks it holds, and every chunk a zone hands out lies
 * inside its user pages */
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !UNIT_TESTING
#error "This test needs the library built with -DUNIT_TESTING=1"
#endif

static size_t page_size;

static void check_zone_shape(iso_alloc_zone_t *zone) {
    const size_t covered = zone->max_bitmap_idx * (BITS_PER_QWORD / BITS_PER_CHUNK);
    const size_t lowbit = (zone->chunk_size & (~zone->chunk_size + 1));
    const size_t unit = (lowbit > CHUNK_OFFSET_UNIT) ? lowbit : CHUNK_OFFSET_UNIT;

    /* The bitmap covers the chunks of an offset of 0 and the
     * chunk offset can only cost chunks within one page */
    if(zone->chunk_count > covered || (covered - zone->chunk_count) > (page_size / zone->chunk_size) + 1) {
        LOG_AND_ABORT("Zone[%d] has %d chunks but its bitmap covers %d", zone->index, zone->chunk_count, covered);
    }

    if(zone->bitmap_size != (zone->max_bitmap_idx * sizeof(bitmap_index_t))) {
        LOG_AND_ABORT("Zone[%d] bitmap is %d bytes for %d qwords", zone->index, zone->bitmap_size, zone->max_bitmap_idx);
    }

    if((zone->chunk_offset + (uint64_t) zone->chunk_count * zone->chunk_size) > ZONE_USER_SIZE) {
        LOG_AND_ABORT("Zone[%d] holds %d chunks of %d bytes at offset %d, more than its user pages", zone->index, zone->chunk_count, zone->chunk_size, zone->chunk_offset);
    }

    /* The offset keeps the natural alignment of the chunk size
     * and stays within a page */
    if((zone->chunk_offset % unit) != 0 || zone->chunk_offset >= page_size) {
        LOG_AND_ABORT("Zone[%d] has chunk offset %d for %d byte chunks", zone->index, zone->chunk_offset, zone->chunk_size);
    }

    /* Bits past the last chunk are all set so no scan can hand
     * out a chunk that does not exist */
    const bitmap_index_t *bm = UNMASK_BITMAP_PTR(zone);

    for(size_t c = zone->chunk_count; c < covered; c++) {
        const bit_slot_t bit_slot = (c << BITS_PER_CHUNK_SHIFT);

        if(((bm[bit_slot >> BITS_PER_QWORD_SHIFT] >> WHICH_BIT(bit_slot)) & 3) != 3) {
            LOG_AND_ABORT("Zone[%d] chunk %d past the end is not marked used", zone->index, c);
        }
    }

    if(zone->seed == 0) {
        LOG_AND_ABORT("Zone[%d] has no random seed", zone->index);
    }
}

int main(int argc, char *argv[]) {
    page_size = (size_t) sysconf(_SC_PAGESIZE);

    /* The smallest request gets the smallest chunk and
     * every chunk is SZ_ALIGNMENT aligned */
    void *p = iso_alloc(1);

    if(p == NULL || iso_chunksz(p) != SZ_ALIGNMENT || ((uintptr_t) p & (SZ_ALIGNMENT - 1)) != 0) {
        LOG_AND_ABORT("iso_alloc(1) returned %p with chunk size %d", p, iso_chunksz(p));
    }

    iso_free(p);

    for(size_t s = 1; s <= 4096; s++) {
        p = iso_alloc(s);

        if(p == NULL || iso_chunksz(p) < s || ((uintptr_t) p & (SZ_ALIGNMENT - 1)) != 0) {
            LOG_AND_ABORT("iso_alloc(%d) returned %p with chunk size %d", s, p, iso_chunksz(p));
        }

        memset(p, 0x41, s);
        iso_free(p);
    }

    /* Every request lands in exactly its size class and never
     * in a bigger one. Above the exact classes a chunk is at
     * most a quarter larger than the aligned request */
    for(size_t s = 1; s <= SMALL_SIZE_MAX; s += (s < 512) ? 1 : 97) {
        const size_t aligned = (s < SMALLEST_CHUNK_SZ) ? SMALLEST_CHUNK_SZ : ALIGN_SZ_UP(s);
        const size_t cls = size_class(aligned);
        p = iso_alloc(s);

        if(p == NULL || iso_chunksz(p) != cls) {
            LOG_AND_ABORT("iso_alloc(%d) got a %d byte chunk, its class is %d", s, iso_chunksz(p), cls);
        }

        if(cls < aligned || (cls - aligned) > (aligned / SIZE_CLASS_STEPS_PER_POW2)) {
            LOG_AND_ABORT("Class %d wastes too much for a %d byte request", cls, aligned);
        }

        if(size_class_index(cls) >= SIZE_CLASS_COUNT && size_class_index(cls) != SIZE_CLASS_NONE) {
            LOG_AND_ABORT("Class %d has slot %d, the table holds %d", cls, size_class_index(cls), SIZE_CLASS_COUNT);
        }

        iso_free(p);
    }

    /* Every class maps to a distinct table slot */
    size_t last_idx = SIZE_CLASS_NONE;

    for(size_t s = SMALLEST_CHUNK_SZ; s <= SMALL_SIZE_MAX; s += SZ_ALIGNMENT) {
        const size_t cls = size_class(s);
        const size_t idx = size_class_index(cls);

        if(cls != s) {
            continue;
        }

        if(idx != SIZE_CLASS_NONE && last_idx != SIZE_CLASS_NONE && idx != last_idx + 1) {
            LOG_AND_ABORT("Class %d has slot %d, the class before it has %d", cls, idx, last_idx);
        }

        if(idx != SIZE_CLASS_NONE) {
            last_idx = idx;
        }
    }

    /* The thread cache used to hand a 32 byte request the most
     * recently used zone that fit, so alternating 1024 and 32
     * byte requests put every 32 byte request in a 1024 byte
     * chunk. Each request must get its own class */
    void *held[512];

    for(size_t i = 0; i < 256; i++) {
        held[i * 2] = iso_alloc(1024);
        held[i * 2 + 1] = iso_alloc(32);

        if(iso_chunksz(held[i * 2]) != 1024 || iso_chunksz(held[i * 2 + 1]) != 32) {
            LOG_AND_ABORT("Alternating requests got %d and %d byte chunks", iso_chunksz(held[i * 2]), iso_chunksz(held[i * 2 + 1]));
        }
    }

    for(size_t i = 0; i < 512; i++) {
        iso_free(held[i]);
    }

    iso_alloc_root *root = _get_root();

    if(root == NULL) {
        LOG_AND_ABORT("No root");
    }

    for(size_t i = 0; i < root->zones_used; i++) {
        check_zone_shape(&root->zones[i]);

        /* Every zone draws from its own random stream */
        for(size_t j = 0; j < i; j++) {
            if(root->zones[i].seed == root->zones[j].seed) {
                LOG_AND_ABORT("Zones %d and %d share a random seed", i, j);
            }
        }
    }

    /* A refill advances the stream. 16 byte chunks refill the
     * free list every ZONE_FREE_LIST_SZ allocations */
    const uint64_t seed_before = root->zones[0].seed;
    void *refill[ZONE_FREE_LIST_SZ + 1];

    for(size_t i = 0; i < ZONE_FREE_LIST_SZ + 1; i++) {
        refill[i] = iso_alloc(SMALLEST_CHUNK_SZ);
    }

    if(root->zones[0].seed == seed_before) {
        LOG_AND_ABORT("Refilling the free list did not advance the zones random stream");
    }

    for(size_t i = 0; i < ZONE_FREE_LIST_SZ + 1; i++) {
        iso_free(refill[i]);
    }

    /* 48 does not divide the zone evenly. The bitmap used to be
     * truncated so the last chunks had no bits. Every chunk the
     * zone hands out must lie inside its user pages and the
     * zone must be able to hand out at least its advertised
     * chunk count */
    iso_alloc_zone_handle *handle = iso_alloc_new_zone(48);

    if(handle == NULL) {
        LOG_AND_ABORT("Failed to create a 48 byte zone");
    }

    iso_alloc_zone_t *zone = NULL;

    for(size_t i = 0; i < root->zones_used; i++) {
        if(root->zones[i].chunk_size == 48 && root->zones[i].internal == false) {
            zone = &root->zones[i];
        }
    }

    if(zone == NULL) {
        LOG_AND_ABORT("Cannot find the 48 byte zone");
    }

    check_zone_shape(zone);

    const uintptr_t start = (uintptr_t) UNMASK_USER_PTR(zone) + zone->chunk_offset;
    const uintptr_t end = start + ((uintptr_t) zone->chunk_count * zone->chunk_size);
    const size_t advertised = iso_zone_chunk_count(handle);
    size_t allocated = 0;

    while(true) {
        void *q = iso_alloc_from_zone(handle);

        if(q == NULL) {
            break;
        }

        if((uintptr_t) q < start || ((uintptr_t) q + 48) > end) {
            LOG_AND_ABORT("Chunk %p is outside the zones chunks %p - %p", q, start, end);
        }

        allocated++;

        if(allocated > zone->chunk_count) {
            LOG_AND_ABORT("Zone handed out %d chunks but only holds %d", allocated, zone->chunk_count);
        }
    }

    if(allocated < advertised) {
        LOG_AND_ABORT("Zone handed out %d chunks, advertised %d", allocated, advertised);
    }

    iso_alloc_destroy_zone(handle);

    /* Zones of one class do not all start their chunks at the
     * same offset. 6 zones with 64 possible offsets agreeing by
     * chance is one in a billion */
    iso_alloc_zone_handle *handles[6];
    uint16_t first_offset = 0;
    bool differ = false;

    for(size_t i = 0; i < 6; i++) {
        handles[i] = iso_alloc_new_zone(32);
        void *q = iso_alloc_from_zone(handles[i]);
        iso_alloc_zone_t *z = NULL;

        for(size_t j = 0; j < root->zones_used; j++) {
            iso_alloc_zone_t *cand = &root->zones[j];
            const void *cand_start = UNMASK_USER_PTR(cand);

            if(cand->chunk_size == 32 && cand->internal == false && cand_start <= q && q < cand_start + ZONE_USER_SIZE) {
                z = cand;
            }
        }

        if(z == NULL) {
            LOG_AND_ABORT("Cannot find the zone for %p", q);
        }

        check_zone_shape(z);

        if(i == 0) {
            first_offset = z->chunk_offset;
        } else if(z->chunk_offset != first_offset) {
            differ = true;
        }

        iso_free_from_zone(q, handles[i]);
    }

    if(differ == false) {
        LOG_AND_ABORT("Every 32 byte zone starts its chunks at offset %d", first_offset);
    }

    for(size_t i = 0; i < 6; i++) {
        iso_alloc_destroy_zone(handles[i]);
    }

    iso_verify_zones();

    return 0;
}
