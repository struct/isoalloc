/* iso_alloc.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

/* Select a random number of chunks to be canaries. These
 * can be verified anytime by calling check_canary()
 * or check_canary_no_abort() */
INTERNAL_HIDDEN void create_canary_chunks(iso_alloc_zone *zone) {
    /* Canary chunks are only for default zone sizes. This
     * is because larger zones would waste a lot of memory
     * if we set aside some of their chunks as canaries */
    if(zone->chunk_size > MAX_DEFAULT_ZONE_SZ) {
        return;
    }

    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t max_bitmap_idx = (zone->bitmap_size / sizeof(int64_t));
    int64_t chunk_count = (ZONE_USER_SIZE / zone->chunk_size);
    int64_t bit_slot;

    /* Roughly %1 of the chunks in this zone will become a canary */
    int64_t canary_count = (chunk_count / CANARY_COUNT_DIV);

    /* This function is only ever called during zone
     * initialization so we don't need to check the
     * current state of any chunks, they're all free.
     * It's possible the call to random() above picked
     * the same index twice, we can live with that
     * collision as canary chunks only provide a small
     * security property anyway */
    for(int64_t i = 0; i < canary_count; i++) {
        int64_t bm_idx = ALIGN_SZ_DOWN((random() % max_bitmap_idx));

        if(0 > bm_idx) {
            bm_idx = 0;
        }

        /* Set the 1st and 2nd bits as 1 */
        SET_BIT(bm[bm_idx], 0);
        SET_BIT(bm[bm_idx], 1);
        bit_slot = (bm_idx * BITS_PER_QWORD);
        void *p = POINTER_FROM_BITSLOT(zone, bit_slot);
        write_canary(zone, p);
    }
}

/* Verify the integrity of all canary chunks and the
 * canary written to all free chunks. This function
 * either aborts or returns nothing */
INTERNAL_HIDDEN void verify_all_zones() {
    for(size_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        if(zone == NULL) {
            break;
        }

        verify_zone(zone);
    }
}

INTERNAL_HIDDEN void verify_zone(iso_alloc_zone *zone) {
    UNMASK_ZONE_PTRS(zone);
    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t bit_slot;
    int64_t bit;

    for(int64_t i = 0; i < (zone->bitmap_size / sizeof(int64_t)); i++) {
        for(int64_t j = 0; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            bit_slot = (i * BITS_PER_QWORD);
            bit = GET_BIT(bm[i], (j + 1));

            /* If this bit is set it is either a free chunk or
             * a canary chunk. Either way it should have a
             * canary we can verify */
            if(bit == 1) {
                void *p = POINTER_FROM_BITSLOT(zone, bit_slot);
                check_canary(zone, p);
            }
        }
    }

    MASK_ZONE_PTRS(zone);
    return;
}

/* Pick a random index in the bitmap and start looking
 * for free bit slots we can add to the cache. The random
 * bitmap index is to protect against biasing the free
 * slot cache with only chunks towards the start of the
 * user mapping. Theres no guarantee this function will
 * find any free slots. */
INTERNAL_HIDDEN INLINE void fill_free_bit_slot_cache(iso_alloc_zone *zone) {
    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t bit_slot;
    int64_t max_bitmap_idx = (zone->bitmap_size / sizeof(int64_t));

    /* This gives us an arbitrary spot in the bitmap to 
     * start searching but may mean we end up with a smaller
     * cache. This will negatively affect performance but
     * leads to a less predictable free list */
    int64_t bm_idx = ALIGN_SZ_DOWN((random() % max_bitmap_idx / 4));

    if(0 > bm_idx) {
        bm_idx = 0;
    }

    memset(zone->free_bit_slot_cache, BAD_BIT_SLOT, sizeof(zone->free_bit_slot_cache));
    zone->free_bit_slot_cache_usable = 0;

    for(zone->free_bit_slot_cache_index = 0; zone->free_bit_slot_cache_index < BIT_SLOT_CACHE_SZ; bm_idx++) {
        /* Don't index outside of the bitmap or
         * we will return inaccurate bit slots */
        if(bm_idx >= max_bitmap_idx) {
            return;
        }

        for(int64_t j = 0; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            if(zone->free_bit_slot_cache_index >= BIT_SLOT_CACHE_SZ) {
                return;
            }

            int64_t bit = GET_BIT(bm[bm_idx], j);

            if(bit == 0) {
                bit_slot = (bm_idx * BITS_PER_QWORD) + j;
                zone->free_bit_slot_cache[zone->free_bit_slot_cache_index] = bit_slot;
                zone->free_bit_slot_cache_index++;
            }
        }
    }
}

