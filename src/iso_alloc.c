/* iso_alloc.c - A secure memory allocator
 * Copyright 2021 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

#if THREAD_SUPPORT
atomic_flag root_busy_flag;
atomic_flag big_zone_busy_flag;
#endif

uint32_t g_page_size;
uint32_t _default_zone_count;
iso_alloc_root *_root;

/* Select a random number of chunks to be canaries. These
 * can be verified anytime by calling check_canary()
 * or check_canary_no_abort() */
INTERNAL_HIDDEN void create_canary_chunks(iso_alloc_zone *zone) {
#if ENABLE_ASAN || DISABLE_CANARY
    return;
#else
    /* Canary chunks are only for default zone sizes. This
     * is because larger zones would waste a lot of memory
     * if we set aside some of their chunks as canaries */
    if(zone->chunk_size > MAX_DEFAULT_ZONE_SZ) {
        return;
    }

    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    bitmap_index_t max_bitmap_idx = GET_MAX_BITMASK_INDEX(zone) - 1;
    uint64_t chunk_count = GET_CHUNK_COUNT(zone);
    bit_slot_t bit_slot;

    /* Roughly %1 of the chunks in this zone will become a canary */
    uint64_t canary_count = (chunk_count / CANARY_COUNT_DIV);

    /* This function is only ever called during zone
     * initialization so we don't need to check the
     * current state of any chunks, they're all free.
     * It's possible the call to rand_uint64() here will
     * return the same index twice, we can live with
     * that collision as canary chunks only provide a
     * small security property anyway */
    for(uint64_t i = 0; i < canary_count; i++) {
        bitmap_index_t bm_idx = ALIGN_SZ_DOWN((rand_uint64() & (max_bitmap_idx)));

        if(0 > bm_idx) {
            bm_idx = 0;
        }

        /* Set the 1st and 2nd bits as 1 */
        SET_BIT(bm[bm_idx], 0);
        SET_BIT(bm[bm_idx], 1);
        bit_slot = (bm_idx << BITS_PER_QWORD_SHIFT);
        void *p = POINTER_FROM_BITSLOT(zone, bit_slot);
        write_canary(zone, p);
    }
#endif
}

#if ENABLE_ASAN
/* Verify the integrity of all canary chunks and the
 * canary written to all free chunks. This function
 * either aborts or returns nothing */
INTERNAL_HIDDEN void verify_all_zones(void) {
    return;
}

INTERNAL_HIDDEN void verify_zone(iso_alloc_zone *zone) {
    return;
}

INTERNAL_HIDDEN void _verify_all_zones(void) {
    return;
}

INTERNAL_HIDDEN void _verify_zone(iso_alloc_zone *zone) {
    return;
}
#else

/* Verify the integrity of all canary chunks and the
 * canary written to all free chunks. This function
 * either aborts or returns nothing */
INTERNAL_HIDDEN void verify_all_zones(void) {
    LOCK_ROOT();
    _verify_all_zones();
    UNLOCK_ROOT();
    return;
}

INTERNAL_HIDDEN void verify_zone(iso_alloc_zone *zone) {
    LOCK_ROOT();
    _verify_zone(zone);
    UNLOCK_ROOT();
    return;
}

INTERNAL_HIDDEN void _verify_all_zones(void) {
    for(int32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        if(zone->bitmap_start == NULL || zone->user_pages_start == NULL) {
            break;
        }

        _verify_zone(zone);
    }

    /* No need to lock big zone here since the
     * root should be locked by our caller */
    iso_alloc_big_zone *big = _root->big_zone_head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big != NULL) {
        check_big_canary(big);

        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            break;
        }
    }
}

INTERNAL_HIDDEN void _verify_zone(iso_alloc_zone *zone) {
    UNMASK_ZONE_PTRS(zone);
    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    bitmap_index_t max_bm_idx = GET_MAX_BITMASK_INDEX(zone);
    bit_slot_t bit_slot;
    int64_t bit;

    for(bitmap_index_t i = 0; i < max_bm_idx; i++) {
        for(int64_t j = 1; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            bit = GET_BIT(bm[i], j);

            /* If this bit is set it is either a free chunk or
             * a canary chunk. Either way it should have a set
             * of canaries we can verify */
            if(bit == 1) {
                bit_slot = (i << BITS_PER_QWORD_SHIFT) + j;
                void *p = POINTER_FROM_BITSLOT(zone, bit_slot);
                check_canary(zone, p);
            }
        }
    }

    MASK_ZONE_PTRS(zone);
    return;
}
#endif

/* Pick a random index in the bitmap and start looking
 * for free bit slots we can add to the cache. The random
 * bitmap index is to protect against biasing the free
 * slot cache with only chunks towards the start of the
 * user mapping. Theres no guarantee this function will
 * find any free slots. */
INTERNAL_HIDDEN INLINE void fill_free_bit_slot_cache(iso_alloc_zone *zone) {
    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    bitmap_index_t max_bitmap_idx = GET_MAX_BITMASK_INDEX(zone);
    bit_slot_t bit_slot;

    /* This gives us an arbitrary spot in the bitmap to
     * start searching but may mean we end up with a smaller
     * cache. This may negatively affect performance but
     * leads to a less predictable free list */
    bitmap_index_t bm_idx = ALIGN_SZ_DOWN((rand_uint64() & (max_bitmap_idx - 1)));

    if(0 > bm_idx) {
        bm_idx = 0;
    }

    memset(zone->free_bit_slot_cache, BAD_BIT_SLOT, sizeof(zone->free_bit_slot_cache));
    zone->free_bit_slot_cache_usable = 0;
    uint8_t free_bit_slot_cache_index;

    for(free_bit_slot_cache_index = 0; free_bit_slot_cache_index < BIT_SLOT_CACHE_SZ; bm_idx++) {
        /* Don't index outside of the bitmap or
         * we will return inaccurate bit slots */
        if(bm_idx >= max_bitmap_idx) {
            zone->free_bit_slot_cache_index = free_bit_slot_cache_index;
            return;
        }

        for(int64_t j = 0; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            if(free_bit_slot_cache_index >= BIT_SLOT_CACHE_SZ) {
                zone->free_bit_slot_cache_index = free_bit_slot_cache_index;
                return;
            }

            if((GET_BIT(bm[bm_idx], j)) == 0) {
                bit_slot = (bm_idx << BITS_PER_QWORD_SHIFT) + j;
                zone->free_bit_slot_cache[free_bit_slot_cache_index] = bit_slot;
                free_bit_slot_cache_index++;
            }
        }
    }

    zone->free_bit_slot_cache_index = free_bit_slot_cache_index;
}

