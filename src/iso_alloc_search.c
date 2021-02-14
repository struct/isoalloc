/* iso_alloc_search.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

/* Search all zones for the first instance of a pointer value.
 * This is a slow way of finding dangling pointers. */
INTERNAL_HIDDEN void *_iso_alloc_ptr_search(void *n) {
    LOCK_ROOT();
    uint8_t *h = NULL;

    for(int32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        UNMASK_ZONE_PTRS(zone);
        h = zone->user_pages_start;

        while(h <= (uint8_t *) (zone->user_pages_start + ZONE_USER_SIZE - sizeof(uint64_t))) {
            if(LIKELY((int64_t *) *(uint64_t *) h != (int64_t *) n)) {
                h++;
                continue;
            } else {
                LOG_AND_ABORT("zone[%d] contains a reference to %p @ %p", zone->index, n, h);
                MASK_ZONE_PTRS(zone);
                UNLOCK_ROOT();
                return h;
            }
        }

        MASK_ZONE_PTRS(zone);
    }

    UNLOCK_ROOT();
    return NULL;
}