INTERNAL_HIDDEN void insert_free_bit_slot(iso_alloc_zone *zone, int64_t bit_slot) {
    if(0 > zone->free_bit_slot_cache_usable || 0 > zone->free_bit_slot_cache_index) {
        LOG_AND_ABORT("Zone[%d] contains a corrupt cache index", zone->index);
    }

#if DEBUG
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
    for(int64_t i = zone->free_bit_slot_cache_usable; i < (sizeof(zone->free_bit_slot_cache) / sizeof(uint64_t)); i++) {
        if(zone->free_bit_slot_cache[i] == bit_slot) {
            LOG_AND_ABORT("Zone[%d] already contains bit slot %ld in cache", zone->index, bit_slot);
        }
    }
#endif

    if(zone->free_bit_slot_cache_index >= BIT_SLOT_CACHE_SZ) {
        return;
    }

    zone->free_bit_slot_cache[zone->free_bit_slot_cache_index] = bit_slot;
    zone->free_bit_slot_cache_index++;
}

INTERNAL_HIDDEN int64_t get_next_free_bit_slot(iso_alloc_zone *zone) {
    if(0 > zone->free_bit_slot_cache_usable || zone->free_bit_slot_cache_usable >= BIT_SLOT_CACHE_SZ ||
       zone->free_bit_slot_cache_usable > zone->free_bit_slot_cache_index) {
        return BAD_BIT_SLOT;
    }

    zone->next_free_bit_slot = zone->free_bit_slot_cache[zone->free_bit_slot_cache_usable];
    zone->free_bit_slot_cache[zone->free_bit_slot_cache_usable++] = BAD_BIT_SLOT;
    return zone->next_free_bit_slot;
}

INTERNAL_HIDDEN INLINE void *get_base_page(void *addr) {
    return (void *) ((uintptr_t) addr & ~(_root->system_page_size - 1));
}

INTERNAL_HIDDEN INLINE void iso_clear_user_chunk(uint8_t *p, size_t size) {
#if SANITIZE_CHUNKS
    memset(p, POISON_BYTE, size);
#endif
}

INTERNAL_HIDDEN INLINE void *mmap_rw_pages(size_t size, bool populate) {
    size = ROUND_UP_PAGE(size);
    void *p = NULL;

    if(populate == true) {
        p = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    } else {
        p = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    if(p == MAP_FAILED) {
        LOG_AND_ABORT("Failed to mmap rw pages");
        return NULL;
    }

    return p;
}

INTERNAL_HIDDEN INLINE void mprotect_pages(void *p, size_t size, int32_t protection) {
    if((mprotect(p, size, protection)) == ERR) {
        LOG_AND_ABORT("Failed to mprotect pages @ %p", p);
    }
}

INTERNAL_HIDDEN void iso_alloc_new_root() {
    void *p = NULL;

    size_t _root_size = sizeof(iso_alloc_root) + (g_page_size * 2);

    if(_root == NULL) {
        p = (void *) mmap_rw_pages(_root_size, true);
    }

    if(p == NULL) {
        LOG_AND_ABORT("Cannot allocate pages for _root");
    }

    _root = (iso_alloc_root *) (p + g_page_size);

    if((pthread_mutex_init(&_root->zone_mutex, NULL)) != 0) {
        LOG_AND_ABORT("Cannot initialize zone mutex for root")
    }

    _root->system_page_size = g_page_size;

    _root->guard_below = p;
    mprotect_pages(_root->guard_below, _root->system_page_size, PROT_NONE);
    madvise(_root->guard_below, _root->system_page_size, MADV_DONTNEED);

    _root->guard_above = (void *) ROUND_UP_PAGE((uintptr_t)(p + sizeof(iso_alloc_root) + _root->system_page_size));
    mprotect_pages(_root->guard_above, _root->system_page_size, PROT_NONE);
    madvise(_root->guard_above, _root->system_page_size, MADV_DONTNEED);
}

INTERNAL_HIDDEN void iso_alloc_initialize() {
    /* Do not allow a reinitialization unless root is NULL */
    if(_root != NULL) {
        return;
    }

    struct timeval t;
    gettimeofday(&t, NULL);
    g_page_size = sysconf(_SC_PAGESIZE);

    iso_alloc_new_root();
    iso_alloc_zone *zone = NULL;

    for(int64_t i = 0; i < (sizeof(default_zones) / sizeof(uint64_t)); i++) {
        if(!(zone = iso_new_zone(default_zones[i], true))) {
            LOG_AND_ABORT("Failed to create a new zone");
        }

        /* This call to mlock may fail if memory limits
         * are set too low. This will not affect us
         * at runtime. It just means some of the default
         * zone meta data may get swapped to disk */
        mlock(zone, sizeof(iso_alloc_zone));
    }

    struct timeval nt;
    gettimeofday(&nt, NULL);
    srandom((t.tv_usec * t.tv_sec) + (nt.tv_usec * nt.tv_sec) + getpid());

    _root->zone_handle_mask = (random() * random());
    iso_alloc_initialized = true;
}

__attribute__((constructor(0))) void iso_alloc_ctor() {
    if(iso_alloc_initialized == false) {
        iso_alloc_initialize();
    }
}

INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone *zone) {
    UNMASK_ZONE_PTRS(zone);

    if(zone->internally_managed == false) {
        /* If this zone was a special case then we don't want
         * to reuse any of its backing pages. Mark them unusable
         * and ensure any future accesses result in a segfault */
        memset(zone->bitmap_start, POISON_BYTE, zone->bitmap_size);
        mprotect_pages(zone->bitmap_start, zone->bitmap_size, PROT_NONE);
        memset(zone->user_pages_start, POISON_BYTE, ZONE_USER_SIZE);
        mprotect_pages(zone->user_pages_start, ZONE_USER_SIZE, PROT_NONE);
        memset(zone, POISON_BYTE, sizeof(iso_alloc_zone));
        /* Purposefully keep the mutex locked. Any thread
         * that tries to allocate/free in this zone should
         * rightfully deadlock */
    } else {
        munmap(zone->bitmap_start, zone->bitmap_size);
        munmap(zone->bitmap_start - _root->system_page_size, _root->system_page_size);
        munmap(zone->bitmap_start + zone->bitmap_size, _root->system_page_size);
        munmap(zone->user_pages_start, ZONE_USER_SIZE);
        munmap(zone->user_pages_start - _root->system_page_size, _root->system_page_size);
        munmap(zone->user_pages_start + ZONE_USER_SIZE, _root->system_page_size);
        memset(zone, POISON_BYTE, sizeof(iso_alloc_zone));
    }
}