INTERNAL_HIDDEN INLINE void insert_free_bit_slot(iso_alloc_zone *zone, int64_t bit_slot) {
    if(0 > zone->free_bit_slot_cache_usable || 0 > zone->free_bit_slot_cache_index) {
        LOG_AND_ABORT("Zone[%d] contains a corrupt cache index", zone->index);
    }

#if VERIFY_BIT_SLOT_CACHE
    /* The cache is sorted at creation time but once we start
     * free'ing chunks we add bit_slots to it in an unpredictable
     * order. So we can't search the cache with something like
     * a binary search. This brute force search shouldn't incur
     * too much of a performance penalty as we only search starting
     * at the free_bit_slot_cache_usable index which is updated
     * everytime we call get_next_free_bit_slot(). We do this in
     * order to detect any corruption of the cache that attempts
     * to add duplicate bit_slots which would result in iso_alloc()
     * handing out in-use chunks. The _iso_alloc() path also does
     * a check on the bitmap itself before handing out any chunks */
    int32_t max_cache_slots = sizeof(zone->free_bit_slot_cache) >> 3;

    for(int32_t i = zone->free_bit_slot_cache_usable; i < max_cache_slots; i++) {
        if(zone->free_bit_slot_cache[i] == bit_slot) {
            LOG_AND_ABORT("Zone[%d] already contains bit slot %lu in cache", zone->index, bit_slot);
        }
    }
#endif

    if(zone->free_bit_slot_cache_index >= BIT_SLOT_CACHE_SZ) {
        return;
    }

    zone->free_bit_slot_cache[zone->free_bit_slot_cache_index] = bit_slot;
    zone->free_bit_slot_cache_index++;
}

INTERNAL_HIDDEN bit_slot_t get_next_free_bit_slot(iso_alloc_zone *zone) {
    if(0 > zone->free_bit_slot_cache_usable || zone->free_bit_slot_cache_usable >= BIT_SLOT_CACHE_SZ ||
       zone->free_bit_slot_cache_usable > zone->free_bit_slot_cache_index) {
        return BAD_BIT_SLOT;
    }

    zone->next_free_bit_slot = zone->free_bit_slot_cache[zone->free_bit_slot_cache_usable];
    zone->free_bit_slot_cache[zone->free_bit_slot_cache_usable++] = BAD_BIT_SLOT;
    return zone->next_free_bit_slot;
}

INTERNAL_HIDDEN INLINE void iso_clear_user_chunk(uint8_t *p, size_t size) {
    memset(p, POISON_BYTE, size);
}

INTERNAL_HIDDEN void *create_guard_page(void *p) {
    if(p == NULL) {
        p = mmap_rw_pages(g_page_size, false, GUARD_PAGE_NAME);

        if(p == NULL) {
            LOG_AND_ABORT("Could not allocate guard page");
        }
    }

    /* Use g_page_size here because we could be
     * calling this while we setup the root */
    mprotect_pages(p, g_page_size, PROT_NONE);
    madvise(p, g_page_size, MADV_DONTNEED);
    return p;
}

INTERNAL_HIDDEN void *mmap_rw_pages(size_t size, bool populate, const char *name) {
#if !ENABLE_ASAN
    /* Produce a random page address as a hint for mmap */
    uint64_t hint = ROUND_DOWN_PAGE(rand_uint64());
    hint &= 0x3FFFFFFFF000;
    void *p = (void *) hint;
#else
    void *p = NULL;
#endif

    size = ROUND_UP_PAGE(size);

    /* Only Linux supports MAP_POPULATE */
#if __linux__ && PRE_POPULATE_PAGES
    if(populate == true) {
        p = mmap(p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    } else {
        p = mmap(p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
#else
    p = mmap(p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

    if(p == MAP_FAILED) {
        LOG_AND_ABORT("Failed to mmap rw pages");
        return NULL;
    }

    if(name != NULL) {
        name_mapping(p, size, name);
    }

    return p;
}

INTERNAL_HIDDEN void mprotect_pages(void *p, size_t size, int32_t protection) {
    size = ROUND_UP_PAGE(size);

    if((mprotect(p, size, protection)) == ERR) {
        LOG_AND_ABORT("Failed to mprotect pages @ 0x%p", p);
    }
}

INTERNAL_HIDDEN iso_alloc_root *iso_alloc_new_root(void) {
    void *p = NULL;
    iso_alloc_root *r;

    size_t _root_size = sizeof(iso_alloc_root) + (g_page_size << 1);

    p = (void *) mmap_rw_pages(_root_size, true, ROOT_NAME);

    if(p == NULL) {
        LOG_AND_ABORT("Cannot allocate pages for root");
    }

    r = (iso_alloc_root *) (p + g_page_size);
    r->system_page_size = g_page_size;
    r->guard_below = p;
    create_guard_page(r->guard_below);

    r->guard_above = (void *) ROUND_UP_PAGE((uintptr_t) (p + sizeof(iso_alloc_root) + r->system_page_size));
    create_guard_page(r->guard_above);
    return r;
}

INTERNAL_HIDDEN void iso_alloc_initialize_global_root(void) {
    /* Do not allow a reinitialization unless root is NULL */
    if(_root != NULL) {
        return;
    }

    _root = iso_alloc_new_root();

    if(_root == NULL) {
        LOG_AND_ABORT("Could not initialize global root");
    }

    _default_zone_count = sizeof(default_zones) >> 3;

    _root->zones_size = (MAX_ZONES * sizeof(iso_alloc_zone));
    _root->zones_size += (g_page_size * 2);
    _root->zones_size = ROUND_UP_PAGE(_root->zones_size);

    /* Allocate memory with guard pages to hold zone data */
    void *p = mmap_rw_pages(_root->zones_size, false, NULL);
    create_guard_page(p);
    create_guard_page((void *) (uintptr_t) (p + _root->zones_size) - g_page_size);

    _root->zones = (void *) (p + g_page_size);
    name_mapping(p, _root->zones_size, "isoalloc zone metadata");

    for(int64_t i = 0; i < _default_zone_count; i++) {
        if((_iso_new_zone(default_zones[i], true)) == NULL) {
            LOG_AND_ABORT("Failed to create a new zone");
        }
    }

    /* This call to mlock may fail if memory limits
     * are set too low. This will not affect us
     * at runtime. It just means some of the default
     * root meta data may get swapped to disk */
    mlock(&_root, sizeof(iso_alloc_root));

    _root->zone_handle_mask = rand_uint64();
    _root->big_zone_next_mask = rand_uint64();
    _root->big_zone_canary_secret = rand_uint64();
}

__attribute__((constructor(FIRST_CTOR))) void iso_alloc_ctor(void) {
    g_page_size = sysconf(_SC_PAGESIZE);
    iso_alloc_initialize_global_root();
    _initialize_profiler();

#if ALLOC_SANITY && UNINIT_READ_SANITY
    _iso_alloc_setup_userfaultfd();
#endif

#if ALLOC_SANITY
    _sanity_canary = rand_uint64();
#endif
}

INTERNAL_HIDDEN INLINE void flush_thread_zone_cache() {
#if THREAD_SUPPORT && THREAD_ZONE_CACHE
    /* The thread zone cache needs to be invalidated */
    memset(thread_zone_cache, 0x0, sizeof(thread_zone_cache));
    thread_zone_cache_count = 0;
#endif
}

INTERNAL_HIDDEN void _unmap_zone(iso_alloc_zone *zone) {
    munmap(zone->bitmap_start, zone->bitmap_size);
    munmap(zone->bitmap_start - _root->system_page_size, _root->system_page_size);
    munmap(zone->bitmap_start + zone->bitmap_size, _root->system_page_size);
    munmap(zone->user_pages_start, ZONE_USER_SIZE);
    munmap(zone->user_pages_start - _root->system_page_size, _root->system_page_size);
    munmap(zone->user_pages_start + ZONE_USER_SIZE, _root->system_page_size);
}

INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone *zone) {
    LOCK_ROOT();
    UNMASK_ZONE_PTRS(zone);
    UNPOISON_ZONE(zone);

    if(zone->internally_managed == false) {
#if NEVER_REUSE_ZONES || FUZZ_MODE
        _unmap_zone(zone);
        zone->user_pages_start = NULL;
        zone->bitmap_start = NULL;

        /* Mark the zone as full so no attempts are made to use it */
        zone->is_full = true;
        flush_thread_zone_cache();
#else
        /* This zone can be used again, we just need to wipe
         * any sensitive data from it and prime it for use */
        memset(zone->bitmap_start, 0x0, zone->bitmap_size);
        memset(zone->user_pages_start, 0x0, ZONE_USER_SIZE);

        /* Take over the zone to be used internally */
        zone->internally_managed = true;
        zone->is_full = false;

        /* Reusing custom zones has the potential for introducing
         * zone-use-after-free patterns. So we bootstrap the zone
         * from scratch here */
        create_canary_chunks(zone);

        fill_free_bit_slot_cache(zone);

        /* Prime the next_free_bit_slot member */
        get_next_free_bit_slot(zone);

        MASK_ZONE_PTRS(zone);
#endif
        /* If we are destroying the zone lets give the memory
         * back to the OS. It will still be available if we
         * try to use it */
        madvise(zone->bitmap_start, zone->bitmap_size, MADV_DONTNEED);
        madvise(zone->user_pages_start, ZONE_USER_SIZE, MADV_DONTNEED);
        POISON_ZONE(zone);
        UNLOCK_ROOT();
        return;
    } else {
        /* The only time we ever destroy a default non-custom zone
         * is from the destructor so its safe unmap pages */
        _unmap_zone(zone);
        flush_thread_zone_cache();
        UNLOCK_ROOT();
    }
}

__attribute__((destructor(LAST_DTOR))) void iso_alloc_dtor(void) {
    LOCK_ROOT();

#if HEAP_PROFILER
    _iso_alloc_printf(profiler_fd, "allocated=%d\n", _allocation_count);
    _iso_alloc_printf(profiler_fd, "sampled=%d\n", _sampled_count);

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];
        _zone_profiler_map[zone->chunk_size].total++;
    }

    for(uint32_t i = 0; i < HG_SIZE; i++) {
        if(caller_hg[i] != 0) {
            _iso_alloc_printf(profiler_fd, "backtrace_hash=0x%x,calls=%d\n", i, caller_hg[i]);
        }
    }

    for(uint32_t i = 0; i < SMALL_SZ_MAX; i++) {
        if(_zone_profiler_map[i].count != 0) {
            _iso_alloc_printf(profiler_fd, "%d,%d,%d\n", i, _zone_profiler_map[i].total, _zone_profiler_map[i].count);
        }
    }

    if(profiler_fd != ERR) {
        close(profiler_fd);
        profiler_fd = ERR;
    }
#endif

#if DEBUG && (LEAK_DETECTOR || MEM_USAGE)
    uint64_t mb = 0;

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];
        _iso_alloc_zone_leak_detector(zone, false);
    }

    mb = __iso_alloc_mem_usage();

