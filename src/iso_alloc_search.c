/* iso_alloc_search.c - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

#if UAF_PTR_PAGE
/* Search all zones for the first 8-byte sequence equal to n and overwrite
 * it with the address of the PROT_NONE uaf_ptr_page so the next deref
 * faults at a known address. Sampled from the free path. */
INTERNAL_HIDDEN void *_iso_alloc_ptr_search(void *n) {
    uint8_t *search = NULL;
    uint8_t *end = NULL;
    const size_t zones_used = _root->zones_used;

#if MEMORY_TAGGING || (ARM_MTE == 1)
    /* It should be safe to clear these upper bits even
     * if the pointer wasn't returned by IsoAlloc. */
    n = (void *) ((uintptr_t) n & TAGGED_PTR_MASK);
#endif

#if USE_NEON
    /* Per-call invariants — n is fixed for the entire search, so broadcast
     * the two pre-filter bytes once instead of once per zone. */
    const uint8x16_t b0 = vdupq_n_u8((uint8_t) (uintptr_t) n);
    const uint8x16_t b1 = vdupq_n_u8((uint8_t) ((uintptr_t) n >> 8));
#endif

    for(int32_t i = 0; i < zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];

        search = UNMASK_USER_PTR(zone);
        end = search + ZONE_USER_SIZE;

#if USE_NEON
        /* A u64 at byte position k can match n only if bytes[k] == n[0]
         * AND bytes[k+1] == n[1]. AND the two shifted byte-equality
         * vectors before reducing — collapses false positives 256x and
         * keeps the filter useful when chunks are filled with POISON_BYTE
         * (which would defeat a single-byte filter when n[0] == 0xde).
         * Stop 23 bytes before end so the last candidate u64 read fits. */
        uint8_t *neon_end = end - 23;
        while(search <= neon_end) {
            uint8x16_t eq = vandq_u8(vceqq_u8(vld1q_u8(search), b0),
                                     vceqq_u8(vld1q_u8(search + 1), b1));
            if(LIKELY(vmaxvq_u8(eq) == 0)) {
                search += 16;
                continue;
            }
            uint8_t *win_end = search + 16;
            while(search < win_end) {
                if(UNLIKELY(*(uint64_t *) search == (uint64_t) n)) {
                    *(uint64_t *) search = (uint64_t) (_root->uaf_ptr_page);
                    return search;
                }
                search++;
            }
        }
#endif

        uint8_t *tail_end = end - sizeof(uint64_t);
        while(search <= tail_end) {
            if(UNLIKELY((uint64_t) * (uint64_t *) search == (uint64_t) n)) {
                *(uint64_t *) search = (uint64_t) (_root->uaf_ptr_page);
                return search;
            }
            search++;
        }
    }

    return NULL;
}
#endif

#if EXPERIMENTAL
/* These functions are all experimental and subject to change */

/* Search the stack for pointers into IsoAlloc zones. If
 * stack_start is NULL then this function starts searching
 * from the environment variables which should be mapped
 * just below the stack */
INTERNAL_HIDDEN void _iso_alloc_search_stack(uint8_t *stack_start) {
    if(stack_start == NULL) {
        stack_start = (uint8_t *) ENVIRON;

        if(stack_start == NULL) {
            return;
        }
    }

    /* The end of our stack is the address of this local */
    uint8_t *stack_end;
    stack_end = (uint8_t *) &stack_end;
    const uint64_t tps = UINT32_MAX;

    uint8_t *current = stack_start;
    uint64_t max_ptr = 0x800000000000;

    while(current > stack_end) {
        /* Iterating through zones is expensive so this quickly
         * decides on values that are unlikely to be pointers
         * into zone user pages */
        if(*(int64_t *) current <= tps || *(int64_t *) current >= max_ptr || (*(int64_t *) current & 0xffffff) == 0) {
            // LOG("Ignoring pointer start=%p end=%p stack_ptr=%p value=%lx", stack_start, stack_end, current, *(int64_t *)current);
            current--;
            continue;
        }

        uint64_t *p = (uint64_t *) *(int64_t *) current;
        iso_alloc_zone_t *zone = iso_find_zone_range(p);
        current--;

        if(zone != NULL) {
            UNMASK_ZONE_PTRS(zone);

            /* Ensure the pointer is properly aligned */
            if(UNLIKELY(IS_ALIGNED((uintptr_t) p) != 0)) {
                LOG_AND_ABORT("Chunk at 0x%p of zone[%d] is not %d byte aligned", p, zone->index, SZ_ALIGNMENT);
            }

            uint64_t chunk_offset = (uint64_t) (p - (uint64_t *) zone->user_pages_start);
            LOG("zone[%d] user_pages_start=%p value=%p %lu %d", zone->index, zone->user_pages_start, p, chunk_offset, zone->chunk_size);

            /* Ensure the pointer is a multiple of chunk size */
            if(UNLIKELY((chunk_offset % zone->chunk_size) != 0)) {
                LOG("Chunk at %p is not a multiple of zone[%d] chunk size %d. Off by %" PRIu64 " bits", p, zone->index, zone->chunk_size, (chunk_offset % zone->chunk_size));
                MASK_ZONE_PTRS(zone);
                continue;
            }

            size_t chunk_number = (chunk_offset / zone->chunk_size);
            bit_slot_t bit_slot = (chunk_number * BITS_PER_CHUNK);
            bit_slot_t dwords_to_bit_slot = (bit_slot / BITS_PER_QWORD);

            if(UNLIKELY((zone->bitmap_start + dwords_to_bit_slot) >= (zone->bitmap_start + zone->bitmap_size))) {
                LOG("Cannot calculate this chunks location in the bitmap %p", p);
                MASK_ZONE_PTRS(zone);
                continue;
            }

            int64_t which_bit = (bit_slot % BITS_PER_QWORD);
            bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
            bitmap_index_t b = bm[dwords_to_bit_slot];

            if(UNLIKELY((GET_BIT(b, which_bit)) == 0)) {
                LOG("Chunk at %p is in-use", p);
            } else {
                LOG("Chunk at %p is free", p);
            }

            MASK_ZONE_PTRS(zone);
        }

        zone = iso_find_zone_bitmap_range(p);

        if(zone != NULL) {
            LOG_AND_ABORT("Pointer to bitmap for zone[%d] found in stack @ %p", zone->index, p);
        }
    }
}
#endif