__attribute__((destructor(65535))) void iso_alloc_dtor() {
#if DEBUG && (LEAK_DETECTOR || MEM_USAGE)
    uint64_t mb = 0;

    for(size_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        if(zone == NULL) {
            break;
        }

        _iso_alloc_zone_leak_detector(zone);
    }

    mb = _iso_alloc_mem_usage();

#if MEM_USAGE
    LOG("Total megabytes consumed by all zones: %ld", mb);
#endif

#endif

    /* Using MALLOC_HOOK is not recommended. But if you do
     * use it you may find your program crashing in various
     * dynamic linker routines that support destructors. In
     * this case we verify each zone but don't destroy them.
     * This will leak the root structure but we are probably
     * exiting anyway. */
    for(int64_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone *zone = &_root->zones[i];

        if(zone == NULL) {
            break;
        }

        verify_zone(zone);
#ifndef MALLOC_HOOK
        _iso_alloc_destroy_zone(zone);
#endif
    }

    iso_alloc_big_zone *big_zone = _root->big_alloc_zone_head;
    iso_alloc_big_zone *b = NULL;

    while(big_zone != NULL) {
        b = big_zone->next;
        munmap(big_zone - _root->system_page_size, (_root->system_page_size * BIG_ALLOCATION_PAGE_COUNT) + big_zone->size);
        big_zone = b;
    }

#ifndef MALLOC_HOOK
    munmap(_root->guard_below, _root->system_page_size);
    munmap(_root->guard_above, _root->system_page_size);
    pthread_mutex_destroy(&_root->zone_mutex);
    munmap(_root, sizeof(iso_alloc_root));
#endif
}