#if MEM_USAGE
    LOG("Total megabytes consumed by all zones: %lu", mb);
    _iso_alloc_print_stats();
#endif

#endif

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];
        _verify_zone(zone);
#if ISO_DTOR_CLEANUP
        _iso_alloc_destroy_zone(zone);
#endif
    }

#if ISO_DTOR_CLEANUP
    /* Unmap all zone structures */
    munmap((void *) ((uintptr_t) _root->zones - g_page_size), _root->zones_size);
#endif

    iso_alloc_big_zone *big_zone = _root->big_zone_head;
    iso_alloc_big_zone *big = NULL;

    if(big_zone != NULL) {
        big_zone = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big_zone != NULL) {
        check_big_canary(big_zone);

        if(big_zone->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big_zone->next);
        } else {
            big = NULL;
        }

#if ISO_DTOR_CLEANUP
        /* Free the user pages first */
        void *up = big_zone->user_pages_start - _root->system_page_size;
        munmap(up, (_root->system_page_size << 1) + big_zone->size);

        /* Free the meta data */
        munmap(big_zone - _root->system_page_size, (_root->system_page_size * BIG_ZONE_META_DATA_PAGE_COUNT));
#endif
        big_zone = big;
    }

#if ISO_DTOR_CLEANUP
    munmap(_root->guard_below, _root->system_page_size);
    munmap(_root->guard_above, _root->system_page_size);
    munmap(_root, sizeof(iso_alloc_root));
#endif

    UNLOCK_ROOT();
}

INTERNAL_HIDDEN int32_t name_mapping(void *p, size_t sz, const char *name) {
#if NAMED_MAPPINGS && __ANDROID__
    return prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, p, sz, name);
#endif
    return 0;
}

INTERNAL_HIDDEN iso_alloc_zone *iso_new_zone(size_t size, bool internal) {
    LOCK_ROOT();
    iso_alloc_zone *zone = _iso_new_zone(size, internal);
    UNLOCK_ROOT();
    return zone;
}

