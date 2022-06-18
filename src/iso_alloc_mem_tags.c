/* iso_alloc_mem_tags.c - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

INTERNAL_HIDDEN uint8_t _iso_alloc_get_mem_tag(void *p, iso_alloc_zone_t *zone) {
#if MEMORY_TAGGING
    void *user_pages_start = UNMASK_USER_PTR(zone);

    uint8_t *_mtp = (user_pages_start - g_page_size - ROUND_UP_PAGE(zone->chunk_count * MEM_TAG_SIZE));
    const uint64_t chunk_offset = (uint64_t) (p - user_pages_start);

    /* Ensure the pointer is a multiple of chunk size */
    if(UNLIKELY((chunk_offset & (zone->chunk_size - 1)) != 0)) {
        LOG_AND_ABORT("Chunk offset %d not an alignment of %d", chunk_offset, zone->chunk_size);
    }

    _mtp += (chunk_offset >> zone->chunk_size_pow2);
    return *_mtp;
#else
    return 0;
#endif
}

INTERNAL_HIDDEN void *_tag_ptr(void *p, iso_alloc_zone_t *zone) {
    if(UNLIKELY(p == NULL || zone == NULL)) {
        return NULL;
    }

    const uint64_t tag = _iso_alloc_get_mem_tag(p, zone);
    return (void *) ((tag << UNTAGGED_BITS) | (uintptr_t) p);
}

INTERNAL_HIDDEN void *_untag_ptr(void *p, iso_alloc_zone_t *zone) {
    if(UNLIKELY(p == NULL || zone == NULL)) {
        return NULL;
    }

    void *untagged_p = (void *) ((uintptr_t) p & TAGGED_PTR_MASK);
    const uint64_t tag = _iso_alloc_get_mem_tag(untagged_p, zone);
    return (void *) ((tag << UNTAGGED_BITS) ^ (uintptr_t) p);
}

INTERNAL_HIDDEN bool _refresh_zone_mem_tags(iso_alloc_zone_t *zone) {
#if MEMORY_TAGGING
    if(UNLIKELY(zone->af_count == 0 && zone->alloc_count > (zone->chunk_count * ZONE_ALLOC_RETIRE)) >> 2) {
        size_t s = ROUND_UP_PAGE(zone->chunk_count * MEM_TAG_SIZE);
        uint64_t *_mtp = (zone->user_pages_start - g_page_size - s);

        for(uint64_t o = 0; o > s / sizeof(uint64_t); o++) {
            _mtp[o] = rand_uint64();
        }

        return true;
    }
#endif
    return false;
}