INTERNAL_HIDDEN iso_alloc_zone *iso_new_zone(size_t size, bool internal) {
    if(_root->zones_used >= MAX_ZONES) {
        LOG_AND_ABORT("Cannot allocate additional zones");
    }

    if((size % ALIGNMENT) != 0) {
        size = ROUND_UP_PAGE(size);
    }

    if(size > SMALL_SZ_MAX) {
        LOG_AND_ABORT("Request for chunk of %ld bytes should be handled by big alloc path", size);
    }

    iso_alloc_zone *new_zone = &_root->zones[_root->zones_used];

    new_zone->internally_managed = internal;
    new_zone->is_full = false;
    new_zone->chunk_size = size;

    /* If a caller requests an allocation that is >=(ZONE_USER_SIZE/2)
     * then we need to allocate a minimum size bitmap */
    size_t bitmap_size = (GET_CHUNK_COUNT(new_zone) * BITS_PER_CHUNK) / BITS_PER_BYTE;
    new_zone->bitmap_size = (bitmap_size > sizeof(uint64_t)) ? bitmap_size : sizeof(uint64_t);

    /* Most of the following fields are effectively immutable
     * and should not change once they are set */

#if PRE_POPULATE_PAGES
    void *p = mmap_rw_pages(new_zone->bitmap_size + (_root->system_page_size * 2), true);
#else
    void *p = mmap_rw_pages(new_zone->bitmap_size + (_root->system_page_size * 2), false);
#endif

    void *bitmap_pages_guard_below = p;
    new_zone->bitmap_start = (p + _root->system_page_size);

    void *bitmap_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + (new_zone->bitmap_size + _root->system_page_size));

    mprotect_pages(bitmap_pages_guard_below, _root->system_page_size, PROT_NONE);
    madvise(bitmap_pages_guard_below, _root->system_page_size, MADV_DONTNEED);

    mprotect_pages(bitmap_pages_guard_above, _root->system_page_size, PROT_NONE);
    madvise(bitmap_pages_guard_above, _root->system_page_size, MADV_DONTNEED);

    /* Bitmap pages are accessed often and usually in sequential order */
    madvise(new_zone->bitmap_start, new_zone->bitmap_size, MADV_WILLNEED);
    madvise(new_zone->bitmap_start, new_zone->bitmap_size, MADV_SEQUENTIAL);

    /* All user pages use MAP_POPULATE. This might seem like we are asking
     * the kernel to commit a lot of memory for us that we may never use
     * but when we call create_canary_chunks() that will happen anyway */
    p = mmap_rw_pages(ZONE_USER_SIZE + (_root->system_page_size * 2), true);

    void *user_pages_guard_below = p;
    new_zone->user_pages_start = (p + _root->system_page_size);
    void *user_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + (ZONE_USER_SIZE + _root->system_page_size));

    mprotect_pages(user_pages_guard_below, _root->system_page_size, PROT_NONE);
    madvise(user_pages_guard_below, _root->system_page_size, MADV_DONTNEED);

    mprotect_pages(user_pages_guard_above, _root->system_page_size, PROT_NONE);
    madvise(user_pages_guard_above, _root->system_page_size, MADV_DONTNEED);

    /* User pages will be accessed in an unpredictable order */
    madvise(new_zone->user_pages_start, ZONE_USER_SIZE, MADV_WILLNEED);
    madvise(new_zone->user_pages_start, ZONE_USER_SIZE, MADV_RANDOM);

    new_zone->index = _root->zones_used;
    new_zone->canary_secret = (random() * random());
    new_zone->pointer_mask = (random() * random());

    /* This should be the only place we call this function */
    create_canary_chunks(new_zone);

    /* When we create a new zone its an opportunity to
     * populate our free list cache with random entries */
    fill_free_bit_slot_cache(new_zone);

    /* Prime the next_free_bit_slot member */
    get_next_free_bit_slot(new_zone);

    MASK_ZONE_PTRS(new_zone);

    _root->zones_used++;
    return new_zone;
}

/* Iterate through a zone bitmap a dword at a time
 * looking for empty holes (i.e. slot == 0) */
INTERNAL_HIDDEN int64_t iso_scan_zone_free_slot(iso_alloc_zone *zone) {
    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t bit_position = BAD_BIT_SLOT;

    /* Iterate the entire bitmap a dword at a time */
    for(int64_t i = 0; i < (zone->bitmap_size / sizeof(int64_t)); i++) {
        /* If the byte is 0 then there are some free
         * slots we can use at this location */
        if(bm[i] == 0x0) {
            bit_position = (i * BITS_PER_QWORD);
            return bit_position;
        }
    }

    return bit_position;
}

/* This function scans an entire bitmap bit-by-bit
 * and returns the first free bit position. In a heavily
 * used zone this function will be slow to search */
INTERNAL_HIDDEN INLINE int64_t iso_scan_zone_free_slot_slow(iso_alloc_zone *zone) {
    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t bit_position = BAD_BIT_SLOT;
    int64_t bit;

    for(int64_t i = 0; i < (zone->bitmap_size / sizeof(int64_t)); i++) {
        for(int64_t j = 0; j < BITS_PER_QWORD; j += BITS_PER_CHUNK) {
            bit = GET_BIT(bm[i], j);

            if(bit == 0) {
                bit_position = (i * BITS_PER_QWORD) + j;
                return bit_position;
            }
        }
    }

    return bit_position;
}

