/* iso_alloc.c - A secure memory allocator
 * Copyright 2021 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks() {
    uint64_t total_leaks = 0;
    uint64_t big_leaks = 0;

    LOCK_ROOT();

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        if(zone == NULL) {
            break;
        }

        total_leaks += _iso_alloc_zone_leak_detector(zone, false);
    }

    UNLOCK_ROOT();
    LOCK_BIG_ZONE();

    iso_alloc_big_zone *big = _root->big_zone_head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big != NULL) {
        if(big->free == true) {
            big_leaks += big->size;
            LOG("Big zone leaked %lu bytes", big->size);
        }

        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
        }
    }

    UNLOCK_BIG_ZONE();

    LOG("Total leaked in big zones: bytes (%lu) megabytes (%lu)", big_leaks, (big_leaks / MEGABYTE_SIZE));
    return total_leaks + big_leaks;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks_in_zone(iso_alloc_zone *zone) {
    LOCK_ROOT();
    return _iso_alloc_zone_leak_detector(zone, false);
    UNLOCK_ROOT();
}

/* This is the built-in leak detector. It works by scanning
 * the bitmap for every allocated zone and looking for
 * uncleared bits. This does not search for references from
 * a root like a GC, so if you purposefully did not free a
 * chunk then expect it to show up as leaked! */
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone *zone, bool profile) {
    uint64_t in_use = 0;

#if LEAK_DETECTOR || HEAP_PROFILER
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

            if(bit == 1) {
                /* Theres no difference between a leaked and previously
                 * used chunk (11) and a canary chunk (11). So in order
                 * to accurately report on leaks we need to verify the
                 * canary value. If it doesn't validate then we assume
                 * its a true leak and increment the in_use counter */
                bit_slot = (i * BITS_PER_QWORD) + j;
                void *leak = (zone->user_pages_start + ((bit_slot / BITS_PER_CHUNK) * zone->chunk_size));

                if(bit_two == 1 && (check_canary_no_abort(zone, leak) != ERR)) {
                    continue;
                } else {
                    in_use++;

                    if(profile == false) {
                        LOG("Leaked chunk in zone[%d] of %d bytes detected at 0x%p (bit position = %lu)", zone->index, zone->chunk_size, leak, bit_slot);
                    }
                }
            }
        }
    }

    if(profile == false) {
        LOG("Zone[%d] Total number of %d byte chunks(%d) used and free'd (%lu) (%d percent)", zone->index, zone->chunk_size, GET_CHUNK_COUNT(zone),
            was_used, (int32_t)((float) was_used / (GET_CHUNK_COUNT(zone)) * 100.0));
    }

    MASK_ZONE_PTRS(zone);
#endif

#if HEAP_PROFILER
    /* When profiling this zone we want to capture
     * the total number of allocations both currently
     * in use and previously used by this zone */
    if(profile == true) {
        uint64_t total = (in_use + was_used);
        return (uint64_t)((float) total / (GET_CHUNK_COUNT(zone)) * 100.0);
    }
#endif
    return in_use;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone) {
    uint64_t mem_usage = 0;
    LOCK_ROOT();
    mem_usage += zone->bitmap_size;
    mem_usage += ZONE_USER_SIZE;
    LOG("Zone[%d] holds %d byte chunks. Total bytes (%lu), megabytes (%lu)", zone->index, zone->chunk_size, mem_usage, (mem_usage / MEGABYTE_SIZE));
    UNLOCK_ROOT();
    return (mem_usage / MEGABYTE_SIZE);
}

INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage() {
    LOCK_ROOT();
    uint64_t mem_usage = 0;

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];
        mem_usage += zone->bitmap_size;
        mem_usage += ZONE_USER_SIZE;
        LOG("Zone[%d] holds %d byte chunks, megabytes (%d)", zone->index, zone->chunk_size, (ZONE_USER_SIZE / MEGABYTE_SIZE));
    }

    UNLOCK_ROOT();
    LOCK_BIG_ZONE();

    iso_alloc_big_zone *big = _root->big_zone_head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big != NULL) {
        LOG("Big Zone Total bytes (%lu), megabytes (%lu)", big->size, (big->size / MEGABYTE_SIZE));
        mem_usage += big->size;
        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
        }
    }

    UNLOCK_BIG_ZONE();

    LOG("Total megabytes allocated (%lu)", (mem_usage / MEGABYTE_SIZE));

    return (mem_usage / MEGABYTE_SIZE);
}

INTERNAL_HIDDEN void _iso_alloc_profile() {
#if HEAP_PROFILER
    _allocation_count++;

    /* Don't run the profiler on every allocation */
    if(LIKELY((rand_uint64() % PROFILER_ODDS) != 1)) {
        return;
    }

    LOCK_ROOT();

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];
        uint32_t used = 0;

        if(zone == NULL) {
            break;
        }

        used = _iso_alloc_zone_leak_detector(zone, true);

        if(used > CHUNK_USAGE_THRESHOLD) {
            _zone_profiler_map[zone->chunk_size].count++;
        }

        _sampled_count++;
    }

    UNLOCK_ROOT();
#endif
}

INTERNAL_HIDDEN void _initialize_profiler() {
#if HEAP_PROFILER
    /* We don't need thread safety for this file descriptor
     * as long as we guarantee to never use it if the root
     * is not locked */
    if(getenv(PROFILER_ENV_STR) != NULL) {
        profiler_fd = open(getenv(PROFILER_ENV_STR), O_RDWR | O_CREAT, 0666);
    } else {
        profiler_fd = open(PROFILER_FILE_PATH, O_RDWR | O_CREAT, 0666);
    }

    if(profiler_fd == ERR) {
        LOG_AND_ABORT("Cannot open file descriptor for profiler.data");
    }
#endif
    return;
}
