/* iso_alloc_search.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks() {
    uint64_t total_leaks = 0;
    uint64_t big_leaks = 0;

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        if(zone == NULL) {
            break;
        }

        total_leaks += _iso_alloc_zone_leak_detector(zone);
    }

    iso_alloc_big_zone *big = _root->big_zone_head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big != NULL) {
        if(big->free == true) {
            big_leaks += big->size;
            LOG("Big zone leaked %" PRIu64 " bytes", big->size);
        }

        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
        }
    }

    LOG("Total leaked in big zones: bytes (%" PRIu64 ") megabytes (%" PRIu64 ")", big_leaks, (big_leaks / MEGABYTE_SIZE));

    return total_leaks + big_leaks;
}

/* This is the built-in leak detector. It works by scanning
 * the bitmap for every allocated zone and looking for
 * uncleared bits. All user allocations should have been
 * free'd by the time this function runs! */
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone *zone) {
    uint64_t total_leaks = 0;

#if LEAK_DETECTOR
    if(zone == NULL) {
        return 0;
    }

    UNMASK_ZONE_PTRS(zone);

    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    bit_slot_t bit_slot;
    int64_t was_used = 0;

    for(int64_t i = 0; i < zone->bitmap_size / sizeof(bitmap_index_t); i++) {
        for(size_t j = 0; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            int64_t bit = GET_BIT(bm[i], j);
            int64_t bit_two = GET_BIT(bm[i], (j + 1));

            /* Chunk was used but is now free */
            if(bit == 0 && bit_two == 1) {
                was_used++;
            }

            /* Theres no difference between a leaked and previously
             * used chunk (11) and a canary chunk (11). So in order
             * to accurately report on leaks we need to verify the
             * canary value. If it doesn't validate then we assume
             * its a true leak and increment the total_leaks counter */
            if(bit == 1) {
                bit_slot = (i * BITS_PER_QWORD) + j;
                void *leak = (zone->user_pages_start + ((bit_slot / BITS_PER_CHUNK) * zone->chunk_size));

                if(bit_two == 1 && (check_canary_no_abort(zone, leak) != ERR)) {
                    continue;
                } else {
                    total_leaks++;
                    LOG("Leaked chunk of %d bytes detected in zone[%d] at %p (bit position = %" PRIu64 ")", zone->chunk_size, zone->index, leak, bit_slot);
                }
            }
        }
    }

    LOG("Zone[%d] Total number of %d byte chunks(%d) used and free'd (%" PRIu64 ") (%%%d)", zone->index, zone->chunk_size, GET_CHUNK_COUNT(zone),
        was_used, (int32_t)((float) was_used / (GET_CHUNK_COUNT(zone)) * 100.0));

    MASK_ZONE_PTRS(zone);
#endif

    return total_leaks;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone) {
    uint64_t mem_usage = 0;
    mem_usage += zone->bitmap_size;
    mem_usage += ZONE_USER_SIZE;
    LOG("Zone[%d] holds %d byte chunks. Total bytes (%" PRIu64 "), megabytes (%" PRIu64 ")", zone->index, zone->chunk_size, mem_usage, (mem_usage / MEGABYTE_SIZE));
    return (mem_usage / MEGABYTE_SIZE);
}

INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage() {
    uint64_t mem_usage = 0;

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];
        mem_usage += zone->bitmap_size;
        mem_usage += ZONE_USER_SIZE;
        LOG("Zone[%d] holds %d byte chunks, megabytes (%d)", zone->index, zone->chunk_size, (ZONE_USER_SIZE / MEGABYTE_SIZE));
    }

    iso_alloc_big_zone *big = _root->big_zone_head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big != NULL) {
        LOG("Big Zone Total bytes (%" PRIu64 "), megabytes (%" PRIu64 ")", big->size, (big->size / MEGABYTE_SIZE));
        mem_usage += big->size;
        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
        }
    }

    LOG("Total megabytes allocated (%" PRIu64 ")", (mem_usage / MEGABYTE_SIZE));

    return (mem_usage / MEGABYTE_SIZE);
}