INTERNAL_HIDDEN iso_alloc_zone *is_zone_usable(iso_alloc_zone *zone, size_t size) {
    if(zone->next_free_bit_slot != BAD_BIT_SLOT) {
        return zone;
    }

    UNMASK_ZONE_PTRS(zone);

    /* This zone may fit this chunk but if the zone was
     * created for chunks more than N* larger than the
     * requested allocation size then we would be wasting
     * a lot memory by using it. Lets force the creation
     * of a new zone instead. We only do this for sizes
     * beyond ZONE_1024 bytes. In other words we can live
     * with some wasted space in zones that manage chunks
     * smaller than that */
    if(zone->chunk_size >= (size * WASTED_SZ_MULTIPLIER) && size > ZONE_1024) {
        MASK_ZONE_PTRS(zone);
        return NULL;
    }

    /* If the cache for this zone is empty we should
     * refill it to make future allocations faster */
    if(zone->free_bit_slot_cache_usable == zone->free_bit_slot_cache_index) {
        fill_free_bit_slot_cache(zone);
    }

    int64_t bit_slot = get_next_free_bit_slot(zone);

    if(bit_slot != BAD_BIT_SLOT) {
        MASK_ZONE_PTRS(zone);
        return zone;
    }

    /* Free list failed, use a fast search */
    bit_slot = iso_scan_zone_free_slot(zone);

    if(bit_slot == BAD_BIT_SLOT) {
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

/* Finds a zone that can fit this allocation request */
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_fit(size_t size) {
    iso_alloc_zone *zone = NULL;

    /* A simple optimization to find which default zone
     * should fit this allocation. If we fail then a
     * slower iterative approach is used. The longer a
     * program runs the more likely we will fail this
     * fast path as default zones may fill up */
    int64_t i = 0;

    if(size >= ZONE_512) {
        i = (sizeof(default_zones) / sizeof(uint64_t) / 2);
    } else if(size > ZONE_8192) {
        i = sizeof(default_zones) / sizeof(uint64_t);
    } else {
        i = 0;
    }

    for(; i < _root->zones_used; i++) {
        zone = &_root->zones[i];

        if(zone == NULL) {
            return NULL;
        }

        if(zone->chunk_size < size || zone->internally_managed == false || zone->is_full == true) {
            continue;
        }

        /* We found a zone, lets try to find a free slot in it */
        if(zone->chunk_size >= size) {
            zone = is_zone_usable(zone, size);

            if(zone == NULL) {
                continue;
            } else {
                return zone;
            }
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
    size = ROUND_UP_PAGE(size);

    /* Let's first see if theres an existing set of
     * pages that can satisfy this allocation request */
    iso_alloc_big_zone *big = _root->big_alloc_zone_head;
    iso_alloc_big_zone *last_big = NULL;

    while(big != NULL) {
        if(big->free == true && big->size >= size) {
            break;
        }

        last_big = big;
        big = big->next;
    }

    /* We need to setup a new set of pages */
    if(big == NULL) {
        void *p = mmap_rw_pages((_root->system_page_size * BIG_ALLOCATION_PAGE_COUNT) + size, false);

        /* The first page is a guard page */
        mprotect_pages(p, _root->system_page_size, PROT_NONE);
        madvise(p, _root->system_page_size, MADV_DONTNEED);

        /* Setup the next guard page */
        void *next_gp = (p + _root->system_page_size * 2);
        mprotect_pages(next_gp, _root->system_page_size, PROT_NONE);
        madvise(next_gp, _root->system_page_size, MADV_DONTNEED);

        /* User data starts at the beginning of the 3rd page */
        void *user_pages = next_gp + _root->system_page_size;
        madvise(user_pages, size, MADV_WILLNEED);
        madvise(user_pages, size, MADV_RANDOM);

        /* The second page is for meta data and it is placed
         * at a random offset from the start of the page */
        big = (iso_alloc_big_zone *) (p + _root->system_page_size);
        madvise(big, _root->system_page_size, MADV_WILLNEED);
        big = (iso_alloc_big_zone *) ((p + _root->system_page_size) + (random() % (_root->system_page_size - sizeof(iso_alloc_big_zone))));
        big->user_pages_start = user_pages;
        big->free = false;
        big->size = size;
        big->next = NULL;

        void *last_gp = (user_pages + size);
        mprotect_pages(last_gp, _root->system_page_size, PROT_NONE);
        madvise(last_gp, _root->system_page_size, MADV_DONTNEED);

        if(last_big != NULL) {
            last_big->next = big;
        }

        if(_root->big_alloc_zone_head == NULL) {
            _root->big_alloc_zone_head = big;
        }

        return big->user_pages_start;
    } else {
        big->free = false;
        return big->user_pages_start;
    }
}

INTERNAL_HIDDEN void *_iso_alloc(iso_alloc_zone *zone, size_t size) {
    if(iso_alloc_initialized == false) {
        iso_alloc_initialize();
    }

    /* Allocation requests of 8mb or larger are handled
     * by the 'big allocation' path. If a zone was passed
     * in we abort because its a misuse of the API */
    if(size > SMALL_SZ_MAX) {
        if(zone != NULL) {
            LOG_AND_ABORT("Allocations of >= 8mb cannot use custom zones");
        }

        return _iso_big_alloc(size);
    }

    if(zone == NULL) {
        zone = iso_find_zone_fit(size);
    }

    int64_t free_bit_slot = BAD_BIT_SLOT;

    if(zone == NULL) {
        /* In order to guarantee an 8 byte memory alignment
         * for all allocations we only create zones that
         * work with default allocation sizes */
        for(int64_t i = 0; i < (sizeof(default_zones) / sizeof(uint64_t)); i++) {
            if(size < default_zones[i]) {
                size = default_zones[i];
                zone = iso_new_zone(size, true);

                if(zone == NULL) {
                    LOG_AND_ABORT("Failed to create a new zone for allocation of %zu bytes", size);
                } else {
                    break;
                }
            }
        }

        /* The size requested is above default zone sizes
         * but we can still create it. iso_new_zone will
         * align the requested size for us */
        if(zone == NULL) {
            zone = iso_new_zone(size, true);

            if(zone == NULL) {
                LOG_AND_ABORT("Failed to create a zone for allocation of %zu bytes", size);
            }
        }

        /* This is a brand new zone, so the fast path
         * should always work. Abort if it doesn't */
        free_bit_slot = zone->next_free_bit_slot;

        if(free_bit_slot == BAD_BIT_SLOT) {
            LOG_AND_ABORT("Allocated a new zone with no free bit slots");
        }
    } else {
        /* We only need to check if the zone is usable
         * if we didn't choose the zone ourselves. If
         * we chose this zone then its guaranteed to
         * already be usable */
        if(zone->internally_managed == false) {
            zone = is_zone_usable(zone, size);

            if(zone == NULL) {
                return NULL;
            }
        }

        free_bit_slot = zone->next_free_bit_slot;
    }

    if(free_bit_slot == BAD_BIT_SLOT) {
        return NULL;
    }

    UNMASK_ZONE_PTRS(zone);

    zone->next_free_bit_slot = BAD_BIT_SLOT;

    int64_t dwords_to_bit_slot = (free_bit_slot / BITS_PER_QWORD);
    int64_t which_bit = (free_bit_slot % BITS_PER_QWORD);

    void *p = POINTER_FROM_BITSLOT(zone, free_bit_slot);
    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t b = bm[dwords_to_bit_slot];

    if(p > zone->user_pages_start + ZONE_USER_SIZE) {
        LOG_AND_ABORT("Allocating an address %p from zone[%d], bit slot %ld %ld bytes %ld pages outside zones user pages %p %p",
                      p, zone->index, free_bit_slot, p - (zone->user_pages_start + ZONE_USER_SIZE), (p - (zone->user_pages_start + ZONE_USER_SIZE)) / _root->system_page_size, zone->user_pages_start, zone->user_pages_start + ZONE_USER_SIZE);
    }

    if((GET_BIT(b, which_bit)) != 0) {
        LOG_AND_ABORT("Zone[%d] for chunk size %d cannot return allocated chunk at %p bitmap location @ %p. bit slot was %ld, which_bit was %ld",
                      zone->index, zone->chunk_size, p, &bm[dwords_to_bit_slot], free_bit_slot, which_bit);
    }

    /* This chunk was previously allocated and free'd which
     * means it must have a canary written in its first dword.
     * Here we check the validity of that canary and abort
     * if its been corrupted */
    if((GET_BIT(b, (which_bit + 1))) == 1) {
        check_canary(zone, p);
        memset(p, 0x0, CANARY_SIZE);
    }

    /* Set the in-use bit */
    SET_BIT(b, which_bit);

    /* The second bit is flipped to 0 while in use. This
     * is because a previously in use chunk would have
     * a bit pattern of 11 which makes it looks the same
     * as a canary chunk. This bit is set again upon free. */
    UNSET_BIT(b, (which_bit + 1));

    bm[dwords_to_bit_slot] = b;

    MASK_ZONE_PTRS(zone);

    return p;
}

INTERNAL_HIDDEN iso_alloc_big_zone *iso_find_big_zone(void *p) {
    /* Its possible we are trying to unmap a big allocation */
    iso_alloc_big_zone *big = _root->big_alloc_zone_head;

    while(big != NULL) {
        /* Only a free of the exact address is valid */
        if(p == big->user_pages_start) {
            return big;
        }

        if(p > big->user_pages_start && p < (big->user_pages_start + big->size)) {
            LOG_AND_ABORT("Invalid free of big allocation at %p in mapping %p", p, big->user_pages_start);
        }

        big = big->next;
    }

    return NULL;
}

INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_range(void *p) {
    iso_alloc_zone *zone = NULL;

    for(uint32_t i = 0; i < _root->zones_used; i++) {
        zone = &_root->zones[i];

        if(i == _root->zones_used) {
            break;
        }

        UNMASK_ZONE_PTRS(zone);

        if(zone->user_pages_start <= p && (zone->user_pages_start + ZONE_USER_SIZE) > p) {
            MASK_ZONE_PTRS(zone);
            return zone;
        }

        MASK_ZONE_PTRS(zone);
    }

    return NULL;
}

/* All free chunks get a canary written at both
 * the start and end of their chunks. These canaries
 * are verified when adjacent chunks are allocated,
 * freed, or when the API requests validation */
INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone *zone, void *p) {
    uint64_t canary = zone->canary_secret ^ (uint64_t) p;
    memcpy(p, &canary, CANARY_SIZE);
    p += (zone->chunk_size - sizeof(uint64_t));
    memcpy(p, &canary, CANARY_SIZE);
}

/* Verify the canary value in an allocation */
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p) {
    uint64_t v = *((uint64_t *) p);
    uint64_t canary = (zone->canary_secret ^ (uint64_t) p);

    if(v != canary) {
        LOG_AND_ABORT("Canary at beginning of chunk %p in zone[%d] has been corrupted! Value: 0x%lx Expected: 0x%lx", p, zone->index, v, (uint64_t)(zone->canary_secret ^ (uint64_t) p));
    }

    v = *((uint64_t *) (p + zone->chunk_size - sizeof(uint64_t)));

    if(v != canary) {
        LOG_AND_ABORT("Canary at end of chunk %p in zone[%d] has been corrupted! Value: 0x%lx Expected: 0x%lx", p, zone->index, v, (uint64_t)(zone->canary_secret ^ (uint64_t) p));
    }
}

INTERNAL_HIDDEN INLINE int64_t check_canary_no_abort(iso_alloc_zone *zone, void *p) {
    uint64_t v = *((uint64_t *) p);
    uint64_t canary = (zone->canary_secret ^ (uint64_t) p);

    if(v != canary) {
        LOG("Canary at beginning of chunk %p in zone[%d] has been corrupted! Value: 0x%lx Expected: 0x%lx", p, zone->index, v, (uint64_t)(zone->canary_secret ^ (uint64_t) p));
        return ERR;
    }

    v = *((uint64_t *) (p + zone->chunk_size - sizeof(uint64_t)));

    if(v != canary) {
        LOG("Canary at end of chunk %p in zone[%d] has been corrupted! Value: 0x%lx Expected: 0x%lx", p, zone->index, v, (uint64_t)(zone->canary_secret ^ (uint64_t) p));
        return ERR;
    }

    return OK;
}

INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone *big_zone, bool permanent) {
    /* If this isn't a permanent free then all we need
     * to do is sanitize the mapping and mark it free */
    memset(big_zone->user_pages_start, POISON_BYTE, big_zone->size);

    if(permanent == false) {
        /* There is nothing else to do but mark this big zone free */
        big_zone->free = true;
    } else {
        iso_alloc_big_zone *last = _root->big_alloc_zone_head;

        if(last == big_zone) {
            _root->big_alloc_zone_head = NULL;
        } else {
            /* We need to remove this entry from the list */
            while(last != NULL) {
                if(last->next == big_zone) {
                    last->next = big_zone->next;
                    break;
                }

                last = last->next;
            }
        }

        if(last == NULL) {
            LOG_AND_ABORT("The big zone list has been corrupted, unable to find big zone %p", big_zone);
        }

        /* Sanitize and mark the entire thing PROT_NONE */
        memset(big_zone, POISON_BYTE, sizeof(iso_alloc_big_zone));
        mprotect((void *) ROUND_DOWN_PAGE((uintptr_t)(big_zone - _root->system_page_size)), (_root->system_page_size * BIG_ALLOCATION_PAGE_COUNT) + big_zone->size, PROT_NONE);
    }
}

INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone *zone, void *p, bool permanent) {
    /* Ensure the pointer is properly aligned */
    if(((uintptr_t) p % ALIGNMENT) != 0) {
        LOG_AND_ABORT("Chunk at %p of zone[%d] is not %d byte aligned", p, zone->index, ALIGNMENT);
    }

    uint64_t chunk_offset = (uint64_t)(p - zone->user_pages_start);

    /* Ensure the pointer is a multiple of chunk size */
    if((chunk_offset % zone->chunk_size) != 0) {
        LOG_AND_ABORT("Chunk at %p is not a multiple of zone[%d] chunk size %d. Off by %lu bits", p, zone->index, zone->chunk_size, (chunk_offset % zone->chunk_size));
    }

    size_t chunk_number = (chunk_offset / zone->chunk_size);
    int64_t bit_slot = (chunk_number * BITS_PER_CHUNK);
    int64_t dwords_to_bit_slot = (bit_slot / BITS_PER_QWORD);

    if((zone->bitmap_start + dwords_to_bit_slot) >= (zone->bitmap_start + zone->bitmap_size)) {
        LOG_AND_ABORT("Cannot calculate this chunks location in the bitmap %p", p);
    }

    int64_t which_bit = (bit_slot % BITS_PER_QWORD);
    int64_t *bm = (int64_t *) zone->bitmap_start;
    int64_t b = bm[dwords_to_bit_slot];

    /* Double free detection */
    if((GET_BIT(b, which_bit)) == 0) {
        LOG_AND_ABORT("Double free of chunk %p detected from zone[%d] dwords_to_bit_slot=%ld bit_slot=%ld", p, zone->index, dwords_to_bit_slot, bit_slot);
    }

    /* Set the next bit so we know this chunk was used */
    SET_BIT(b, (which_bit + 1));

    /* Unset the bit and write the value into the bitmap
     * if this is not a permanent free. A permanent free
     * means this chunk will be marked as if it is a canary */
    if(permanent == false) {
        UNSET_BIT(b, which_bit);
    }

    bm[dwords_to_bit_slot] = b;

    iso_clear_user_chunk(p, zone->chunk_size);

    write_canary(zone, p);

    /* Now that we have free'd this chunk lets validate the
     * chunks before and after it. If they were previously
     * used and currently free they should have canaries
     * we can verify */
    if((p + zone->chunk_size) < (zone->user_pages_start + ZONE_USER_SIZE)) {
        int64_t bit_position_over = ((chunk_number + 1) * BITS_PER_CHUNK);
        dwords_to_bit_slot = (bit_position_over / BITS_PER_QWORD);
        b = bm[dwords_to_bit_slot];
        which_bit = (bit_position_over % BITS_PER_QWORD);

        if((GET_BIT(b, (which_bit + 1))) == 1) {
            void *p_over = POINTER_FROM_BITSLOT(zone, bit_position_over);
            check_canary(zone, p_over);
        }
    }

    if((p - zone->chunk_size) > zone->user_pages_start) {
        int64_t bit_position_under = ((chunk_number - 1) * BITS_PER_CHUNK);
        dwords_to_bit_slot = (bit_position_under / BITS_PER_QWORD);
        b = bm[dwords_to_bit_slot];
        which_bit = (bit_position_under % BITS_PER_QWORD);

        if((GET_BIT(b, (which_bit + 1))) == 1) {
            void *p_under = POINTER_FROM_BITSLOT(zone, bit_position_under);
            check_canary(zone, p_under);
        }
    }

    insert_free_bit_slot(zone, bit_slot);
    zone->is_full = false;
    return;
}