INTERNAL_HIDDEN iso_alloc_zone *_iso_new_zone(size_t size, bool internal) {
    if(_root->zones_used >= MAX_ZONES) {
        LOG_AND_ABORT("Cannot allocate additional zones");
    }

    if(size > SMALL_SZ_MAX) {
        LOG("Request for chunk of %ld bytes should be handled by big alloc path", size);
        return NULL;
    }

    /* Chunk size must be aligned */
    if(IS_ALIGNED(size) != 0) {
        size = ALIGN_SZ_UP(size);
    }

    /* Minimum chunk size */
    if(size < SMALLEST_CHUNK_SZ) {
        size = SMALLEST_CHUNK_SZ;
    }

    iso_alloc_zone *new_zone = &_root->zones[_root->zones_used];

    new_zone->internally_managed = internal;
    new_zone->is_full = false;
    new_zone->chunk_size = size;

    /* If a caller requests an allocation that is >=(ZONE_USER_SIZE/2)
     * then we need to allocate a minimum size bitmap */
    uint32_t bitmap_size = (GET_CHUNK_COUNT(new_zone) << BITS_PER_CHUNK_SHIFT) >> BITS_PER_BYTE_SHIFT;
    new_zone->bitmap_size = (bitmap_size > sizeof(bitmap_index_t)) ? bitmap_size : sizeof(bitmap_index_t);

    /* All of the following fields are immutable
     * and should not change once they are set */
    void *p = mmap_rw_pages(new_zone->bitmap_size + (_root->system_page_size << 1), true, ZONE_BITMAP_NAME);

    void *bitmap_pages_guard_below = p;
    new_zone->bitmap_start = (p + _root->system_page_size);

    void *bitmap_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + (new_zone->bitmap_size + _root->system_page_size));

    create_guard_page(bitmap_pages_guard_below);
    create_guard_page(bitmap_pages_guard_above);

    /* Bitmap pages are accessed often and usually in sequential order */
    madvise(new_zone->bitmap_start, new_zone->bitmap_size, MADV_WILLNEED);
    madvise(new_zone->bitmap_start, new_zone->bitmap_size, MADV_SEQUENTIAL);

    char *name;

    if(internal == true) {
        name = INTERNAL_UZ_NAME;
    } else {
        name = CUSTOM_UZ_NAME;
    }

    /* All user pages use MAP_POPULATE. This might seem like we are asking
     * the kernel to commit a lot of memory for us that we may never use
     * but when we call create_canary_chunks() that will happen anyway */
    p = mmap_rw_pages(ZONE_USER_SIZE + (_root->system_page_size << 1), true, name);

    void *user_pages_guard_below = p;
    new_zone->user_pages_start = (p + _root->system_page_size);
    void *user_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + (ZONE_USER_SIZE + _root->system_page_size));

    create_guard_page(user_pages_guard_below);
    create_guard_page(user_pages_guard_above);

    /* User pages will be accessed in an unpredictable order */
    madvise(new_zone->user_pages_start, ZONE_USER_SIZE, MADV_WILLNEED);
    madvise(new_zone->user_pages_start, ZONE_USER_SIZE, MADV_RANDOM);

    new_zone->index = _root->zones_used;
    new_zone->canary_secret = rand_uint64();
    new_zone->pointer_mask = rand_uint64();

    create_canary_chunks(new_zone);

    /* When we create a new zone its an opportunity to
     * populate our free list cache with random entries */
    fill_free_bit_slot_cache(new_zone);

    /* Prime the next_free_bit_slot member */
    get_next_free_bit_slot(new_zone);

#if CPU_PIN
    new_zone->cpu_core = sched_getcpu();
#endif

    POISON_ZONE(new_zone);
    MASK_ZONE_PTRS(new_zone);

    _root->zones_used++;

    return new_zone;
}

/* Iterate through a zone bitmap a dword at a time
 * looking for empty holes (i.e. slot == 0) */
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot(iso_alloc_zone *zone) {
    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    bit_slot_t bit_slot = BAD_BIT_SLOT;
    bitmap_index_t max_bm_idx = GET_MAX_BITMASK_INDEX(zone);

    /* Iterate the entire bitmap a dword at a time */
    for(bitmap_index_t i = 0; i < max_bm_idx; i++) {
        /* If the byte is 0 then there are some free
         * slots we can use at this location */
        if(bm[i] == 0x0) {
            bit_slot = (i << BITS_PER_QWORD_SHIFT);
            return bit_slot;
        }
    }

    return bit_slot;
}

/* This function scans an entire bitmap bit-by-bit
 * and returns the first free bit position. In a heavily
 * used zone this function will be slow to search */
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot_slow(iso_alloc_zone *zone) {
    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    bit_slot_t bit_slot = BAD_BIT_SLOT;
    bitmap_index_t max_bm_idx = GET_MAX_BITMASK_INDEX(zone);
    int64_t bit;

    for(bitmap_index_t i = 0; i < max_bm_idx; i++) {
        for(int64_t j = 0; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            bit = GET_BIT(bm[i], j);

            if(bit == 0) {
                bit_slot = (i << BITS_PER_QWORD_SHIFT) + j;
                return bit_slot;
            }
        }
    }

    return bit_slot;
}

INTERNAL_HIDDEN iso_alloc_zone *is_zone_usable(iso_alloc_zone *zone, size_t size) {
    /* This zone may fit this chunk but if the zone was
     * created for chunks more than N* larger than the
     * requested allocation size then we would be wasting
     * a lot of memory by using it. We only do this for
     * sizes beyond ZONE_1024 bytes. In other words we can
     * live with some wasted space in zones that manage
     * chunks smaller than ZONE_1024 */
    if(zone->internally_managed == true && size > ZONE_1024 && zone->chunk_size >= (size << WASTED_SZ_MULTIPLIER_SHIFT)) {
        return NULL;
    }

    if(zone->next_free_bit_slot != BAD_BIT_SLOT) {
        return zone;
    }

    UNMASK_ZONE_PTRS(zone);

    /* If the cache for this zone is empty we should
     * refill it to make future allocations faster 
     * for all threads */
    if(zone->free_bit_slot_cache_usable == zone->free_bit_slot_cache_index) {
        fill_free_bit_slot_cache(zone);
    }

    bit_slot_t bit_slot = get_next_free_bit_slot(zone);

    if(LIKELY(bit_slot != BAD_BIT_SLOT)) {
        MASK_ZONE_PTRS(zone);
        return zone;
    }

    /* Free list failed, use a fast search */
    bit_slot = iso_scan_zone_free_slot(zone);

    if(UNLIKELY(bit_slot == BAD_BIT_SLOT)) {
        /* Fast search failed, search bit by bit */
        bit_slot = iso_scan_zone_free_slot_slow(zone);
        MASK_ZONE_PTRS(zone);

        /* This zone may be entirely full, try the next one
         * but mark this zone full so future allocations can
         * take a faster path */
        if(bit_slot == BAD_BIT_SLOT) {
            zone->is_full = true;
            return NULL;
        } else {
            zone->next_free_bit_slot = bit_slot;
            return zone;
        }
    } else {
        zone->next_free_bit_slot = bit_slot;
        MASK_ZONE_PTRS(zone);
        return zone;
    }
}

/* Implements the check for iso_find_zone_fit */
INTERNAL_HIDDEN bool iso_does_zone_fit(iso_alloc_zone *zone, size_t size) {
#if CPU_PIN
    if(zone->cpu_core != sched_getcpu()) {
        return false;
    }
#endif

    /* Don't return a zone that handles a size far larger
     * than we need. This could lead to high memory usage
     * depending on allocation patterns but helps enforce
     * spatial separation based on sized */
    if(zone->chunk_size >= ZONE_1024 && size <= ZONE_128) {
        return false;
    }

    if(zone->chunk_size < size || zone->internally_managed == false || zone->is_full == true) {
        return false;
    }

    /* We found a zone, lets try to find a free slot in it */
    zone = is_zone_usable(zone, size);

    if(zone == NULL) {
        return false;
    } else {
        return true;
    }
}

/* Finds a zone that can fit this allocation request */
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_fit(size_t size) {
    iso_alloc_zone *zone = NULL;
    int32_t i = 0;

#if !SMALL_MEM_STARTUP
    /* A simple optimization to find which default zone
     * should fit this allocation. If we fail then a
     * slower iterative approach is used. The longer a
     * program runs the more likely we will fail this
     * fast path as default zones may fill up */
    if(size >= ZONE_512 && size <= ZONE_8192) {
        i = _default_zone_count >> 1;
    } else if(size > ZONE_8192) {
        i = _default_zone_count;
    }
#endif

    for(; i < _root->zones_used; i++) {
        zone = &_root->zones[i];

        bool fits = iso_does_zone_fit(zone, size);

        if(fits == true) {
            return zone;
        }
    }

    return NULL;
}