INTERNAL_HIDDEN void _iso_free(void *p, bool permanent) {
    if(p == NULL) {
        return;
    }

    iso_alloc_zone *zone = iso_find_zone_range(p);

    if(zone == NULL) {
        iso_alloc_big_zone *big_zone = iso_find_big_zone(p);

        if(big_zone == NULL) {
            LOG_AND_ABORT("Could not find any zone for allocation at %p", p);
        }

        iso_free_big_zone(big_zone, permanent);
    } else {
        UNMASK_ZONE_PTRS(zone);
        iso_free_chunk_from_zone(zone, p, permanent);
        MASK_ZONE_PTRS(zone);
    }
}

/* Disable all use of iso_alloc by protecting the _root */
INTERNAL_HIDDEN void _iso_alloc_protect_root() {
    mprotect_pages(_root, sizeof(iso_alloc_root), PROT_NONE);
}

/* Unprotect all use of iso_alloc by allowing R/W of the _root */
INTERNAL_HIDDEN void _iso_alloc_unprotect_root() {
    mprotect_pages(_root, sizeof(iso_alloc_root), PROT_READ | PROT_WRITE);
}

INTERNAL_HIDDEN size_t _iso_chunk_size(void *p) {
    if(p == NULL) {
        return 0;
    }

    /* We cannot return NULL here, we abort instead */
    iso_alloc_zone *zone = iso_find_zone_range(p);

    if(zone == NULL) {
        iso_alloc_big_zone *big_zone = iso_find_big_zone(p);

        if(big_zone == NULL) {
            LOG_AND_ABORT("Could not find any zone for allocation at %p", p);
        }

        return big_zone->size;
    }

    return zone->chunk_size;
}