INTERNAL_HIDDEN void *_iso_calloc(size_t nmemb, size_t size) {
    if(nmemb > (nmemb * size)) {
        LOG_AND_ABORT("Call to calloc() will overflow nmemb=%zu size=%zu", nmemb, size);
        return NULL;
    }

    void *p = _iso_alloc(NULL, nmemb * size);

    memset(p, 0x0, nmemb * size);
    return p;
}

INTERNAL_HIDDEN void *_iso_big_alloc(size_t size) {
    size_t new_size = ROUND_UP_PAGE(size);

    if(new_size < size || new_size > BIG_SZ_MAX) {
        LOG_AND_ABORT("Cannot allocate a big zone of %ld bytes", new_size);
    }

    size = new_size;

    LOCK_BIG_ZONE();

    /* Let's first see if theres an existing set of
     * pages that can satisfy this allocation request */
    iso_alloc_big_zone *big = _root->big_zone_head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    iso_alloc_big_zone *last_big = NULL;

    while(big != NULL) {
        check_big_canary(big);

        if(big->free == true && big->size >= size) {
            break;
        }

        last_big = big;

        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
            break;
        }
    }

    /* We need to setup a new set of pages */
    if(big == NULL) {
        /* User data is allocated separately from big zone meta
         * data to prevent an attacker from targeting it */
        void *user_pages = mmap_rw_pages((_root->system_page_size << BIG_ZONE_USER_PAGE_COUNT_SHIFT) + size, false, BIG_ZONE_UD_NAME);

        if(user_pages == NULL) {
            UNLOCK_BIG_ZONE();
            return NULL;
        }

        void *p = mmap_rw_pages((_root->system_page_size * BIG_ZONE_META_DATA_PAGE_COUNT), false, BIG_ZONE_MD_NAME);

        /* The first page before meta data is a guard page */
        create_guard_page(p);

        /* The second page is for meta data and it is placed
         * at a random offset from the start of the page */
        big = (iso_alloc_big_zone *) (p + _root->system_page_size);
        madvise(big, _root->system_page_size, MADV_WILLNEED);
        uint32_t random_offset = ALIGN_SZ_DOWN(rand_uint64());
        big = (iso_alloc_big_zone *) ((p + _root->system_page_size) + (random_offset % (_root->system_page_size - sizeof(iso_alloc_big_zone))));
        big->free = false;
        big->size = size;
        big->next = NULL;

        if(last_big != NULL) {
            last_big->next = MASK_BIG_ZONE_NEXT(big);
        }

        if(_root->big_zone_head == NULL) {
            _root->big_zone_head = MASK_BIG_ZONE_NEXT(big);
        }

        /* Create the guard page after the meta data */
        void *next_gp = (p + (_root->system_page_size << 1));
        create_guard_page(next_gp);

        /* The first page is a guard page */
        create_guard_page(user_pages);

        /* Tell the kernel we want to access this big zone allocation */
        user_pages += _root->system_page_size;
        madvise(user_pages, size, MADV_WILLNEED);
        madvise(user_pages, size, MADV_RANDOM);

        /* The last page beyond user data is a guard page */
        void *last_gp = (user_pages + size);
        create_guard_page(last_gp);

        /* Save a pointer to the user pages */
        big->user_pages_start = user_pages;

        /* The canaries prevents a linear overwrite of the big
         * zone meta data structure from either direction */
        big->canary_a = ((uint64_t) big ^ bswap_64((uint64_t) big->user_pages_start) ^ _root->big_zone_canary_secret);
        big->canary_b = big->canary_a;

        UNLOCK_BIG_ZONE();
        return big->user_pages_start;
    } else {
        check_big_canary(big);
        big->free = false;
        UNPOISON_BIG_ZONE(big);
        UNLOCK_BIG_ZONE();
        return big->user_pages_start;
    }
}

INTERNAL_HIDDEN void *_iso_alloc_bitslot_from_zone(bit_slot_t bitslot, iso_alloc_zone *zone) {
    bitmap_index_t dwords_to_bit_slot = (bitslot >> BITS_PER_QWORD_SHIFT);
    int64_t which_bit = WHICH_BIT(bitslot);

    void *p = POINTER_FROM_BITSLOT(zone, bitslot);
    UNPOISON_ZONE_CHUNK(zone, p);

    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;

    /* Read out 64 bits from the bitmap. We will write
     * them back before we return. This reduces the
     * number of times we have to hit the bitmap page
     * which could result in a page fault */
    bitmap_index_t b = bm[dwords_to_bit_slot];

    if(UNLIKELY(p > zone->user_pages_start + ZONE_USER_SIZE)) {
        LOG_AND_ABORT("Allocating an address 0x%p from zone[%d], bit slot %lu %ld bytes %ld pages outside zones user pages 0x%p 0x%p",
                      p, zone->index, bitslot, p - (zone->user_pages_start + ZONE_USER_SIZE), (p - (zone->user_pages_start + ZONE_USER_SIZE)) / _root->system_page_size,
                      zone->user_pages_start, zone->user_pages_start + ZONE_USER_SIZE);
    }

    if(UNLIKELY((GET_BIT(b, which_bit)) != 0)) {
        LOG_AND_ABORT("Zone[%d] for chunk size %d cannot return allocated chunk at 0x%p bitmap location @ 0x%p. bit slot was %lu, bit number was %" PRIu64,
                      zone->index, zone->chunk_size, p, &bm[dwords_to_bit_slot], bitslot, which_bit);
    }

    /* This chunk was either previously allocated and free'd
     * or it's a canary chunk. In either case this means it
     * has a canary written in its first dword. Here we check
     * that canary and abort if its been corrupted */
#if !ENABLE_ASAN && !DISABLE_CANARY
    if((GET_BIT(b, (which_bit + 1))) == 1) {
        check_canary(zone, p);
        memset(p, 0x0, CANARY_SIZE);
    }
#endif

    /* Set the in-use bit */
    SET_BIT(b, which_bit);

    /* The second bit is flipped to 0 while in use. This
     * is because a previously in use chunk would have
     * a bit pattern of 11 which makes it looks the same
     * as a canary chunk. This bit is set again upon free */
    UNSET_BIT(b, (which_bit + 1));
    bm[dwords_to_bit_slot] = b;
    return p;
}

INTERNAL_HIDDEN INLINE size_t next_pow2(size_t sz) {
    sz |= sz >> 1;
    sz |= sz >> 2;
    sz |= sz >> 4;
    sz |= sz >> 8;
    sz |= sz >> 16;
    sz |= sz >> 32;
    return sz + 1;
}

INTERNAL_HIDDEN INLINE void populate_thread_zone_cache(iso_alloc_zone *zone) {
#if THREAD_SUPPORT && THREAD_ZONE_CACHE
    if(thread_zone_cache_count < THREAD_ZONE_CACHE_SZ) {
        thread_zone_cache[thread_zone_cache_count].zone = zone;
        thread_zone_cache[thread_zone_cache_count].chunk_size = zone->chunk_size;
        thread_zone_cache_count++;
    } else {
        thread_zone_cache_count = 0;
        thread_zone_cache[thread_zone_cache_count].zone = zone;
        thread_zone_cache[thread_zone_cache_count].chunk_size = zone->chunk_size;
    }
#endif
}

INTERNAL_HIDDEN void *_iso_alloc(iso_alloc_zone *zone, size_t size) {
    LOCK_ROOT();

    if(UNLIKELY(_root == NULL)) {
        g_page_size = sysconf(_SC_PAGESIZE);
        iso_alloc_initialize_global_root();
    }

#if ALLOC_SANITY
    /* We only sample allocations smaller than an individual
     * page. We are unlikely to find uninitialized reads on
     * larger size and it makes tracking them less complex */
    size_t sampled_size = ALIGN_SZ_UP(size);

    if(sampled_size < _root->system_page_size && _sane_sampled < MAX_SANE_SAMPLES) {
        void *ps = _iso_alloc_sample(sampled_size);

        if(ps != NULL) {
            UNLOCK_ROOT();
            return ps;
        }
    }
#endif

#if HEAP_PROFILER
    _iso_alloc_profile();
#endif

    /* Allocation requests of SMALL_SZ_MAX bytes or larger are
     * handled by the 'big allocation' path. If a zone was
     * passed in we abort because its a misuse of the API */
    if(UNLIKELY(size > SMALL_SZ_MAX)) {
        /* It's safe to unlock the root at this point because
         * the big zone allocation path uses a different lock */
        UNLOCK_ROOT();

        if(zone != NULL) {
            LOG_AND_ABORT("Allocations of >= %d cannot use custom zones", SMALL_SZ_MAX);
        }

        return _iso_big_alloc(size);
    }

#if FUZZ_MODE
    _verify_all_zones();
#endif

#if THREAD_SUPPORT && THREAD_ZONE_CACHE
    if(LIKELY(zone == NULL)) {
        /* Hot Path: Check the thread cache for a zone this
         * thread recently used for an alloc/free operation.
         * It's likely we are allocating a similar size chunk
         * and this will speed up that operation */
        for(int64_t i = 0; i < thread_zone_cache_count; i++) {
            if(thread_zone_cache[i].chunk_size >= size) {
                bool fit = iso_does_zone_fit(thread_zone_cache[i].zone, size);

                if(fit == true) {
                    zone = thread_zone_cache[i].zone;
                    break;
                }
            }
        }
    }
#endif

    bit_slot_t free_bit_slot = BAD_BIT_SLOT;

    /* Slow Path: This will iterate through all zones
     * looking for a suitable one, this includes the
     * zones we cached above */
    if(LIKELY(zone == NULL)) {
        zone = iso_find_zone_fit(size);
    }

    if(LIKELY(zone != NULL)) {
        /* We only need to check if the zone is usable
         * if it's a custom zone. If we chose this zone
         * then its guaranteed to already be usable */
        if(zone->internally_managed == false) {
            zone = is_zone_usable(zone, size);

            if(zone == NULL) {
                UNLOCK_ROOT();
                return NULL;
            }
        }

        free_bit_slot = zone->next_free_bit_slot;
    } else {
        /* Extra Slow Path: We need a new zone in order
         * to satisfy this allocation request */

        /* The size requested is above default zone sizes
         * but we can still create it. iso_new_zone will
         * align the requested size for us */
        if(size > ZONE_8192) {
            zone = _iso_new_zone(size, true);
        } else {
            /* For chunks smaller than 8192 bytes we
             * bump the size up to the next power of 2 */
            size = next_pow2(size);
            zone = _iso_new_zone(size, true);
        }

        if(UNLIKELY(zone == NULL)) {
            LOG_AND_ABORT("Failed to create a zone for allocation of %zu bytes", size);
        }

        /* This is a brand new zone, so the fast path
         * should always work. Abort if it doesn't */
        free_bit_slot = zone->next_free_bit_slot;

        if(UNLIKELY(free_bit_slot == BAD_BIT_SLOT)) {
            LOG_AND_ABORT("Allocated a new zone with no free bit slots");
        }
    }

    if(free_bit_slot == BAD_BIT_SLOT) {
        UNLOCK_ROOT();
        return NULL;
    }

    UNMASK_ZONE_PTRS(zone);

    zone->next_free_bit_slot = BAD_BIT_SLOT;
    void *p = _iso_alloc_bitslot_from_zone(free_bit_slot, zone);
    MASK_ZONE_PTRS(zone);

    populate_thread_zone_cache(zone);

    UNLOCK_ROOT();
    return p;
}

INTERNAL_HIDDEN iso_alloc_big_zone *iso_find_big_zone(void *p) {
    LOCK_BIG_ZONE();

    /* Its possible we are trying to unmap a big allocation */
    iso_alloc_big_zone *big_zone = _root->big_zone_head;

    if(big_zone != NULL) {
        big_zone = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
    }

    while(big_zone != NULL) {
        check_big_canary(big_zone);

        /* Only a free of the exact address is valid */
        if(p == big_zone->user_pages_start) {
            UNLOCK_BIG_ZONE();
            return big_zone;
        }

        if(UNLIKELY(p > big_zone->user_pages_start) && UNLIKELY(p < (big_zone->user_pages_start + big_zone->size))) {
            LOG_AND_ABORT("Invalid free of big zone allocation at 0x%p in mapping 0x%p", p, big_zone->user_pages_start);
        }

        if(big_zone->next != NULL) {
            big_zone = UNMASK_BIG_ZONE_NEXT(big_zone->next);
        } else {
            big_zone = NULL;
            break;
        }
    }

    UNLOCK_BIG_ZONE();
    return NULL;
}

INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_bitmap_range(void *p) {
    iso_alloc_zone *zone = NULL;

#if THREAD_SUPPORT && THREAD_ZONE_CACHE
    /* Hot Path: Check the thread cache for a zone this
     * thread recently used for an alloc/free operation */
    for(int64_t i = 0; i < thread_zone_cache_count; i++) {
        UNMASK_ZONE_PTRS(thread_zone_cache[i].zone);
        zone = thread_zone_cache[i].zone;
        if(zone->bitmap_start <= p && (zone->bitmap_start + zone->bitmap_size) > p) {
            MASK_ZONE_PTRS(zone);
            return zone;
        }
        MASK_ZONE_PTRS(zone);
    }
#endif

    for(int32_t i = 0; i < _root->zones_used; i++) {
        zone = &_root->zones[i];
        UNMASK_ZONE_PTRS(zone);
        if(zone->bitmap_start <= p && (zone->bitmap_start + zone->bitmap_size) > p) {
            MASK_ZONE_PTRS(zone);
            return zone;
        }
        MASK_ZONE_PTRS(zone);
    }

    return NULL;
}

INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_range(void *p) {
    iso_alloc_zone *zone = NULL;

#if THREAD_SUPPORT && THREAD_ZONE_CACHE
    /* Hot Path: Check the thread cache for a zone this
     * thread recently used for an alloc/free operation */
    for(int64_t i = 0; i < thread_zone_cache_count; i++) {
        UNMASK_ZONE_PTRS(thread_zone_cache[i].zone);
        zone = thread_zone_cache[i].zone;
        if(zone->user_pages_start <= p && (zone->user_pages_start + ZONE_USER_SIZE) > p) {
            MASK_ZONE_PTRS(zone);
            return zone;
        }
        MASK_ZONE_PTRS(zone);
    }
#endif

    for(int32_t i = 0; i < _root->zones_used; i++) {
        zone = &_root->zones[i];
        UNMASK_ZONE_PTRS(zone);
        if(zone->user_pages_start <= p && (zone->user_pages_start + ZONE_USER_SIZE) > p) {
            MASK_ZONE_PTRS(zone);
            return zone;
        }
        MASK_ZONE_PTRS(zone);
    }

    return NULL;
}

/* Checking canaries under ASAN mode is not trivial. ASAN
 * provides a strong guarantee that these chunks haven't
 * been modified in some way */
#if ENABLE_ASAN || DISABLE_CANARY
INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone *big) {
    return;
}

INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone *zone, void *p) {
    return;
}

/* Verify the canary value in an allocation */
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p) {
    return;
}

INTERNAL_HIDDEN INLINE int64_t check_canary_no_abort(iso_alloc_zone *zone, void *p) {
    return OK;
}
#else
/* Verifies both canaries in a big zone structure. This
 * is a fast operation so we call it anytime we iterate
 * through the linked list of big zones */
INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone *big) {
    uint64_t canary = ((uint64_t) big ^ bswap_64((uint64_t) big->user_pages_start) ^ _root->big_zone_canary_secret);

    if(UNLIKELY(big->canary_a != canary)) {
        LOG_AND_ABORT("Big zone 0x%p bottom canary has been corrupted! Value: 0x%x Expected: 0x%x", big, big->canary_a, canary);
    }

    if(UNLIKELY(big->canary_b != canary)) {
        LOG_AND_ABORT("Big zone 0x%p top canary has been corrupted! Value: 0x%x Expected: 0x%x", big, big->canary_a, canary);
    }
}

/* All free chunks get a canary written at both
 * the start and end of their chunks. These canaries
 * are verified when adjacent chunks are allocated,
 * freed, or when the API requests validation. We
 * sacrifice the high byte in entropy to prevent
 * unbounded string reads from leaking it */
INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone *zone, void *p) {
    uint64_t canary = (zone->canary_secret ^ (uint64_t) p) & CANARY_VALIDATE_MASK;
    memcpy(p, &canary, CANARY_SIZE);
    p += (zone->chunk_size - sizeof(uint64_t));
    memcpy(p, &canary, CANARY_SIZE);
}

/* Verify the canary value in an allocation */
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p) {
    uint64_t v = *((uint64_t *) p);
    uint64_t canary = (zone->canary_secret ^ (uint64_t) p) & CANARY_VALIDATE_MASK;

    if(UNLIKELY(v != canary)) {
        LOG_AND_ABORT("Canary at beginning of chunk 0x%p in zone[%d][%d byte chunks] has been corrupted! Value: 0x%x Expected: 0x%x", p, zone->index, zone->chunk_size, v, canary);
    }

    v = *((uint64_t *) (p + zone->chunk_size - sizeof(uint64_t)));

    if(UNLIKELY(v != canary)) {
        LOG_AND_ABORT("Canary at end of chunk 0x%p in zone[%d][%d byte chunks] has been corrupted! Value: 0x%x Expected: 0x%x", p, zone->index, zone->chunk_size, v, canary);
    }
}

INTERNAL_HIDDEN INLINE int64_t check_canary_no_abort(iso_alloc_zone *zone, void *p) {
    uint64_t v = *((uint64_t *) p);
    uint64_t canary = (zone->canary_secret ^ (uint64_t) p) & CANARY_VALIDATE_MASK;

    if(UNLIKELY(v != canary)) {
        LOG("Canary at beginning of chunk 0x%p in zone[%d] has been corrupted! Value: 0x%x Expected: 0x%x", p, zone->index, v, canary);
        return ERR;
    }

    v = *((uint64_t *) (p + zone->chunk_size - sizeof(uint64_t)));

    if(UNLIKELY(v != canary)) {
        LOG("Canary at end of chunk 0x%p in zone[%d] has been corrupted! Value: 0x%x Expected: 0x%x", p, zone->index, v, canary);
        return ERR;
    }

    return OK;
}
#endif

INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone *big_zone, bool permanent) {
    LOCK_BIG_ZONE();
    if(UNLIKELY(big_zone->free == true)) {
        LOG_AND_ABORT("Double free of big zone 0x%p has been detected!", big_zone);
    }

#if !ENABLE_ASAN && SANITIZE_CHUNKS
    memset(big_zone->user_pages_start, POISON_BYTE, big_zone->size);
#endif

    madvise(big_zone->user_pages_start, big_zone->size, MADV_DONTNEED);

    /* If this isn't a permanent free then all we need
     * to do is sanitize the mapping and mark it free.
     * The pages backing the big zone can be reused. */
    if(permanent == false) {
        POISON_BIG_ZONE(big_zone);
        big_zone->free = true;
    } else {
        iso_alloc_big_zone *big = _root->big_zone_head;

        if(big != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_head);
        }

        if(big == big_zone) {
            _root->big_zone_head = NULL;
        } else {
            /* We need to remove this entry from the list */
            while(big != NULL) {
                check_big_canary(big);

                if(UNMASK_BIG_ZONE_NEXT(big->next) == big_zone) {
                    big->next = UNMASK_BIG_ZONE_NEXT(big_zone->next);
                    break;
                }

                if(big->next != NULL) {
                    big = UNMASK_BIG_ZONE_NEXT(big->next);
                } else {
                    big = NULL;
                }
            }
        }

        if(big == NULL) {
            LOG_AND_ABORT("The big zone list has been corrupted, unable to find big zone 0x%p", big_zone);
        }

        mprotect_pages(big_zone->user_pages_start, big_zone->size, PROT_NONE);
        memset(big_zone, POISON_BYTE, sizeof(iso_alloc_big_zone));

        /* Big zone meta data is at a random offset from its base page */
        mprotect_pages(((void *) ROUND_DOWN_PAGE((uintptr_t) big_zone)), _root->system_page_size, PROT_NONE);
    }

    UNLOCK_BIG_ZONE();
}

INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone *zone, void *p, bool permanent) {
    /* Ensure the pointer is properly aligned */
    if(UNLIKELY(IS_ALIGNED((uintptr_t) p) != 0)) {
        LOG_AND_ABORT("Chunk at 0x%p of zone[%d] is not %d byte aligned", p, zone->index, ALIGNMENT);
    }

    uint64_t chunk_offset = (uint64_t) (p - zone->user_pages_start);

    /* Ensure the pointer is a multiple of chunk size */
    if(UNLIKELY((chunk_offset % zone->chunk_size) != 0)) {
        LOG_AND_ABORT("Chunk at 0x%p is not a multiple of zone[%d] chunk size %d. Off by %lu bits", p, zone->index, zone->chunk_size, (chunk_offset % zone->chunk_size));
    }

    size_t chunk_number = (chunk_offset / zone->chunk_size);
    bit_slot_t bit_slot = (chunk_number << BITS_PER_CHUNK_SHIFT);
    bit_slot_t dwords_to_bit_slot = (bit_slot >> BITS_PER_QWORD_SHIFT);

    if(UNLIKELY((zone->bitmap_start + dwords_to_bit_slot) >= (zone->bitmap_start + zone->bitmap_size))) {
        LOG_AND_ABORT("Cannot calculate this chunks location in the bitmap 0x%p", p);
    }

    int64_t which_bit = WHICH_BIT(bit_slot);
    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;

    /* Read out 64 bits from the bitmap. We will write
     * them back before we return. This reduces the
     * number of times we have to hit the bitmap page
     * which could result in a page fault */
    bitmap_index_t b = bm[dwords_to_bit_slot];

    /* Double free detection */
    if(UNLIKELY((GET_BIT(b, which_bit)) == 0)) {
        LOG_AND_ABORT("Double free of chunk 0x%p detected from zone[%d] dwords_to_bit_slot=%lu bit_slot=%" PRIu64, p, zone->index, dwords_to_bit_slot, bit_slot);
    }

    /* Set the next bit so we know this chunk was used */
    SET_BIT(b, (which_bit + 1));

    /* Unset the bit and write the value into the bitmap
     * if this is not a permanent free. A permanent free
     * means this chunk will be marked as if it is a canary */
    if(LIKELY(permanent == false)) {
        UNSET_BIT(b, which_bit);
        insert_free_bit_slot(zone, bit_slot);
        zone->is_full = false;
#if !ENABLE_ASAN && SANITIZE_CHUNKS
        iso_clear_user_chunk(p, zone->chunk_size);
#endif
    } else {
        iso_clear_user_chunk(p, zone->chunk_size);
    }

    bm[dwords_to_bit_slot] = b;

    /* Now that we have free'd this chunk lets validate the
     * chunks before and after it. If they were previously
     * used and currently free they should have canaries
     * we can verify */
#if !ENABLE_ASAN && !DISABLE_CANARY
    write_canary(zone, p);

    if((chunk_number + 1) != GET_CHUNK_COUNT(zone)) {
        bit_slot_t bit_slot_over = ((chunk_number + 1) << BITS_PER_CHUNK_SHIFT);
        dwords_to_bit_slot = (bit_slot_over >> BITS_PER_QWORD_SHIFT);
        which_bit = WHICH_BIT(bit_slot_over);

        if((GET_BIT(bm[dwords_to_bit_slot], (which_bit + 1))) == 1) {
            void *p_over = POINTER_FROM_BITSLOT(zone, bit_slot_over);
            check_canary(zone, p_over);
        }
    }

    if(chunk_number != 0) {
        bit_slot_t bit_slot_under = ((chunk_number - 1) << BITS_PER_CHUNK_SHIFT);
        dwords_to_bit_slot = (bit_slot_under >> BITS_PER_QWORD_SHIFT);
        which_bit = WHICH_BIT(bit_slot_under);

        if((GET_BIT(bm[dwords_to_bit_slot], (which_bit + 1))) == 1) {
            void *p_under = POINTER_FROM_BITSLOT(zone, bit_slot_under);
            check_canary(zone, p_under);
        }
    }
#endif

    POISON_ZONE_CHUNK(zone, p);

    populate_thread_zone_cache(zone);

    return;
}

INTERNAL_HIDDEN void _iso_free(void *p, bool permanent) {
    if(p == NULL) {
        return;
    }

#if ALLOC_SANITY
    int32_t r = _iso_alloc_free_sane_sample(p);

    if(r == OK) {
        return;
    }
#endif

    LOCK_ROOT();

#if FUZZ_MODE
    _verify_all_zones();
#endif

    iso_alloc_zone *zone = iso_find_zone_range(p);

    if(zone != NULL) {
        UNMASK_ZONE_PTRS(zone);
        iso_free_chunk_from_zone(zone, p, permanent);
        MASK_ZONE_PTRS(zone);

#if UAF_PTR_PAGE
        if(UNLIKELY((rand_uint64() % UAF_PTR_PAGE_ODDS) == 1)) {
            _iso_alloc_ptr_search(p, true);
        }
#endif
        UNLOCK_ROOT();
    } else {
        iso_alloc_big_zone *big_zone = iso_find_big_zone(p);
        UNLOCK_ROOT();

        if(big_zone == NULL) {
            LOG_AND_ABORT("Could not find any zone for allocation at 0x%p", p);
        }

        iso_free_big_zone(big_zone, permanent);
        return;
    }
}

/* Disable all use of iso_alloc by protecting the _root */
INTERNAL_HIDDEN void _iso_alloc_protect_root(void) {
    LOCK_ROOT();
    mprotect_pages(_root, sizeof(iso_alloc_root), PROT_NONE);
}

/* Unprotect all use of iso_alloc by allowing R/W of the _root */
INTERNAL_HIDDEN void _iso_alloc_unprotect_root(void) {
    mprotect_pages(_root, sizeof(iso_alloc_root), PROT_READ | PROT_WRITE);
    UNLOCK_ROOT();
}

INTERNAL_HIDDEN size_t _iso_chunk_size(void *p) {
    if(p == NULL) {
        return 0;
    }

#if ALLOC_SANITY
    LOCK_SANITY_CACHE();
    _sane_allocation_t *sane_alloc = _get_sane_alloc(p);

    if(sane_alloc != NULL) {
        size_t orig_size = sane_alloc->orig_size;
        UNLOCK_SANITY_CACHE();
        return orig_size;
    }

    UNLOCK_SANITY_CACHE();
#endif

    LOCK_ROOT();

    /* We cannot return NULL here, we abort instead */
    iso_alloc_zone *zone = iso_find_zone_range(p);

    if(zone == NULL) {
        UNLOCK_ROOT();
        iso_alloc_big_zone *big_zone = iso_find_big_zone(p);

        if(big_zone == NULL) {
            LOG_AND_ABORT("Could not find any zone for allocation at 0x%p", p);
        }

        return big_zone->size;
    }

    UNLOCK_ROOT();
    return zone->chunk_size;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks_in_zone(iso_alloc_zone *zone) {
    LOCK_ROOT();
    uint64_t leaks = _iso_alloc_zone_leak_detector(zone, false);
    UNLOCK_ROOT();
    return leaks;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage() {
    LOCK_ROOT();
    uint64_t mem_usage = __iso_alloc_mem_usage();
    mem_usage += _iso_alloc_big_zone_mem_usage();
    UNLOCK_ROOT();
    return mem_usage;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_big_zone_mem_usage() {
    LOCK_BIG_ZONE();
    uint64_t mem_usage = __iso_alloc_big_zone_mem_usage();
    UNLOCK_BIG_ZONE();
    return mem_usage;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone) {
    LOCK_ROOT();
    uint64_t zone_mem_usage = __iso_alloc_zone_mem_usage(zone);
    UNLOCK_ROOT();
    return zone_mem_usage;
}

#if UNIT_TESTING
/* Some tests require getting access to IsoAlloc internals
 * that aren't supported by the API. We never want these
 * in release builds of the library */
EXTERNAL_API iso_alloc_root *_get_root(void) {
    return _root;
}
#endif
