/* iso_alloc.c - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"
#include "iso_alloc_sanity.h"

#if HEAP_PROFILER
#include "iso_alloc_profiler.h"
#endif

#if THREAD_SUPPORT

#if USE_SPINLOCK
atomic_flag root_busy_flag;
#else
pthread_mutex_t root_busy_mutex;
#endif

uint32_t g_page_size;
uint32_t g_page_size_shift;
iso_alloc_root *_root;

/* Every thread owns one iso_thread_state_t holding its zone
 * cache and quarantine. It is mapped by this allocator on the
 * threads first use, surrounded by guard pages, and released
 * by the pthread key destructor when the thread exits. It used
 * to live in __thread storage, but on macOS that storage is
 * malloc'd by dyld and torn down by a key destructor of its
 * own, which can run before ours and left the quarantine flush
 * reading freed memory. Owning the block makes its lifetime
 * ours on every platform */
static pthread_key_t thread_state_key;

/* Serializes the registry of thread states in the root. It is
 * separate from the root lock so a thread can create its state
 * from a path that already holds the root lock. Lock order is
 * root, then registry, then a quarantine */
static atomic_flag thread_registry_flag;

#define LOCK_THREAD_REGISTRY() \
    do {                       \
    } while(atomic_flag_test_and_set_explicit(&thread_registry_flag, memory_order_acquire));

#define UNLOCK_THREAD_REGISTRY() \
    atomic_flag_clear_explicit(&thread_registry_flag, memory_order_release);

#if __APPLE__
/* On macOS the pthread specific values are an array of direct
 * slots reached from the thread pointer register, so the state
 * pointer is two loads instead of a call into dyld, which is
 * what a __thread access costs in a dylib. The layout is
 * verified once at startup and pthread_getspecific is used if
 * it does not hold */
static bool direct_tls_slots;

static INLINE void **tls_slots(void) {
#if __aarch64__
    uint64_t tp;
    __asm__("mrs %0, tpidrro_el0\n\tbic %0, %0, #7"
            : "=r"(tp));
    return (void **) tp;
#elif __x86_64__
    void **tp;
    __asm__("movq %%gs:0, %0"
            : "=r"(tp));
    return tp;
#else
    return NULL;
#endif
}

static INLINE iso_thread_state_t *thread_state_get(void) {
    if(LIKELY(direct_tls_slots == true)) {
        return (iso_thread_state_t *) tls_slots()[thread_state_key];
    }

    return (iso_thread_state_t *) pthread_getspecific(thread_state_key);
}

static INLINE void thread_state_set(iso_thread_state_t *ts) {
    if(UNLIKELY(pthread_setspecific(thread_state_key, ts) != 0)) {
        LOG_AND_ABORT("Failed to set the thread state key");
    }
}
#else
/* Elsewhere a __thread pointer is a single load with the
 * initial exec TLS model and the storage outlives the key
 * destructors, so it caches the pointer the key also holds */
static __thread iso_thread_state_t *thread_state_tls;

static INLINE iso_thread_state_t *thread_state_get(void) {
    return thread_state_tls;
}

static INLINE void thread_state_set(iso_thread_state_t *ts) {
    /* Publish to this thread before touching the key. Some libc
     * implementations allocate inside pthread_setspecific and
     * that allocation must find the state rather than create
     * another one */
    thread_state_tls = ts;

    if(UNLIKELY(pthread_setspecific(thread_state_key, ts) != 0)) {
        LOG_AND_ABORT("Failed to set the thread state key");
    }
}
#endif

/* Runs when a thread exits. Frees whatever its quarantine still
 * holds, unlinks it from the registry so flush_all_quarantines
 * never walks into unmapped memory, and releases the state. The
 * key already reads as NULL here, so a free from a destructor
 * that runs after this one creates a fresh state which pthread
 * passes back to us in a later round */
static void thread_state_dtor(void *arg) {
    iso_thread_state_t *ts = (iso_thread_state_t *) arg;
    iso_quarantine_t *q = &ts->quarantine;

    LOCK_QUARANTINE(q);
    flush_quarantine(q, false);
    UNLOCK_QUARANTINE(q);

    LOCK_THREAD_REGISTRY();
    iso_quarantine_t **link = &_root->quarantines;

    while(*link != NULL && *link != q) {
        link = &(*link)->next;
    }

    if(*link == q) {
        *link = q->next;
    }

    UNLOCK_THREAD_REGISTRY();

#if !__APPLE__
    thread_state_tls = NULL;
#endif
    unmap_guarded_pages(ts, sizeof(iso_thread_state_t));
}

/* Creates this threads state the first time it allocates or
 * frees. Never takes the root lock, a caller may hold it */
static iso_thread_state_t *thread_state_init(void) {
    iso_thread_state_t *ts = (iso_thread_state_t *) mmap_guarded_rw_pages(sizeof(iso_thread_state_t), true, NULL);

    if(UNLIKELY(ts == NULL)) {
        LOG_AND_ABORT("Failed to map the thread state");
    }

    thread_state_set(ts);

    LOCK_THREAD_REGISTRY();
    ts->quarantine.next = _root->quarantines;
    _root->quarantines = &ts->quarantine;
    UNLOCK_THREAD_REGISTRY();
    return ts;
}

static INLINE iso_thread_state_t *thread_state(void) {
    iso_thread_state_t *ts = thread_state_get();

    if(UNLIKELY(ts == NULL)) {
        ts = thread_state_init();
    }

    return ts;
}
#else
uint32_t g_page_size;
uint32_t g_page_size_shift;
iso_alloc_root *_root;

/* Without threads one state serves the process. It is mapped
 * with guard pages at startup rather than living in .bss */
static iso_thread_state_t *thread_state_single;

static INLINE iso_thread_state_t *thread_state(void) {
    return thread_state_single;
}
#endif

#define THREAD_ZONE_CACHE() (&thread_state()->cache)
#define THREAD_QUARANTINE() (&thread_state()->quarantine)

INTERNAL_HIDDEN iso_alloc_root *iso_alloc_new_root(void) {
    iso_alloc_root *p = NULL;

    p = (void *) mmap_guarded_rw_pages(sizeof(iso_alloc_root), true, ROOT_NAME);

    if(p == NULL) {
        LOG_AND_ABORT("Cannot allocate pages for root");
    }

#if __APPLE__
    while(madvise((void *) p, ROUND_UP_PAGE(sizeof(iso_alloc_root)), MADV_FREE_REUSE) && errno == EAGAIN) {
    }
#endif
    return p;
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

    /* We mlock the root or every allocation would
     * result in a soft page fault */
    MLOCK(_root, sizeof(iso_alloc_root));

#if ARM_MTE
    if(iso_is_mte_supported() == false) {
        _root->arm_mte_enabled = false;
    } else {
        _root->arm_mte_enabled = true;
        prctl(PR_SET_TAGGED_ADDR_CTRL,
              PR_TAGGED_ADDR_ENABLE | PR_MTE_TCF_SYNC | (0xfffe << PR_MTE_TAG_SHIFT),
              0, 0, 0);
    }
#endif

    _root->zone_retirement_shf = _log2(ZONE_ALLOC_RETIRE);
    _root->last_purge_ms = coarse_now_ms();

    /* Threads that find every zone of a class busy get a new
     * zone up to this many per class, so under contention they
     * spread out instead of queueing on one lock */
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    _root->zones_per_class_cap = (ncpu > 2) ? (uint32_t) (ncpu * 2) : 4;
    _root->zones_size = (MAX_ZONES * sizeof(iso_alloc_zone_t));
    _root->zones_size += (g_page_size * 2);
    _root->zones_size = ROUND_UP_PAGE(_root->zones_size);

    /* Allocate memory with guard pages to hold zone data */
    _root->zones = mmap_guarded_rw_pages(_root->zones_size, false, "isoalloc zone metadata");

#if __APPLE__
    darwin_reuse(_root->zones, g_page_size);
#endif
#if __linux__ && defined(MADV_NOHUGEPAGE)
    /* Only the zone structs in use are ever touched. With
     * transparent huge pages set to always the first touch
     * would make 2 MB of this array resident for nothing */
    madvise(_root->zones, _root->zones_size - (g_page_size * 2), MADV_NOHUGEPAGE);
#endif
    /* The zones array is 8192 structs of about 1.2 KB. Locking
     * it all would make the whole array resident at startup, so
     * each zone struct is locked as it is created instead */

#if THREAD_SUPPORT
    /* The destructor flushes a threads quarantine when it
     * exits so no chunk is left allocated. Created here so
     * both the constructor and the lazy init path have it */
    if(pthread_key_create(&thread_state_key, thread_state_dtor) != 0) {
        LOG_AND_ABORT("Failed to create the thread state key");
    }

#if __APPLE__
    /* Confirm the key indexes the direct slot array before the
     * fast path relies on it. The probe value is cleared again
     * so this thread starts with no state like every other */
    void *probe = (void *) ((uintptr_t) &thread_state_key | 1);

    if(pthread_setspecific(thread_state_key, probe) != 0) {
        LOG_AND_ABORT("Failed to probe the thread state key");
    }

    direct_tls_slots = (thread_state_key < PTHREAD_KEYS_MAX && tls_slots() != NULL && tls_slots()[thread_state_key] == probe);
    pthread_setspecific(thread_state_key, NULL);
#endif
#else
    size_t c = ROUND_UP_PAGE(sizeof(iso_thread_state_t));
    thread_state_single = mmap_guarded_rw_pages(c, true, NULL);
#if __APPLE__
    darwin_reuse(thread_state_single, c);
#endif
    MLOCK(thread_state_single, c);
#endif

    iso_ptr_map_init(&_root->zone_map, ZONE_MAP_CAPACITY, true);
    iso_ptr_map_init(&_root->big_zone_map, BIG_ZONE_MAP_INITIAL_CAPACITY, false);

    for(int i = 0; i < DEFAULT_ZONE_COUNT; i++) {
        if((_iso_new_zone(default_zones[i], true, -1)) == NULL) {
            LOG_AND_ABORT("Failed to create a new zone");
        }
    }

    char *name = NULL;

#if NAMED_MAPPINGS && (__ANDROID__ || KERNEL_VERSION_SEQ_5_17)
    name = PREALLOC_BITMAPS;
#endif

    /* The trailing 0 in small_bitmap_sizes is not a bucket */
    const int sbsi = (sizeof(small_bitmap_sizes) / sizeof(int)) - 1;

    for(int i = 0; i < sbsi; i++) {
        _root->bitmaps[i].bitmap = mmap_rw_pages(g_page_size, false, name);
        _root->bitmaps[i].bucket = small_bitmap_sizes[i];
    }

    _root->seed = rand_uint64();
    /* Handle masks may be leaked via iso_alloc_new_zone */
    _root->zone_handle_mask = rand_uint64();
    _root->big_zone_next_mask = us_rand_uint64(&_root->seed);
    _root->big_zone_canary_secret = us_rand_uint64(&_root->seed);
}

INTERNAL_HIDDEN void _iso_alloc_initialize(void) {
    if(_root != NULL) {
        return;
    }

    g_page_size = sysconf(_SC_PAGESIZE);
    g_page_size_shift = _log2(g_page_size);

    iso_alloc_initialize_global_root();

#if THREAD_SUPPORT && !USE_SPINLOCK
    pthread_mutex_init(&root_busy_mutex, NULL);
    pthread_mutex_init(&_root->big_zone_free_mutex, NULL);
    pthread_mutex_init(&_root->big_zone_used_mutex, NULL);
#if ALLOC_SANITY
    pthread_mutex_init(&sane_cache_mutex, NULL);
#endif
#endif

#if HEAP_PROFILER
    _initialize_profiler();
#endif

#if NO_ZERO_ALLOCATIONS
    _root->zero_alloc_page = mmap_pages(g_page_size, false, NULL, PROT_NONE);
#endif

#if UAF_PTR_PAGE
    _root->uaf_ptr_page = mmap_pages(g_page_size, false, NULL, PROT_NONE);
#endif

#if ALLOC_SANITY && UNINIT_READ_SANITY
    _iso_alloc_setup_userfaultfd();
#endif

#if ALLOC_SANITY
    _sanity_canary = us_rand_uint64(&_root->seed);
#endif

#if SIGNAL_HANDLER
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER | SA_SIGINFO;
    sa.sa_sigaction = &handle_signal;

    if(sigaction(SIGSEGV, &sa, NULL) == ERR) {
        LOG("Could not register signal handler");
    }
#endif
}

#if AUTO_CTOR_DTOR
__attribute__((destructor(LAST_DTOR))) void iso_alloc_dtor(void) {
    _iso_alloc_destroy();
}

__attribute__((constructor(FIRST_CTOR))) void iso_alloc_ctor(void) {
    _iso_alloc_initialize();
}
#endif

INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone_t *zone) {
    LOCK_ROOT();

    /* Clearing this threads cache needs no lock but happens
     * here so another thread cannot cache the zone we are
     * about to unmap. Every threads quarantine is flushed so
     * none of them holds a chunk from it, and that happens
     * before the zone lock is taken because the flush frees
     * chunks that may live in this very zone */
    clear_zone_cache();
    flush_all_quarantines();

    LOCK_ZONE(zone);
    _iso_alloc_destroy_zone_unlocked(zone, true);
    UNLOCK_ZONE(zone);
    UNLOCK_ROOT();
}

/* Resets a zone that has no chunks in use as if it were newly
 * created, keeping its mapping, index, chain links and zone map
 * entry. The user pages go back to the OS and read as zero, the
 * bitmap is cleared, and the zone draws new secrets, canary
 * positions and free list order. Retiring a zone used to unmap
 * it and map a replacement, which cost several system calls and
 * a TLB shootdown, and an empty zone that never retired kept its
 * memory forever. The caller holds the zone lock */
INTERNAL_HIDDEN void reset_zone_in_place(iso_alloc_zone_t *zone) {
    void *user_pages_start = UNMASK_USER_PTR(zone);
    void *bitmap_start = UNMASK_BITMAP_PTR(zone);
    int32_t prot = (PROT_READ | PROT_WRITE);

    if(UNLIKELY(zone->af_count != 0)) {
        LOG_AND_ABORT("Cannot reset zone[%d] with %d chunks in use", zone->index, zone->af_count);
    }

#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        prot |= PROT_MTE;
    }
#endif

    UNPOISON_ZONE(zone);
    decommit_zone_pages(user_pages_start, ZONE_USER_SIZE, prot);
    __iso_memset(bitmap_start, 0x0, zone->bitmap_size);
    __iso_memset(zone->canary_pages, 0x0, sizeof(zone->canary_pages));

    zone->seed = rand_uint64();
    zone->refills = 0;
    zone->alloc_count = 0;
    zone->purge_snapshot = 0;
    zone->is_full = false;
    zone->next_free_bit_slot = BAD_BIT_SLOT;
    zone->free_bit_slots_usable = 0;
    zone->free_bit_slots_index = 0;

    uint64_t seed = zone->seed;
    zone->canary_secret = us_rand_uint64(&seed);
    const uint64_t pointer_mask = us_rand_uint64(&seed);
    zone->seed = seed;

    /* New mask, same addresses. The two pointers are rewritten
     * together with the mask they are masked by */
    zone->pointer_mask = pointer_mask;
    zone->user_pages_start = user_pages_start;
    zone->bitmap_start = bitmap_start;
    MASK_ZONE_PTRS_ONCE(zone);

    /* A new offset for the first chunk, which may change the
     * chunk count by a few */
    seed = zone->seed;
    randomize_chunk_offset(zone, &seed);
    zone->seed = seed;

#if MEMORY_TAGGING
    if(zone->tagged == true) {
        const size_t tag_mapping_size = ROUND_UP_PAGE(zone->chunk_count * MEM_TAG_SIZE);
        uint64_t *_mtp = (uint64_t *) (user_pages_start - g_page_size - tag_mapping_size);
        const uint64_t tms = (tag_mapping_size >> 3);
        seed = zone->seed;

        for(uint64_t o = 0; o < tms; o++) {
            _mtp[o] = us_rand_uint64(&seed);
        }

        zone->seed = seed;
        zone->tags_refreshed_at = 0;
    }
#endif

    create_canary_chunks(zone);
    fill_free_bit_slots(zone);
    get_next_free_bit_slot(zone);
    POISON_ZONE(zone);
}

/* One purge pass. A zone is reset when it has no chunks in use
 * and has not allocated anything since the previous pass, which
 * is read off its allocation counter so the free path keeps no
 * state for this. Zones another thread holds are skipped, they
 * are busy. Takes no lock but the zone locks, the zones array is
 * never unmapped while the allocator lives */
INTERNAL_HIDDEN void _iso_alloc_purge_idle_zones(void) {
    const size_t zones_used = _root->zones_used;

    for(size_t i = 0; i < zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];

        /* Unlocked reads as a filter, checked again under the lock.
         * A zone that allocated since the last pass only gets a
         * new snapshot, which needs no lock as the value is only
         * ever compared against alloc_count by this pass */
        const uint32_t alloc_count = zone->alloc_count;

        if(zone->af_count != 0 || alloc_count == 0 || alloc_count != zone->purge_snapshot) {
            zone->purge_snapshot = alloc_count;
            continue;
        }

        if(TRYLOCK_ZONE(zone) == false) {
            continue;
        }

        if(zone->af_count == 0 && zone->alloc_count != 0 && zone->alloc_count == zone->purge_snapshot) {
            reset_zone_in_place(zone);
        }

        UNLOCK_ZONE(zone);
    }
}

#if ZONE_PURGE_DELAY_MS > 0
/* Called from the free path after a quarantine flush. Runs a
 * purge pass when the delay has passed, one thread at a time */
static void maybe_purge_idle_zones(void) {
    const uint64_t now = coarse_now_ms();

    if(LIKELY((now - _root->last_purge_ms) < ZONE_PURGE_DELAY_MS)) {
        return;
    }

#if THREAD_SUPPORT
    if(atomic_flag_test_and_set_explicit(&_root->purging, memory_order_acquire)) {
        return;
    }
#endif

    if((now - _root->last_purge_ms) >= ZONE_PURGE_DELAY_MS) {
        _root->last_purge_ms = now;
        _iso_alloc_purge_idle_zones();
    }

#if THREAD_SUPPORT
    atomic_flag_clear_explicit(&_root->purging, memory_order_release);
#endif
}
#endif

#if UNIT_TESTING
EXTERNAL_API void _iso_alloc_purge_now(void) {
    _iso_alloc_purge_idle_zones();
}
#endif

/* Unmaps a zone and optionally replaces it in place. The caller
 * holds the root lock and the zone lock, and has made sure no
 * quarantine still holds a chunk from the zone. The replacement
 * is created with the lock still held so the slot is never seen
 * half initialized */
INTERNAL_HIDDEN void _iso_alloc_destroy_zone_unlocked(iso_alloc_zone_t *zone, bool replace) {
    void *user_pages_start = UNMASK_USER_PTR(zone);
    void *bitmap_start = UNMASK_BITMAP_PTR(zone);
    UNPOISON_ZONE(zone);

#if MEMORY_TAGGING
    /* If the zone is tagged then unmap the page holding the tags */
    if(zone->tagged == true) {
        size_t s = ROUND_UP_PAGE(zone->chunk_count * MEM_TAG_SIZE);
        void *_mtp = (user_pages_start - s - g_page_size);
        munmap(_mtp, g_page_size + s);
        zone->tagged = false;
    }
#endif

    if(UNLIKELY(iso_ptr_map_remove(&_root->zone_map, PTR_TO_ZONE_KEY(user_pages_start), NULL) == false)) {
        LOG_AND_ABORT("Zone[%d] is missing from the zone map", zone->index);
    }

    if(zone->preallocated_bitmap_idx == -1) {
        munmap(bitmap_start - g_page_size, (zone->bitmap_size + g_page_size * 2));
    } else {
        const int sbsi = (sizeof(small_bitmap_sizes) / sizeof(int)) - 1;

        for(int i = 0; i < sbsi; i++) {
            if(zone->bitmap_size == _root->bitmaps[i].bucket) {
                UNSET_BIT(_root->bitmaps[i].in_use, zone->preallocated_bitmap_idx);
                __iso_memset(bitmap_start, 0x0, zone->bitmap_size);
                break;
            }
        }
    }

    munmap(user_pages_start - g_page_size, (ZONE_USER_SIZE + g_page_size * 2));

    if(replace == true) {
        const bool was_internal = zone->internal;
        size_t size = zone->chunk_size;

        /* A private zone can hold any size and is not on a lookup
         * chain. Its replacement is an internal zone so it must
         * hold a size class and be linked, otherwise nothing
         * could ever find it and its slot would be wasted */
        if(was_internal == false) {
            size = size_class(size);
        }

        iso_alloc_zone_t *new_zone = _iso_new_zone(size, true, zone->index);

        if(was_internal == false) {
            new_zone->next_sz_index = _root->zone_lookup_table[SZ_TO_ZONE_LOOKUP_IDX(size)];
            _root->zone_lookup_table[SZ_TO_ZONE_LOOKUP_IDX(size)] = ZONE_CHAIN_LINK(new_zone->index);
        }
    }
}

INTERNAL_HIDDEN iso_alloc_zone_t *iso_new_zone(size_t size, bool internal) {
    if(size > SMALL_SIZE_MAX) {
        return NULL;
    }

    LOCK_ROOT();
    iso_alloc_zone_t *zone = _iso_new_zone(size, internal, -1);
    UNLOCK_ROOT();
    return zone;
}

INTERNAL_HIDDEN INLINE void clear_zone_cache(void) {
    iso_zone_cache_t *zc = THREAD_ZONE_CACHE();
    __iso_memset(zc, 0x0, sizeof(iso_zone_cache_t));
}

/* Picks a random offset for the first chunk of a zone and sets
 * chunk_count to the chunks that still fit. The bitmap covers
 * the chunks of an offset of 0, the bits of any chunk past the
 * new count are set to 11 so no scan can hand them out. The
 * offset keeps every chunk aligned to the largest power of two
 * that divides its size, which _iso_alloc_aligned relies on,
 * and stays below a page so at most a page is given up. The
 * caller holds the zone lock and the bitmap must be all zero */
INTERNAL_HIDDEN void randomize_chunk_offset(iso_alloc_zone_t *zone, uint64_t *seed) {
    const size_t chunk_size = zone->chunk_size;
    const size_t lowbit = (chunk_size & (~chunk_size + 1));
    const size_t unit = (lowbit > CHUNK_OFFSET_UNIT) ? lowbit : CHUNK_OFFSET_UNIT;
    const size_t covered = ((size_t) zone->max_bitmap_idx << (BITS_PER_QWORD_SHIFT - BITS_PER_CHUNK_SHIFT));
    size_t offset = 0;

    if(unit < g_page_size) {
        const size_t choices = (g_page_size / unit);
        offset = ((us_rand_uint64(seed) % choices) * unit);
    }

    size_t count = ((ZONE_USER_SIZE - offset) / chunk_size);

    if(count > covered) {
        count = covered;
    }

    zone->chunk_offset = (uint16_t) offset;
    zone->chunk_count = (uint32_t) count;

    /* Chunks the bitmap covers but the zone no longer holds
     * are marked as canaries, which nothing ever hands out */
    bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);

    for(size_t c = count; c < covered; c++) {
        const bit_slot_t bit_slot = ((bit_slot_t) c << BITS_PER_CHUNK_SHIFT);
        bm[bit_slot >> BITS_PER_QWORD_SHIFT] |= ((bitmap_index_t) 3 << WHICH_BIT(bit_slot));
    }
}

/* Select a random number of chunks to be canaries. These
 * can be verified anytime by calling check_canary()
 * or check_canary_no_abort() */
INTERNAL_HIDDEN void create_canary_chunks(iso_alloc_zone_t *zone) {
#if ENABLE_ASAN || DISABLE_CANARY
    return;
#else
    /* Canary chunks are only for default zone sizes. This
     * is because larger zones would waste a lot of memory
     * if we set aside some of their chunks as canaries */
    if(zone->chunk_size > MAX_DEFAULT_ZONE_SZ) {
        return;
    }

    bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);

    /* Only qwords whose first chunk exists, the tail past
     * chunk_count is already marked, see randomize_chunk_offset */
    const bitmap_index_t max_bitmap_idx = (zone->chunk_count >> (BITS_PER_QWORD_SHIFT - BITS_PER_CHUNK_SHIFT));

    /* Roughly %1 of the chunks in this zone will become a canary */
    const uint64_t canary_count = (zone->chunk_count >> CANARY_COUNT_DIV);

    if(UNLIKELY(max_bitmap_idx == 0 || canary_count == 0)) {
        return;
    }

    /* This function is only ever called during zone
     * initialization so we don't need to check the
     * current state of any chunks, they're all free.
     * It's possible the call to us_rand_uint64() here
     * will return the same index twice. We can live
     * with that collision as canary chunks only provide
     * a small probabilistic security guarantee */
    /* The zone struct is packed so its seed is advanced in a
     * local and written back, see fill_free_bit_slots */
    uint64_t seed = zone->seed;

    for(uint64_t i = 0; i < canary_count; i++) {
        /* Any qword in the bitmap can hold a canary. Aligning
         * this index down to SZ_ALIGNMENT restricted canaries
         * to every 32nd qword which left most zones with only
         * a handful of possible canary positions */
        const bitmap_index_t bm_idx = (us_rand_uint64(&seed) % max_bitmap_idx);

        /* We may have already chosen this index */
        if(GET_BIT(bm[bm_idx], 0)) {
            continue;
        }

        /* Set the 1st and 2nd bits as 1. The canary value is
         * not written here, that happens in materialize_canaries
         * when the free list first reaches this chunks page.
         * Writing it now would make every page of the zone
         * resident before a single chunk is handed out */
        SET_BIT(bm[bm_idx], 0);
        SET_BIT(bm[bm_idx], 1);
    }

    zone->seed = seed;
#endif
}

/* Returns true if the canary chunks on user page number page
 * of the zone have been written. The caller computes the page
 * number from the unmasked user pages pointer, this never
 * reads the zone pointers so it works on a masked zone */
INTERNAL_HIDDEN INLINE bool canary_page_materialized(const iso_alloc_zone_t *zone, size_t page) {
    return ((GET_BIT(zone->canary_pages[page >> BITS_PER_QWORD_SHIFT], (page & (BITS_PER_QWORD - 1)))) != 0);
}

/* Writes the canary value into every canary chunk whose first
 * qword lies on a user page that has not been materialized yet,
 * for all pages holding the start of a chunk in bitmap qwords
 * first_qword through last_qword inclusive. The free path
 * verifies the canaries of the chunks on either side of a chunk
 * being freed, so before a chunk can be handed out the canaries
 * of its qword and both neighbouring qwords must exist. Callers
 * pass one qword either side of the range they made allocatable */
INTERNAL_HIDDEN void materialize_canaries(iso_alloc_zone_t *zone, bitmap_index_t first_qword, bitmap_index_t last_qword) {
#if !ENABLE_ASAN && !DISABLE_CANARY
    const bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);
    const size_t chunk_size = zone->chunk_size;
    const size_t chunk_count = zone->chunk_count;

    if(first_qword < 0) {
        first_qword = 0;
    }

    if(last_qword >= zone->max_bitmap_idx) {
        last_qword = (zone->max_bitmap_idx - 1);
    }

    if(first_qword > last_qword) {
        return;
    }

    /* The first and last chunk whose start lies in the range */
    size_t first_chunk = ((size_t) first_qword << (BITS_PER_QWORD_SHIFT - BITS_PER_CHUNK_SHIFT));
    size_t last_chunk = (((size_t) last_qword + 1) << (BITS_PER_QWORD_SHIFT - BITS_PER_CHUNK_SHIFT)) - 1;

    if(last_chunk >= chunk_count) {
        last_chunk = (chunk_count - 1);
    }

    /* Pages are counted from the start of the user pages, the
     * first chunk sits chunk_offset bytes in */
    const size_t chunk_offset = zone->chunk_offset;
    const size_t first_page = ((chunk_offset + (first_chunk * chunk_size)) >> g_page_size_shift);
    const size_t last_page = ((chunk_offset + (last_chunk * chunk_size)) >> g_page_size_shift);

    for(size_t page = first_page; page <= last_page; page++) {
        uint64_t *word = &zone->canary_pages[page >> BITS_PER_QWORD_SHIFT];
        const uint64_t bit = (1ULL << (page & (BITS_PER_QWORD - 1)));

        if((*word & bit) != 0) {
            continue;
        }

        *word |= bit;

        /* Chunks whose first qword lies on this page */
        const size_t page_start = (page << g_page_size_shift);
        const size_t page_end = (page_start + g_page_size);
        size_t c = (page_start > chunk_offset) ? ((page_start - chunk_offset + chunk_size - 1) / chunk_size) : 0;
        size_t c_end = (page_end > chunk_offset) ? ((page_end - chunk_offset + chunk_size - 1) / chunk_size) : 0;

        if(c_end > chunk_count) {
            c_end = chunk_count;
        }

        for(; c < c_end; c++) {
            const bit_slot_t bit_slot = ((bit_slot_t) c << BITS_PER_CHUNK_SHIFT);
            const bitmap_index_t b = bm[bit_slot >> BITS_PER_QWORD_SHIFT];
            const uint64_t which_bit = WHICH_BIT(bit_slot);

            /* A canary chunk has both of its bits set */
            if(((b >> which_bit) & 3) == 3) {
                void *cp = POINTER_FROM_BITSLOT(zone, bit_slot);
                write_canary(zone, cp);
            }
        }
    }
#endif
}

/* Requires the root is locked */
INTERNAL_HIDDEN iso_alloc_zone_t *_iso_new_zone(size_t size, bool internal, int32_t index) {
    if(UNLIKELY(_root->zones_used >= MAX_ZONES) || UNLIKELY(index >= MAX_ZONES)) {
        LOG_AND_ABORT("Cannot allocate additional zones. I have already allocated %d zones", _root->zones_used);
    }

    if(size > SMALL_SIZE_MAX) {
        LOG("Request for new zone with %ld byte chunks should be handled by big alloc path", size);
        return NULL;
    }

    /* This is a good time to refresh the root rng seed */
    _root->seed = rand_uint64();

    /* Minimum chunk size */
    if(size < SMALLEST_CHUNK_SZ) {
        size = SMALLEST_CHUNK_SZ;
    } else if((size % SZ_ALIGNMENT) != 0) {
        size = ALIGN_SZ_UP(size);
    }

    iso_alloc_zone_t *new_zone = NULL;

    /* We created a new zone, we did not replace a retired one */
    if(index >= 0) {
        new_zone = &_root->zones[index];
    } else {
        new_zone = &_root->zones[_root->zones_used];
    }

    /* Everything but the lock is cleared. A replacement zone is
     * being reinitialized by the thread that holds its lock and
     * must stay locked throughout, see _iso_alloc_destroy_zone_unlocked */
    uint16_t next_sz_index = new_zone->next_sz_index;
    __iso_memset((uint8_t *) new_zone + ZONE_RESET_OFFSET, 0x0, sizeof(iso_alloc_zone_t) - ZONE_RESET_OFFSET);

    /* Restore next_sz_index */
    new_zone->next_sz_index = next_sz_index;

    new_zone->internal = internal;
    new_zone->is_full = false;
    new_zone->chunk_size = size;

    /* 0 is a valid bit slot, the empty value is the sentinel */
    new_zone->next_free_bit_slot = BAD_BIT_SLOT;

    /* The zones own random stream. Everything random about
     * this zone, its secrets, tags, canary positions and free
     * list order, is drawn from it */
    new_zone->seed = rand_uint64();

    new_zone->chunk_count = (ZONE_USER_SIZE / new_zone->chunk_size);

    /* Precompute a reciprocal so the free path can turn a chunk
     * offset into a chunk number with a multiply and shift. With
     * l = ceil(log2(chunk_size)), magic = ceil(2^(22+l) / chunk_size)
     * gives an exact quotient for every offset below 2^22, which is
     * ZONE_USER_SIZE (Granlund and Montgomery, Theorem 4.2) */
    static_assert(ZONE_USER_SIZE == (1 << 22), "chunk_div_magic assumes a 4MB zone");
    const uint32_t l = (32 - __builtin_clz(new_zone->chunk_size - 1));
    new_zone->chunk_div_shift = (22 + l);
    new_zone->chunk_div_magic = (uint32_t) (((1ULL << new_zone->chunk_div_shift) + new_zone->chunk_size - 1) / new_zone->chunk_size);

    /* If a caller requests an allocation that is >=(ZONE_USER_SIZE/2)
     * then we need to allocate a minimum size bitmap */
    uint32_t bitmap_size = (new_zone->chunk_count * BITS_PER_CHUNK) / BITS_PER_BYTE;
    new_zone->bitmap_size = (bitmap_size > sizeof(bitmap_index_t)) ? bitmap_size : sizeof(bitmap_index_t);
    new_zone->max_bitmap_idx = (new_zone->bitmap_size >> 3);

    /* The bitmap is whole qwords of 32 chunks each. For chunk
     * sizes that do not divide the zone evenly the division
     * above truncates, so the last few chunks of the zone had
     * no bits and could never be handed out, while the loops
     * that walk chunk_count read one qword past the bitmap.
     * Trim the count to what the bitmap covers so every chunk
     * has its two bits and every bit has its chunk. The count
     * drops a little more once the chunk offset is chosen */
    new_zone->chunk_count = (new_zone->max_bitmap_idx << (BITS_PER_QWORD_SHIFT - BITS_PER_CHUNK_SHIFT));
    new_zone->bitmap_size = (new_zone->max_bitmap_idx << 3);

    if(g_page_size >= new_zone->bitmap_size) {
        const int sbsi = (sizeof(small_bitmap_sizes) / sizeof(int)) - 1;

        for(int i = 0; i < sbsi; i++) {
            if(_root->bitmaps[i].bucket == new_zone->bitmap_size) {
                /* Easy path is the bitmap is unused */
                if(_root->bitmaps[i].in_use == 0) {
                    new_zone->bitmap_start = _root->bitmaps[i].bitmap;
                    new_zone->preallocated_bitmap_idx = 0;
                    SET_BIT(_root->bitmaps[i].in_use, 0);
                    break;
                }

                /* A page holds more small bitmaps than in_use has
                 * bits, especially with 16K pages, so cap the slice
                 * count at the width of in_use. Shifting past it
                 * is undefined and would hand out overlapping bitmaps */
                int bits_available = g_page_size / new_zone->bitmap_size;
                const int max_bits = (sizeof(_root->bitmaps[i].in_use) * BITS_PER_BYTE);

                if(bits_available > max_bits) {
                    bits_available = max_bits;
                }

                for(int z = 0; z < bits_available; z++) {
                    if((GET_BIT(_root->bitmaps[i].in_use, z)) == 0) {
                        new_zone->bitmap_start = _root->bitmaps[i].bitmap + (new_zone->bitmap_size * z);
                        new_zone->preallocated_bitmap_idx = z;
                        SET_BIT(_root->bitmaps[i].in_use, z);
                        break;
                    }
                }
            }

            if(new_zone->bitmap_start != NULL) {
                break;
            }
        }

        if(new_zone->bitmap_start == NULL) {
            new_zone->bitmap_start = mmap_guarded_rw_pages(new_zone->bitmap_size, true, ZONE_BITMAP_NAME);
            new_zone->preallocated_bitmap_idx = -1;
        }
    } else {
        new_zone->bitmap_start = mmap_guarded_rw_pages(new_zone->bitmap_size, true, ZONE_BITMAP_NAME);
        new_zone->preallocated_bitmap_idx = -1;
    }

    char *name = NULL;

#if NAMED_MAPPINGS && (__ANDROID__ || KERNEL_VERSION_SEQ_5_17)
    if(internal == true) {
        name = INTERNAL_UZ_NAME;
    } else {
        name = PRIVATE_UZ_NAME;
    }
#endif

    size_t total_size = ZONE_USER_SIZE + (g_page_size << 1);

#if MEMORY_TAGGING
    /* Each tag is 1 byte in size and the start address
     * of each valid chunk is assigned a tag */
    size_t tag_mapping_size = ROUND_UP_PAGE((new_zone->chunk_count * MEM_TAG_SIZE));

    if(internal == false) {
        total_size += (tag_mapping_size + g_page_size);
        new_zone->tagged = true;
    } else {
        tag_mapping_size = 0;
    }
#endif
    void *p = NULL;
    int32_t prot = (PROT_READ | PROT_WRITE);

#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        prot |= PROT_MTE;
    }
#endif

    /* The user pages must be aligned to their own size so the
     * zone map can name this zone from any pointer into them */
    size_t user_offset = g_page_size;

#if MEMORY_TAGGING
    if(new_zone->tagged == true) {
        user_offset = (g_page_size + tag_mapping_size + g_page_size);
    }
#endif

    p = mmap_zone_pages(total_size, user_offset, name, prot);

    if(UNLIKELY(p == NULL)) {
        LOG_AND_ABORT("Failed to map user pages for a new zone");
    }

#if(__ANDROID__ || KERNEL_VERSION_SEQ_5_17) && NAMED_MAPPINGS && MEMORY_TAGGING
    if(new_zone->tagged == false) {
        name = MEM_TAG_NAME;
    }
#endif

    void *user_pages_guard_below = p;
    create_guard_page(user_pages_guard_below);

#if MEMORY_TAGGING
    if(new_zone->tagged == true) {
        create_guard_page(p + g_page_size + tag_mapping_size);
        new_zone->user_pages_start = (p + g_page_size + tag_mapping_size + g_page_size);
        uint64_t *_mtp = p + g_page_size;
        /* (>> 3) == sizeof(uint64_t) == 8 */
        uint64_t tms = tag_mapping_size >> 3;

        /* Generate random tags */
        uint64_t seed = new_zone->seed;

        for(uint64_t o = 0; o < tms; o++) {
            _mtp[o] = us_rand_uint64(&seed);
        }

        new_zone->seed = seed;
    } else {
        new_zone->user_pages_start = (p + g_page_size);
    }
#else
    new_zone->user_pages_start = (p + g_page_size);
#endif

    void *user_pages_guard_above;

#if MEMORY_TAGGING
    if(new_zone->tagged == false) {
        user_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + (ZONE_USER_SIZE + g_page_size));
    } else {
        user_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + tag_mapping_size + (ZONE_USER_SIZE + g_page_size * 2));
    }
#else
    user_pages_guard_above = (void *) ROUND_UP_PAGE((uintptr_t) p + (ZONE_USER_SIZE + g_page_size));
#endif

    create_guard_page(user_pages_guard_above);

#if __linux__ && !HUGE_PAGES && defined(MADV_NOHUGEPAGE)
    /* Huge pages are disabled in this build. The free list
     * touches a zone sparsely, so with transparent huge pages
     * set to always the first chunk handed out would make 2 MB
     * of the zone resident. Opt the user pages out of that */
    madvise(new_zone->user_pages_start, ZONE_USER_SIZE, MADV_NOHUGEPAGE);
#endif

    /* A zone that replaces a retired one (index >= 0, including
     * index 0) keeps the index of the zone it replaces. Only a
     * brand new zone gets the next unused index */
    if(index >= 0) {
        new_zone->index = index;
    } else {
        new_zone->index = _root->zones_used;
    }

    if(UNLIKELY(((uintptr_t) new_zone->user_pages_start & (ZONE_USER_SIZE - 1)) != 0)) {
        LOG_AND_ABORT("Zone[%d] user pages at 0x%p are not aligned to the zone size", new_zone->index, new_zone->user_pages_start);
    }

    iso_ptr_map_insert(&_root->zone_map, PTR_TO_ZONE_KEY(new_zone->user_pages_start), new_zone->index);

    uint64_t seed = new_zone->seed;
    new_zone->canary_secret = us_rand_uint64(&seed);
    new_zone->pointer_mask = us_rand_uint64(&seed);
    new_zone->seed = seed;

    /* From here on the zone pointers are only ever read
     * through UNMASK_USER_PTR and UNMASK_BITMAP_PTR */
    MASK_ZONE_PTRS_ONCE(new_zone);

    seed = new_zone->seed;
    randomize_chunk_offset(new_zone, &seed);
    new_zone->seed = seed;

    create_canary_chunks(new_zone);

    /* When we create a new zone its an opportunity to
     * populate our free list cache with random entries */
    fill_free_bit_slots(new_zone);

    /* Prime the next_free_bit_slot member */
    get_next_free_bit_slot(new_zone);

#if CPU_PIN
    new_zone->cpu_core = _iso_getcpu();
#endif

    POISON_ZONE(new_zone);

    /* The lookup table is never used for private zones.
     * Replacement zones (index >= 0) inherit their predecessors
     * place in the table and its next_sz_index chain which we
     * preserved above, so only brand new zones are inserted here.
     * Table entries and next_sz_index hold the zone index plus
     * one so that 0 means empty, zone 0 used to be unreachable
     * through the table because its index was the sentinel */
    if(LIKELY(internal == true) && index < 0) {
        /* Prepend this zone to the chain for its size so it
         * is checked first on the alloc path. An empty chain
         * is a 0 entry, which is also what the new zone gets
         * as its next pointer */
        new_zone->next_sz_index = _root->zone_lookup_table[SZ_TO_ZONE_LOOKUP_IDX(size)];
        _root->zone_lookup_table[SZ_TO_ZONE_LOOKUP_IDX(size)] = ZONE_CHAIN_LINK(new_zone->index);
    }

    /* We created a new zone, we did not replace a retired one */
    if(index < 0) {
        _root->zones_used++;

        /* Lock the pages holding this zone struct so the hot
         * path never takes a soft page fault on it. Pages that
         * are already locked are unaffected */
        const uintptr_t zs = ROUND_DOWN_PAGE((uintptr_t) new_zone);
        const uintptr_t ze = ROUND_UP_PAGE((uintptr_t) (new_zone + 1));
        MLOCK((void *) zs, (ze - zs));
    }

    return new_zone;
}

/* Pick a random index in the bitmap and start looking
 * for free bit slots we can add to the cache. The random
 * bitmap index is to protect against biasing the free
 * slot cache with only chunks towards the start of the
 * user mapping. Theres no guarantee this function will
 * find any free slots. */
INTERNAL_HIDDEN void fill_free_bit_slots(iso_alloc_zone_t *zone) {
    const bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);
    const bitmap_index_t max = zone->max_bitmap_idx;

    /* The free list order comes from the zones own wyrand
     * stream. It used to reseed the shared root seed from the
     * OS on every refill, a system call per 255 allocations
     * that also wrote shared state. The stream is mixed with
     * fresh OS entropy every FREE_LIST_RESEED_REFILLS refills
     * so its state cannot be followed indefinitely */
    zone->refills++;
    uint64_t seed = zone->seed;

    if(UNLIKELY((zone->refills & (FREE_LIST_RESEED_REFILLS - 1)) == 0)) {
        seed ^= rand_uint64();
    }

    /* Start at a random qword so the free list is not biased
     * towards the start of the zone, then walk the whole bitmap
     * wrapping at the end. The walk used to stop at the end, so
     * a start near it found few or no free chunks even in a
     * zone full of them and every allocation then fell back to
     * a scan of the bitmap. If our max bitmap index is small
     * then it won't provide enough search space for a random
     * list to be of value */
    bitmap_index_t start = 0;

    if(max > MIN_BITMAP_IDX) {
        start = (bitmap_index_t) (us_rand_uint64(&seed) % (uint64_t) max);
    }

    bit_slot_cache_t *free_bit_slots = zone->free_bit_slots;
    __iso_memset(free_bit_slots, BAD_BIT_SLOT, sizeof(zone->free_bit_slots));
    zone->free_bit_slots_usable = 0;
    free_bit_slot_t free_bit_slots_index = 0;
    bitmap_index_t bm_idx = start;
    bitmap_index_t visited = 0;

    while(free_bit_slots_index < ZONE_FREE_LIST_SZ && visited < max) {
        const bitmap_index_t next = ((bm_idx + 1 == max) ? 0 : (bm_idx + 1));

        /* Runs of fully used qwords are common in a busy zone.
         * Checking two at once with a single and skips them at
         * half the cost. A 64 bit and is as good as a vector
         * compare here, there is nothing for NEON to add */
        if((visited + 1) < max && ((bm[bm_idx] & bm[next] & USED_BIT_VECTOR) == USED_BIT_VECTOR)) {
            visited += 2;
            bm_idx = ((next + 1 == max) ? 0 : (next + 1));
            continue;
        }

        const bit_slot_t bts = bm[bm_idx];
        const bitmap_index_t bm_idx_shf = bm_idx << BITS_PER_QWORD_SHIFT;

        /* If the byte is 0 then its faster to add each
         * bitslot without checking each bit value */
        if(bts == 0x0) {
            for(uint64_t z = 0; z < BITS_PER_QWORD; z += BITS_PER_CHUNK) {
                free_bit_slots[free_bit_slots_index] = (bm_idx_shf + z);
                free_bit_slots_index++;

                if(UNLIKELY(free_bit_slots_index >= ZONE_FREE_LIST_SZ)) {
                    break;
                }
            }
        } else {
            /* Use ctzll to skip directly to each free slot instead
             * of iterating all 32 even-bit positions */
            uint64_t free_mask = ~(uint64_t) bts & USED_BIT_VECTOR;

            while(free_mask) {
                free_bit_slots[free_bit_slots_index] = (bm_idx_shf + __builtin_ctzll(free_mask));
                free_bit_slots_index++;

                if(UNLIKELY(free_bit_slots_index >= ZONE_FREE_LIST_SZ)) {
                    break;
                }

                free_mask &= free_mask - 1; /* clear lowest set bit */
            }
        }

        visited++;
        bm_idx = next;
    }

#if RANDOMIZE_FREELIST
    static_assert(MIN_RAND_FREELIST >= 2, "MIN_RAND_FREELIST should be at least 2");

    /* Randomize the list of free bitslots with a Fisher-Yates
     * shuffle. Each step only consumes FREE_LIST_SHF bits so
     * one 64 bit draw from wyrand covers several steps */
    if(free_bit_slots_index > MIN_RAND_FREELIST) {
        uint64_t r = 0;
        uint32_t draws_left = 0;

        for(free_bit_slot_t i = free_bit_slots_index - 1; i > 0; i--) {
            if(draws_left == 0) {
                r = us_rand_uint64(&seed);
                draws_left = (BITS_PER_QWORD / FREE_LIST_SHF);
            }

            /* j is uniform over 0..i inclusive so every
             * permutation is equally likely */
            free_bit_slot_t j = ((free_bit_slot_t) r * (i + 1)) >> FREE_LIST_SHF;
            r >>= FREE_LIST_SHF;
            draws_left--;

            bit_slot_cache_t t = free_bit_slots[j];
            free_bit_slots[j] = free_bit_slots[i];
            free_bit_slots[i] = t;
        }
    }
#endif

    zone->free_bit_slots_index = free_bit_slots_index;
    zone->seed = seed;

    /* Every chunk in the qwords we just visited can now be
     * handed out, so the canaries around them must exist. The
     * visited range may wrap around the end of the bitmap, in
     * which case it is two ranges. The bound one past the last
     * visited qword covers the neighbour the free path checks */
    const bitmap_index_t end = (start + visited);

    if(end <= max) {
        materialize_canaries(zone, start - 1, end);
    } else {
        materialize_canaries(zone, start - 1, max - 1);
        materialize_canaries(zone, 0, end - max);
    }
}

INTERNAL_HIDDEN INLINE void insert_free_bit_slot(iso_alloc_zone_t *zone, int64_t bit_slot) {
#if VERIFY_FREE_BIT_SLOTS
    /* The cache is sorted at creation time but once we start
     * free'ing chunks we add bit_slots to it in an unpredictable
     * order. So we can't search the cache with something like
     * a binary search. This brute force search shouldn't incur
     * too much of a performance penalty as we only search starting
     * at the free_bit_slots_usable index which is updated
     * everytime we call get_next_free_bit_slot(). We do this in
     * order to detect any corruption of the cache that attempts
     * to add duplicate bit_slots which would result in iso_alloc()
     * handing out in-use chunks. The _iso_alloc() path also does
     * a check on the bitmap itself before handing out any chunks */
    const free_bit_slot_t max_cache_slots = (ZONE_FREE_LIST_SZ >> 3);

    for(free_bit_slot_t i = zone->free_bit_slots_usable; i < max_cache_slots; i++) {
        if((bit_slot_t) zone->free_bit_slots[i] == bit_slot) {
            LOG_AND_ABORT("Zone[%d] already contains bit slot %lu in cache", zone->index, bit_slot);
        }
    }
#endif

    if(zone->free_bit_slots_index >= ZONE_FREE_LIST_SZ) {
        return;
    }

    zone->free_bit_slots[zone->free_bit_slots_index] = bit_slot;
    zone->free_bit_slots_index++;
    zone->is_full = false;
}

INTERNAL_HIDDEN INLINE bit_slot_t get_next_free_bit_slot(iso_alloc_zone_t *zone) {
    if(zone->free_bit_slots_usable >= ZONE_FREE_LIST_SZ ||
       zone->free_bit_slots_usable >= zone->free_bit_slots_index) {
        return BAD_BIT_SLOT;
    }

    zone->next_free_bit_slot = zone->free_bit_slots[zone->free_bit_slots_usable];
    zone->free_bit_slots[zone->free_bit_slots_usable++] = BAD_BIT_SLOT;
    return zone->next_free_bit_slot;
}

/* Iterate through a zone bitmap a qword at
 * a time looking for empty slots */
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot(iso_alloc_zone_t *zone) {

#if USE_NEON
    const bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);
    const bitmap_index_t max = (zone->max_bitmap_idx & ~(2 - 1));

    for(int i = 0; i < max; i += 2) {
        int64x2_t im = vld1q_s64(&bm[i]);

        if(vgetq_lane_s64(im, 0) == 0x0) {
            return (i << BITS_PER_QWORD_SHIFT);
        }
        if(vgetq_lane_s64(im, 1) == 0x0) {
            return ((i + 1) << BITS_PER_QWORD_SHIFT);
        }
    }
#elif __SIZEOF_INT128__
    const __int128 *bm = (__int128 *) UNMASK_BITMAP_PTR(zone);
    const size_t max = (zone->max_bitmap_idx >> 1);
    for(size_t i = 0; i < max; i++) {
        if(bm[i] == 0x0) {
            return (i << BITS_PER_ODWORD_SHIFT);
        }
    }
#else
    const bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);
    const bitmap_index_t max = zone->max_bitmap_idx;

    /* Iterate the entire bitmap a qword at a time */
    for(bitmap_index_t i = 0; i < max; i++) {
        if(bm[i] == 0x0) {
            return (i << BITS_PER_QWORD_SHIFT);
        }
    }
#endif

    return BAD_BIT_SLOT;
}

/* This function scans an entire bitmap bit-by-bit
 * and returns the first free bit position. In a heavily
 * used zone this function may be slow to search. */
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot_slow(iso_alloc_zone_t *zone) {
    size_t max = 0;
    bitmap_index_t *bm;

#if USE_NEON
    bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);
    max = (zone->max_bitmap_idx & ~(2 - 1));

    for(bitmap_index_t i = 0; i < max; i += 2) {
        int64x2_t im = vld1q_s64(&bm[i]);

        /* Use ctzll to find the first free slot in O(1) instead of
         * scanning bit-by-bit. USED_BIT_VECTOR selects even-position
         * bits (one per chunk); inverting gives 1s where chunks are
         * free (use bitwise ops on multiple values) */
        uint64_t free_mask0 = ~(uint64_t) vgetq_lane_s64(im, 0) & USED_BIT_VECTOR;
        if(free_mask0) {
            return ((i << BITS_PER_QWORD_SHIFT) + __builtin_ctzll(free_mask0));
        }

        uint64_t free_mask1 = ~(uint64_t) vgetq_lane_s64(im, 1) & USED_BIT_VECTOR;
        if(free_mask1) {
            return (((i + 1) << BITS_PER_QWORD_SHIFT) + __builtin_ctzll(free_mask1));
        }
    }

    if(max == zone->max_bitmap_idx) {
        return BAD_BIT_SLOT;
    }
#elif __SIZEOF_INT128__
    const __int128 *ebm = (__int128 *) UNMASK_BITMAP_PTR(zone);
    max = (zone->max_bitmap_idx >> 1);

    for(size_t i = 0; i < max; i++) {
        unsigned __int128 bts = (unsigned __int128) ebm[i];

        /* Split 128-bit word into two 64-bit halves and use ctzll */
        uint64_t free_lo = ~(uint64_t) bts & USED_BIT_VECTOR;
        if(free_lo) {
            return ((i << BITS_PER_ODWORD_SHIFT) + __builtin_ctzll(free_lo));
        }

        uint64_t free_hi = ~(uint64_t) (bts >> 64) & USED_BIT_VECTOR;
        if(free_hi) {
            return ((i << BITS_PER_ODWORD_SHIFT) + 64 + __builtin_ctzll(free_hi));
        }
    }
#endif
    bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);

    for(bitmap_index_t i = max; i < zone->max_bitmap_idx; i++) {
        uint64_t free_mask = ~(uint64_t) bm[i] & USED_BIT_VECTOR;
        if(free_mask) {
            return ((i << BITS_PER_QWORD_SHIFT) + __builtin_ctzll(free_mask));
        }
    }

    return BAD_BIT_SLOT;
}

/* Returns zone with next_free_bit_slot primed if it can hand
 * out a chunk of size, otherwise NULL. The caller holds the
 * zone lock */
INTERNAL_HIDDEN FLATTEN iso_alloc_zone_t *is_zone_usable(iso_alloc_zone_t *zone, size_t size) {
#if CPU_PIN
    /* A negative value means this platform cannot report
     * the current CPU, so pinning is disabled rather than
     * every zone failing to match a core it never had */
    const int cpu = _iso_getcpu();

    if(cpu >= 0 && zone->cpu_core != cpu) {
        return NULL;
    }
#endif

    /* If the zone is full it is not usable */
    if(zone->is_full == true) {
        return NULL;
    }

    /* Internal zones only ever serve their own size class, the
     * caller passes the class. A zone used to be acceptable for
     * any request up to 1024 bytes that fit, which let the thread
     * cache put 32 byte requests into 1024 byte chunks. Private
     * zones take anything that fits, the caller chose them */
    if(zone->internal == true) {
        if(zone->chunk_size != size) {
            return NULL;
        }
    } else if(zone->chunk_size < size) {
        return NULL;
    }

    if(zone->next_free_bit_slot != BAD_BIT_SLOT) {
        return zone;
    }

    /* If the cache for this zone is empty we should
     * refill it to make future allocations faster */
    if(zone->free_bit_slots_usable >= zone->free_bit_slots_index) {
        fill_free_bit_slots(zone);
    }

    bit_slot_t bit_slot = get_next_free_bit_slot(zone);

    if(LIKELY(bit_slot != BAD_BIT_SLOT)) {
        return zone;
    }

    /* Free list failed, use a fast search */
    bit_slot = iso_scan_zone_free_slot(zone);

    if(UNLIKELY(bit_slot == BAD_BIT_SLOT)) {
        /* Fast search failed, search bit by bit */
        bit_slot = iso_scan_zone_free_slot_slow(zone);

        /* This zone may be entirely full, try the next one
         * but mark this zone full so future allocations can
         * take a faster path */
        if(bit_slot == BAD_BIT_SLOT) {
            zone->is_full = true;
            return NULL;
        }
    }

    materialize_canaries(zone, (bit_slot >> BITS_PER_QWORD_SHIFT) - 1, (bit_slot >> BITS_PER_QWORD_SHIFT) + 1);
    zone->next_free_bit_slot = bit_slot;
    return zone;
}

/* Returns a zone that can hand out a chunk of size class cls
 * with its lock held, creating one if it must. The caller holds
 * the root lock. The chain for the class is walked with try
 * locks: a zone another thread is busy in is skipped, and if
 * only busy zones remain a new zone is created up to a cap per
 * class so contending threads settle on zones of their own,
 * after which their class table keeps them there */
INTERNAL_HIDDEN iso_alloc_zone_t *acquire_zone_for_class(size_t cls) {
    uint32_t link = _root->zone_lookup_table[SZ_TO_ZONE_LOOKUP_IDX(cls)];
    const size_t zones_used = _root->zones_used;
    iso_alloc_zone_t *busy = NULL;
    uint32_t chain_length = 0;

    while(link != 0) {
        const uint32_t i = ZONE_CHAIN_INDEX(link);

        if(UNLIKELY(i >= zones_used)) {
            LOG_AND_ABORT("Zone lookup chain for chunk size %d points at zone %d but only %d zones exist", cls, i, zones_used);
        }

        iso_alloc_zone_t *zone = &_root->zones[i];

        if(UNLIKELY(zone->chunk_size != cls)) {
            LOG_AND_ABORT("Zone lookup table failed to match sizes for zone[%d](%d) for chunk size (%d)", zone->index, zone->chunk_size, cls);
        }

        if(UNLIKELY(zone->internal == false)) {
            LOG_AND_ABORT("Lookup table should never contain private zones");
        }

        chain_length++;

        /* is_full is read without the lock as a hint, the zone
         * is checked again once it is held */
        if(zone->is_full == false) {
            if(TRYLOCK_ZONE(zone)) {
                if(is_zone_usable(zone, cls) != NULL) {
                    return zone;
                }

                UNLOCK_ZONE(zone);
            } else if(busy == NULL) {
                busy = zone;
            }
        }

        link = zone->next_sz_index;
    }

    /* Every zone with room is in use by another thread. Wait
     * for one if this class already has as many zones as the
     * cap allows or no more zones can be created */
    if(busy != NULL && (chain_length >= _root->zones_per_class_cap || zones_used >= MAX_ZONES)) {
        LOCK_ZONE(busy);

        if(is_zone_usable(busy, cls) != NULL) {
            return busy;
        }

        /* It filled up while we waited */
        UNLOCK_ZONE(busy);
    }

    iso_alloc_zone_t *zone = _iso_new_zone(cls, true, -1);

    if(UNLIKELY(zone == NULL)) {
        LOG_AND_ABORT("Failed to create a zone for allocation of %zu bytes", cls);
    }

    /* Nothing else can reach a zone created under the root lock
     * yet, so this never spins */
    LOCK_ZONE(zone);

    /* This is a brand new zone, so the fast path
     * should always work. Abort if it doesn't */
    if(UNLIKELY(zone->next_free_bit_slot == BAD_BIT_SLOT)) {
        LOG_AND_ABORT("Allocated a new zone with no free bit slots");
    }

    return zone;
}

/* Hands out the chunk at bitslot. The caller holds the zone
 * lock. fresh, if given, is set when the chunk had never been
 * handed out since the zone was created or reset, which means
 * its memory is still the zeroes the kernel mapped it with */
INTERNAL_HIDDEN INLINE ASSUME_ALIGNED void *_iso_alloc_bitslot_from_zone(bit_slot_t bitslot, iso_alloc_zone_t *zone, bool *fresh) {
    const bitmap_index_t dwords_to_bit_slot = (bitslot >> BITS_PER_QWORD_SHIFT);
    const int64_t which_bit = WHICH_BIT(bitslot);

    /* The zone pointers stay masked in memory. They are
     * unmasked into locals so the allocation fast path
     * never writes the real addresses back into the zone */
    void *user_pages_start = UNMASK_USER_PTR(zone);
    bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);
    const size_t chunk_number = (bitslot >> 1);
    void *p = user_pages_start + zone->chunk_offset + (chunk_number * zone->chunk_size);
    UNPOISON_ZONE_CHUNK(zone, p);

    if(UNLIKELY(chunk_number >= zone->chunk_count)) {
        LOG_AND_ABORT("Bit slot %lu names chunk %lu of zone[%d] which holds %d chunks", bitslot, chunk_number, zone->index, zone->chunk_count);
    }

    /* Read out 64 bits from the bitmap. We will write
     * them back before we return. This reduces the
     * number of times we have to hit the bitmap page
     * which could result in a page fault */
    bitmap_index_t b = bm[dwords_to_bit_slot];

    if(UNLIKELY(p >= user_pages_start + ZONE_USER_SIZE)) {
        LOG_AND_ABORT("Allocating an address 0x%p from zone[%d], bit slot %lu %ld bytes %ld pages outside zones user pages 0x%p 0x%p",
                      p, zone->index, bitslot, p - user_pages_start + ZONE_USER_SIZE, (p - user_pages_start + ZONE_USER_SIZE) / g_page_size,
                      user_pages_start, user_pages_start + ZONE_USER_SIZE);
    }

    if(UNLIKELY((GET_BIT(b, which_bit)) != 0)) {
        LOG_AND_ABORT("Zone[%d] for chunk size %d cannot return allocated chunk at 0x%p bitmap location @ 0x%p. bit slot was %lu, bit number was %lu",
                      zone->index, zone->chunk_size, p, &bm[dwords_to_bit_slot], bitslot, which_bit);
    }

    /* This chunk was either previously allocated and free'd
     * or it's a canary chunk. In either case this means it
     * has a canary written in its first qword. Here we check
     * that canary and abort if its been corrupted */
#if !ENABLE_ASAN && !DISABLE_CANARY
    if((GET_BIT(b, (which_bit + 1))) == 1) {
        check_canary(zone, p);
        *(uint64_t *) p = 0x0;
    }
#endif

    /* Both bits clear is a chunk nothing has ever written to.
     * Canaries are only written to canary chunks and a freed
     * chunk always has its second bit set */
    if(fresh != NULL) {
        *fresh = (((b >> which_bit) & 3) == 0);
    }

    zone->af_count++;
    zone->alloc_count++;

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

/* Does not require the root is locked */
INTERNAL_HIDDEN INLINE void populate_zone_cache(iso_alloc_zone_t *zone) {
    if(UNLIKELY(zone->internal == false)) {
        return;
    }

    /* Only a zone whose chunk size is a class belongs in the
     * class table, every internal zone is created that way */
    const size_t cls = zone->chunk_size;
    const size_t idx = size_class_index(cls);

    if(LIKELY(idx != SIZE_CLASS_NONE && size_class(cls) == cls)) {
        THREAD_ZONE_CACHE()->by_class[idx] = zone;
    }
}

INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_calloc(size_t nmemb, size_t size) {
    size_t res;

    if(size < SMALLEST_CHUNK_SZ) {
        size = SMALLEST_CHUNK_SZ;
    } else if((size % SZ_ALIGNMENT) != 0) {
        size = ALIGN_SZ_UP(size);
    }

    if(UNLIKELY(__builtin_mul_overflow(nmemb, size, &res)) || UNLIKELY(res > BIG_SZ_MAX)) {
        LOG("Call to calloc() will overflow nmemb=%zu size=%zu", nmemb, size);
        return NULL;
    }

    bool fresh = false;
    void *p = _iso_alloc_core(NULL, res, &fresh);

    if(UNLIKELY(p == NULL)) {
        return NULL;
    }

#if NO_ZERO_ALLOCATIONS
    /* Without this check we would immediately segfault in
     * the call to __iso_memset() to zeroize the chunk */
    if(UNLIKELY(res == 0)) {
        return p;
    }
#endif

    /* A chunk that was never handed out, or a big zone that was
     * just mapped, is still zero from the kernel. Only memory
     * that has been used before needs clearing. The bitmap
     * records which is which so this costs no extra state */
    if(fresh == false) {
        __iso_memset(p, 0x0, res);
    }

    return p;
}

/* The lazy initialization path for a build without the
 * constructor, or for a caller that beats it. Out of line
 * and cold so _iso_alloc only carries the NULL test */
static __attribute__((cold, noinline)) void initialize_root_slow(iso_alloc_zone_t *zone) {
    LOCK_ROOT();

    if(_root == NULL) {
#if AUTO_CTOR_DTOR
        if(UNLIKELY(zone != NULL)) {
            LOG_AND_ABORT("_root was NULL but zone %p was not", zone);
        }

        g_page_size = sysconf(_SC_PAGESIZE);
        g_page_size_shift = _log2(g_page_size);
        iso_alloc_initialize_global_root();
#else
        LOG_AND_ABORT("Root never initialized!");
#endif
    }

    UNLOCK_ROOT();
}

INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_alloc(iso_alloc_zone_t *zone, size_t size) {
    return _iso_alloc_core(zone, size, NULL);
}

/* The allocation path. fresh, if given, is set when the memory
 * returned is known to be zero already, see _iso_calloc */
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_alloc_core(iso_alloc_zone_t *zone, size_t size, bool *fresh) {
    if(fresh != NULL) {
        *fresh = false;
    }

    /* The thread state is read before the lock and needs the
     * root, so the root is created first on the rare path
     * where the constructor has not run */
    if(UNLIKELY(_root == NULL)) {
        initialize_root_slow(zone);
    }

#if NO_ZERO_ALLOCATIONS
    if(UNLIKELY(size == 0)) {
        return _root->zero_alloc_page;
    }
#endif

    if(size < SMALLEST_CHUNK_SZ) {
        size = SMALLEST_CHUNK_SZ;
    } else if((size % SZ_ALIGNMENT) != 0) {
        size = ALIGN_SZ_UP(size);
    }

    if(UNLIKELY(zone && size > zone->chunk_size)) {
        LOG_AND_ABORT("Private zone %d cannot hold chunks of size %d, only %d", zone->index, size, zone->chunk_size);
    }

    /* The request maps to one size class and the thread cache
     * remembers the last zone used for it. Only thread local
     * data is read here, the zone is validated under its lock
     * by is_zone_usable */
    iso_alloc_zone_t *cached_zone = NULL;
    size_t cls = size;

    if(LIKELY(zone == NULL && size <= SMALL_SIZE_MAX)) {
        cls = size_class(size);
        const size_t idx = size_class_index(cls);

        if(LIKELY(idx != SIZE_CLASS_NONE)) {
            cached_zone = THREAD_ZONE_CACHE()->by_class[idx];
        }
    }

    /* Allocation requests of SMALL_SIZE_MAX bytes or larger are
     * handled by the 'big allocation' path. */
    if(LIKELY(size <= SMALL_SIZE_MAX)) {
#if ALLOC_SANITY
        /* We only sample if a zone was not directly passed */
        if(zone == NULL && size < g_page_size && _sane_sampled < MAX_SANE_SAMPLES) {
            LOCK_ROOT();

            /* If we chose to sample this allocation then
             * _iso_alloc_sample will call UNLOCK_ROOT() */
            void *ps = _iso_alloc_sample(size);

            if(ps != NULL) {
                /* The sampled page carries a canary pattern */
                return ps;
            }

            UNLOCK_ROOT();
        }
#endif

#if HEAP_PROFILER
        LOCK_ROOT();
        _iso_alloc_profile(size);
        UNLOCK_ROOT();
#endif

#if FUZZ_MODE
        LOCK_ROOT();
        _verify_all_zones();
        UNLOCK_ROOT();
#endif

        if(LIKELY(zone == NULL)) {
            /* Hot Path: the zone this thread last used for the class.
             * Only its own lock is taken. If another thread holds it
             * or it is full we go through the root lock to find or
             * create another zone, and it becomes the cached one */
            if(cached_zone != NULL && TRYLOCK_ZONE(cached_zone)) {
                if(is_zone_usable(cached_zone, cls) != NULL) {
                    zone = cached_zone;
                } else {
                    UNLOCK_ZONE(cached_zone);
                }
            }

            if(zone == NULL) {
                LOCK_ROOT();
                zone = acquire_zone_for_class(cls);
                UNLOCK_ROOT();
            }
        } else {
            /* A private zone chosen by the caller. It is never
             * cached and must be checked for room */
            LOCK_ZONE(zone);

            if(is_zone_usable(zone, size) == NULL) {
                UNLOCK_ZONE(zone);
#if ABORT_ON_NULL
                LOG_AND_ABORT("isoalloc configured to abort on NULL");
#endif
                return NULL;
            }
        }

        const bit_slot_t free_bit_slot = zone->next_free_bit_slot;

        if(UNLIKELY(free_bit_slot == BAD_BIT_SLOT)) {
            UNLOCK_ZONE(zone);
#if ABORT_ON_NULL
            LOG_AND_ABORT("isoalloc configured to abort on NULL");
#endif
            return NULL;
        }

        zone->next_free_bit_slot = BAD_BIT_SLOT;
        void *p = _iso_alloc_bitslot_from_zone(free_bit_slot, zone, fresh);
        UNLOCK_ZONE(zone);
        populate_zone_cache(zone);

#if ARM_MTE
        if(_root->arm_mte_enabled == true) {
            return iso_mte_set_tag_range(p, zone->chunk_size);
        }
#endif
        return p;
    } else {
        if(UNLIKELY(zone != NULL)) {
            LOG_AND_ABORT("Allocation size of %d is > %d and cannot use a private zone (%d)", size, SMALL_SIZE_MAX, zone->chunk_size);
        }

#if HEAP_PROFILER
        LOCK_ROOT();
        _iso_alloc_profile(size);
        UNLOCK_ROOT();
#endif

        return _iso_big_alloc(size, g_page_size, fresh);
    }
}

/* Allocates a chunk aligned to alignment bytes, which must be
 * a power of 2. Every chunk is at least SZ_ALIGNMENT aligned.
 * Stronger alignments are satisfied by rounding the size up to
 * a multiple of the alignment and allocating from an internal
 * zone whose chunk size is exactly that. Chunks are laid out
 * at multiples of the chunk size from a page aligned base so
 * this guarantees the alignment for anything up to a page.
 * Larger alignments and sizes use the big zone path */
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_alloc_aligned(size_t size, size_t alignment) {
    if(UNLIKELY(is_pow2(alignment) == false)) {
        return NULL;
    }

    /* Every chunk is already SZ_ALIGNMENT aligned, and any
     * smaller power of 2 divides SZ_ALIGNMENT */
    if(alignment <= SZ_ALIGNMENT) {
        return _iso_alloc(NULL, size);
    }

    if(size == 0) {
        size = alignment;
    }

    const size_t s = ((size + (alignment - 1)) & ~(alignment - 1));

    if(UNLIKELY(s < size)) {
        return NULL;
    }

    if(UNLIKELY(_root == NULL)) {
        initialize_root_slow(NULL);
    }

#if HEAP_PROFILER
    LOCK_ROOT();
    _iso_alloc_profile(s);
    UNLOCK_ROOT();
#endif

    if(alignment > g_page_size || s > SMALL_SIZE_MAX) {
        /* Big zones must be at least SMALL_SIZE_MAX bytes. That
         * is a power of 2 so rounding up to it keeps the size a
         * multiple of any smaller alignment */
        return _iso_big_alloc((s < SMALL_SIZE_MAX) ? SMALL_SIZE_MAX : s, alignment, NULL);
    }

    /* s is a multiple of a power of two alignment no larger
     * than a page, which is always a size class, so aligned
     * requests share zones with plain ones of that class */
    LOCK_ROOT();
    iso_alloc_zone_t *zone = acquire_zone_for_class(s);
    UNLOCK_ROOT();

    const bit_slot_t free_bit_slot = zone->next_free_bit_slot;

    if(UNLIKELY(free_bit_slot == BAD_BIT_SLOT)) {
        UNLOCK_ZONE(zone);
#if ABORT_ON_NULL
        LOG_AND_ABORT("isoalloc configured to abort on NULL");
#endif
        return NULL;
    }

    zone->next_free_bit_slot = BAD_BIT_SLOT;
    void *p = _iso_alloc_bitslot_from_zone(free_bit_slot, zone, NULL);
    UNLOCK_ZONE(zone);
    populate_zone_cache(zone);

#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        return iso_mte_set_tag_range(p, zone->chunk_size);
    }
#endif
    return p;
}

/* Returns the zone that holds the bitmap p points into, or
 * NULL. Only used by the experimental stack search so it
 * simply walks every zone */
INTERNAL_HIDDEN iso_alloc_zone_t *iso_find_zone_bitmap_range(const void *restrict p) {
    const size_t zones_used = _root->zones_used;

    for(size_t i = 0; i < zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];
        const void *bitmap_start = UNMASK_BITMAP_PTR(zone);

        if(bitmap_start <= p && (bitmap_start + zone->bitmap_size) > p) {
            return zone;
        }
    }

    return NULL;
}

/* Returns the zone whose user pages hold p, or NULL if no zone
 * does. One probe of the zone map answers it: user pages are
 * aligned to ZONE_USER_SIZE so the window p lies in is the key
 * of exactly one zone. The lookup takes no lock, see
 * iso_ptr_map_find. This used to hash the pointer into a table
 * without a key check and fall back to a scan of every zone,
 * which half of all pointers took because a zone straddled
 * two windows and only one was in the table */
INTERNAL_HIDDEN iso_alloc_zone_t *iso_find_zone_range(void *restrict p) {
#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        p = iso_mte_untag_ptr(p);
    }
#endif
    uint64_t index = 0;

    if(iso_ptr_map_find(&_root->zone_map, PTR_TO_ZONE_KEY(p), &index) == false) {
        return NULL;
    }

    if(UNLIKELY(index >= _root->zones_used)) {
        LOG_AND_ABORT("Zone map entry for 0x%p names zone %lu but only %d zones exist", p, index, _root->zones_used);
    }

    iso_alloc_zone_t *zone = &_root->zones[index];
    const void *user_pages_start = UNMASK_USER_PTR(zone);

    if(LIKELY(user_pages_start <= p && (user_pages_start + ZONE_USER_SIZE) > p)) {
        return zone;
    }

    LOG_AND_ABORT("Zone map entry for 0x%p names zone[%d] whose user pages start at 0x%p", p, zone->index, user_pages_start);
}

/* Checking canaries under ASAN mode is not trivial. ASAN
 * provides a strong guarantee that these chunks haven't
 * been modified in some way */
#if ENABLE_ASAN || DISABLE_CANARY
INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone_t *big) {
    return;
}

INTERNAL_HIDDEN int64_t check_canary_no_abort(iso_alloc_zone_t *zone, const void *p) {
    return OK;
}

INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone_t *zone, void *p) {
    return;
}

/* Verify the canary value in an allocation */
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone_t *zone, const void *p) {
    return;
}
#else
/* Verifies both canaries in a big zone structure. This
 * is a fast operation so we call it anytime we iterate
 * through the linked list of big zones */
INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone_t *big) {
    const uint64_t canary = ((uint64_t) big ^ __builtin_bswap64((uint64_t) big->user_pages_start) ^ _root->big_zone_canary_secret);

    if(UNLIKELY(big->canary_a != canary)) {
        LOG_AND_ABORT("Big zone 0x%p bottom canary has been corrupted! Value: 0x%x Expected: 0x%x", big, big->canary_a, canary);
    }

    if(UNLIKELY(big->canary_b != canary)) {
        LOG_AND_ABORT("Big zone 0x%p top canary has been corrupted! Value: 0x%x Expected: 0x%x", big, big->canary_a, canary);
    }
}

INTERNAL_HIDDEN int64_t check_canary_no_abort(iso_alloc_zone_t *zone, const void *p) {
    uint64_t v = *((uint64_t *) p);
    const uint64_t canary = (zone->canary_secret ^ (uint64_t) p) & CANARY_VALIDATE_MASK;

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

/* All free chunks get a canary written at both
 * the start and end of their chunks. These canaries
 * are verified when adjacent chunks are allocated,
 * freed, or when the API requests validation. We
 * sacrifice the high byte in entropy to prevent
 * unbounded string reads from leaking it */
INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone_t *zone, void *p) {
    const uint64_t canary = (zone->canary_secret ^ (uint64_t) p) & CANARY_VALIDATE_MASK;
    *(uint64_t *) p = canary;
    p += (zone->chunk_size - sizeof(uint64_t));
    *(uint64_t *) p = canary;
}

/* Verify the canary value in an allocation */
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone_t *zone, const void *p) {
    uint64_t v = *((uint64_t *) p);
    const uint64_t canary = (zone->canary_secret ^ (uint64_t) p) & CANARY_VALIDATE_MASK;

    if(UNLIKELY(v != canary)) {
        LOG_AND_ABORT("Canary at beginning of chunk 0x%p in zone[%d][%d byte chunks] has been corrupted! Value: 0x%x Expected: 0x%x",
                      p, zone->index, zone->chunk_size, v, canary);
    }

    v = *((uint64_t *) (p + zone->chunk_size - sizeof(uint64_t)));

    if(UNLIKELY(v != canary)) {
        LOG_AND_ABORT("Canary at end of chunk 0x%p in zone[%d][%d byte chunks] has been corrupted! Value: 0x%x Expected: 0x%x",
                      p, zone->index, zone->chunk_size, v, canary);
    }
}
#endif

INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone_t *zone, void *restrict p, bool permanent) {
#if ARM_MTE
    const uint64_t chunk_offset = (uint64_t) ((iso_mte_untag_ptr(p)) - UNMASK_USER_PTR(zone)) - zone->chunk_offset;
#else
    const uint64_t chunk_offset = (uint64_t) (p - UNMASK_USER_PTR(zone)) - zone->chunk_offset;
#endif
    const size_t chunk_size = zone->chunk_size;
    /* chunk_offset / chunk_size without a divide. The callers
     * verified p lies within the zones user pages so the offset
     * is below ZONE_USER_SIZE and the reciprocal is exact. A p
     * below the first chunk wraps to a huge offset and fails the
     * chunk count check */
    const size_t chunk_number = ((chunk_offset * zone->chunk_div_magic) >> zone->chunk_div_shift);
    const bit_slot_t bit_slot = (chunk_number << BITS_PER_CHUNK_SHIFT);
    const bit_slot_t dwords_to_bit_slot = (bit_slot >> BITS_PER_QWORD_SHIFT);

    if(UNLIKELY(chunk_offset >= ZONE_USER_SIZE || chunk_number >= zone->chunk_count)) {
        LOG_AND_ABORT("Chunk 0x%p is not one of the %d chunks of zone[%d]", p, zone->chunk_count, zone->index);
    }

    /* Ensure the pointer is a multiple of chunk size. */
    if(UNLIKELY((chunk_number * chunk_size) != chunk_offset)) {
        LOG_AND_ABORT("Chunk %d at 0x%p is not a multiple of zone[%d] chunk size %d. Off by %lu bits",
                      chunk_offset, p, zone->index, chunk_size, (chunk_offset & (chunk_size - 1)));
    }

    /* max_bitmap_idx is a count of qwords in the bitmap so the
     * largest valid index is max_bitmap_idx - 1 */
    if(UNLIKELY(dwords_to_bit_slot >= zone->max_bitmap_idx)) {
        LOG_AND_ABORT("Cannot calculate this chunks location in the bitmap 0x%p", p);
    }

    uint64_t which_bit = WHICH_BIT(bit_slot);
    bitmap_index_t *bm = (bitmap_index_t *) UNMASK_BITMAP_PTR(zone);

    /* Read out 64 bits from the bitmap. We will write
     * them back before we return. This reduces the
     * number of times we have to hit the bitmap page
     * which could result in a page fault */
    bitmap_index_t b = bm[dwords_to_bit_slot];

    /* Double free detection */
    if(UNLIKELY((GET_BIT(b, which_bit)) == 0)) {
        LOG_AND_ABORT("Double free of chunk 0x%p detected from zone[%d] dwords_to_bit_slot=%lu bit_slot=%lu",
                      p, zone->index, dwords_to_bit_slot, bit_slot);
    }

    /* Set the next bit so we know this chunk was used */
    SET_BIT(b, (which_bit + 1));

    /* Unset the bit and write the value into the bitmap
     * if this is not a permanent free. A permanent free
     * means this chunk will be marked as if it is a canary */
    if(LIKELY(permanent == false)) {
        UNSET_BIT(b, which_bit);
        insert_free_bit_slot(zone, bit_slot);
#if !ENABLE_ASAN && SANITIZE_CHUNKS
        __iso_memset(p, POISON_BYTE, chunk_size);
#endif
    } else {
        __iso_memset(p, POISON_BYTE, chunk_size);
    }

    bm[dwords_to_bit_slot] = b;

    zone->af_count--;

    /* Now that we have free'd this chunk lets validate the
     * chunks before and after it. If they were previously
     * used and currently free they should have canaries
     * we can verify */
#if !ENABLE_ASAN && !DISABLE_CANARY
    write_canary(zone, p);

    if((chunk_number + 1) != zone->chunk_count) {
        const bit_slot_t bit_slot_over = ((chunk_number + 1) << BITS_PER_CHUNK_SHIFT);
        if((GET_BIT(bm[(bit_slot_over >> BITS_PER_QWORD_SHIFT)], (WHICH_BIT(bit_slot_over) + 1))) == 1) {
            check_canary(zone, p + chunk_size);
        }
    }

    if(chunk_number != 0) {
        const bit_slot_t bit_slot_under = ((chunk_number - 1) << BITS_PER_CHUNK_SHIFT);
        if((GET_BIT(bm[(bit_slot_under >> BITS_PER_QWORD_SHIFT)], (WHICH_BIT(bit_slot_under) + 1))) == 1) {
            check_canary(zone, p - chunk_size);
        }
    }
#endif

    POISON_ZONE_CHUNK(zone, p);
}

INTERNAL_HIDDEN void _iso_free_from_zone(void *p, iso_alloc_zone_t *zone, bool permanent) {
    if(p == NULL || zone == NULL) {
        return;
    }

#if ALLOC_SANITY
    /* A sampled chunk lives in its own guarded page and
     * not in any zone, so this must run before we validate
     * p against the zones user pages */
    int32_t r = _iso_alloc_free_sane_sample(p);

    if(r == OK) {
        return;
    }
#endif

#if MEMORY_TAGGING
    /* Its possible that we were passed a tagged pointer */
    if(UNLIKELY(zone->tagged == true && ((uintptr_t) p & IS_TAGGED_PTR_MASK) != 0)) {
        /* If the untagging results in a bad pointer we
         * will catch it in the free path and abort */
        p = _untag_ptr(p, zone);
    }
#endif

    /* The caller chose this zone so we can't trust that p
     * belongs to it. The bitmap index derived from p is only
     * meaningful if p is within this zones user pages */
    const void *user_pages_start = UNMASK_USER_PTR(zone);
#if ARM_MTE
    const void *up = iso_mte_untag_ptr(p);
#else
    const void *up = p;
#endif

    if(UNLIKELY(up < user_pages_start || up >= (user_pages_start + ZONE_USER_SIZE))) {
        LOG_AND_ABORT("Chunk at 0x%p does not belong to zone[%d] for chunk size %d", p, zone->index, zone->chunk_size);
    }

    _iso_free_internal_unlocked(p, permanent, zone, false);
}

INTERNAL_HIDDEN void flush_caches(void) {
    /* The thread zone cache can be invalidated
     * and does not require a lock */
    clear_zone_cache();

    iso_quarantine_t *q = THREAD_QUARANTINE();

    LOCK_QUARANTINE(q);
    flush_quarantine(q, false);
    UNLOCK_QUARANTINE(q);
}

/* Frees every chunk in the quarantine. The caller holds the
 * quarantine lock and says whether it holds the root lock,
 * which the free path must then not take again */
INTERNAL_HIDDEN void flush_quarantine(iso_quarantine_t *q, bool root_held) {
    const uint32_t count = q->count;

    for(uint32_t i = 0; i < count; i++) {
        _iso_free_internal_unlocked((void *) q->chunks[i], false, NULL, root_held);
    }

    __iso_memset(q->chunks, 0x0, sizeof(q->chunks));
    q->count = 0;
}

/* Flushes the quarantine of every thread. Called with the
 * root lock held before a zone is unmapped, otherwise a
 * chunk from that zone could surface later from another
 * threads quarantine and look like a foreign pointer */
INTERNAL_HIDDEN void flush_all_quarantines(void) {
#if THREAD_SUPPORT
    LOCK_THREAD_REGISTRY();

    for(iso_quarantine_t *q = _root->quarantines; q != NULL; q = q->next) {
        LOCK_QUARANTINE(q);
        flush_quarantine(q, true);
        UNLOCK_QUARANTINE(q);
    }

    UNLOCK_THREAD_REGISTRY();
#else
    flush_quarantine(THREAD_QUARANTINE(), true);
#endif
}

INTERNAL_HIDDEN void _iso_free(void *p, bool permanent) {
    if(p == NULL) {
        return;
    }

    /* All pointers to chunks should be 8 byte aligned.
     * This is true whether its big zone or not */
    if(UNLIKELY(IS_ALIGNED((uintptr_t) p) != 0)) {
        LOG_AND_ABORT("Chunk at 0x%p is not %d byte aligned", p, SZ_ALIGNMENT);
    }

#if NO_ZERO_ALLOCATIONS
    if(UNLIKELY(p == _root->zero_alloc_page)) {
        return;
    }
#endif

#if ALLOC_SANITY
    int32_t r = _iso_alloc_free_sane_sample(p);

    if(r == OK) {
        return;
    }
#endif

#if HEAP_PROFILER
    /* The alloc side profiles with the root locked. The
     * free profiler updates the same globals so it needs
     * the same protection or racing frees can push the
     * backtrace count past the end of the array */
    LOCK_ROOT();
    _iso_free_profile();
    UNLOCK_ROOT();
#endif

#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        /* We want to catch immediate use-after-free without waiting
         * for chunks to be free'd from the quarantine so we set a new
         * random tag for the first 16 byte granule at this address */
        p = (void *) iso_mte_create_tag(p, (1ULL << iso_mte_extract_tag(p)));
        iso_mte_set_tag(p);
    }
#endif

    if(UNLIKELY(permanent == true)) {
        _iso_free_internal(p, permanent);
        return;
    }

    iso_quarantine_t *q = THREAD_QUARANTINE();

    /* Only this thread ever grows the count and only a
     * flush shrinks it, so a full reading here is reliable
     * and a not full reading stays true until we push */
    if(UNLIKELY(q->count >= CHUNK_QUARANTINE_SZ)) {
        LOCK_QUARANTINE(q);
        flush_quarantine(q, false);
        UNLOCK_QUARANTINE(q);

#if ZONE_PURGE_DELAY_MS > 0
        /* Once per flush is often enough to check the clock */
        maybe_purge_idle_zones();
#endif
    }

    LOCK_QUARANTINE(q);
    q->chunks[q->count] = (uintptr_t) p;
    q->count++;
    UNLOCK_QUARANTINE(q);
}

INTERNAL_HIDDEN void _iso_free_size(void *p, size_t size) {
    if(p == NULL) {
        return;
    }

#if NO_ZERO_ALLOCATIONS
    if(UNLIKELY(p == _root->zero_alloc_page && size != 0)) {
        LOG_AND_ABORT("Zero sized chunk (0x%p) with non-zero (%d) size passed to free", p, size);
    }

    if(p == _root->zero_alloc_page) {
        return;
    }
#endif

#if ALLOC_SANITY
    int32_t r = _iso_alloc_free_sane_sample(p);

    if(r == OK) {
        return;
    }
#endif

    if(UNLIKELY(size > SMALL_SIZE_MAX)) {
        iso_alloc_big_zone_t *big_zone = iso_find_big_zone(p, true);

        if(UNLIKELY(big_zone == NULL)) {
#if ABORT_ON_UNOWNED_PTR
            LOG_AND_ABORT("Could not find any zone for allocation at 0x%p", p);
#else
            return;
#endif
        }

        if(UNLIKELY(big_zone->size < size)) {
            LOG_AND_ABORT("Invalid big zone size (expected %d, got %d) for chunk 0x%p", big_zone->size, size, p);
        }

        iso_free_big_zone(big_zone, false);
        return;
    }

    iso_alloc_zone_t *zone = iso_find_zone_range(p);

    if(UNLIKELY(zone == NULL)) {
#if ABORT_ON_UNOWNED_PTR
        LOG_AND_ABORT("Could not find zone for 0x%p", p);
#else
        return;
#endif
    }

    /* The chunk is at least as large as the request it
     * served, its class may be larger */
    if(UNLIKELY(zone->chunk_size < size)) {
        LOG_AND_ABORT("Invalid size (expected %d, got %d) for chunk 0x%p", zone->chunk_size, size, p);
    }

    _iso_free_internal_unlocked(p, false, zone, false);
}

INTERNAL_HIDDEN void _iso_free_internal(void *p, bool permanent) {
    iso_alloc_zone_t *zone = _iso_free_internal_unlocked(p, permanent, NULL, false);

    if(zone != NULL) {
        populate_zone_cache(zone);
    }
}

INTERNAL_HIDDEN bool _is_zone_retired(iso_alloc_zone_t *zone) {
    /* If the zone has no active allocations, holds smaller chunks,
     * and has allocated and freed more than ZONE_ALLOC_RETIRE
     * chunks in its lifetime then we destroy and replace it with
     * a new zone */
    if(UNLIKELY(zone->af_count == 0 && zone->alloc_count > (zone->chunk_count << _root->zone_retirement_shf))) {
        if(zone->internal == true && zone->chunk_size < (MAX_DEFAULT_ZONE_SZ * 2)) {
            return true;
        }
    }

    return false;
}

/* Frees p. zone is the zone that holds p if the caller knows
 * it, otherwise it is looked up. root_held says whether the
 * caller holds the root lock, which is only needed to retire a
 * zone or run the pointer search and is taken here if not. The
 * zone lock is taken for the free itself, see LOCK_ZONE */
INTERNAL_HIDDEN iso_alloc_zone_t *_iso_free_internal_unlocked(void *p, bool permanent, iso_alloc_zone_t *zone, bool root_held) {
#if FUZZ_MODE
    if(root_held == true) {
        _verify_all_zones();
    } else {
        LOCK_ROOT();
        _verify_all_zones();
        UNLOCK_ROOT();
    }
#endif

    if(LIKELY(zone == NULL)) {
        zone = iso_find_zone_range(p);
    }

    if(LIKELY(zone != NULL)) {
        LOCK_ZONE(zone);

        /* The lookup took no lock. A zone cannot be retired while
         * it holds a live chunk, so a mismatch here means p was
         * not a live chunk of this zone */
        const void *user_pages_start = UNMASK_USER_PTR(zone);
#if ARM_MTE
        const void *up = (_root->arm_mte_enabled == true) ? iso_mte_untag_ptr(p) : p;
#else
        const void *up = p;
#endif

        if(UNLIKELY(up < user_pages_start || up >= (user_pages_start + ZONE_USER_SIZE))) {
            LOG_AND_ABORT("Chunk at 0x%p does not belong to zone[%d] for chunk size %d", p, zone->index, zone->chunk_size);
        }

        iso_free_chunk_from_zone(zone, p, permanent);

        /* If the zone has no active allocations, holds smaller chunks,
         * and has allocated and freed more than ZONE_ALLOC_RETIRE
         * chunks in its lifetime then we destroy and replace it with
         * a new zone */
        bool retire = _is_zone_retired(zone);

#if MEMORY_TAGGING
        /* If there are no chunks allocated but this zone has seen
         * %25 of ZONE_ALLOC_RETIRE in allocations we wipe the pointer
         * tags and start fresh. If the whole zone doesn't need to
         * be refreshed then just generate a new tag for this chunk */
        if(zone->tagged == true) {
            if(_refresh_zone_mem_tags(zone) == false) {
                /* We only need to refresh this single tag. The
                 * new tag is guaranteed to differ from the old one
                 * so a stale tagged pointer to p can never verify */
                _iso_alloc_refresh_chunk_tag(p, zone);
            }
        }
#endif

#if UAF_PTR_PAGE
        uint64_t seed = zone->seed;
        const bool search = ((us_rand_uint64(&seed) % UAF_PTR_PAGE_ODDS) == 1);
        zone->seed = seed;
#endif

        /* A retired zone is reset in place. That touches only the
         * zone and its pages so the zone lock is all it needs */
        if(UNLIKELY(retire == true)) {
            reset_zone_in_place(zone);
        }

        UNLOCK_ZONE(zone);

#if UAF_PTR_PAGE
        if(UNLIKELY(search == true)) {
            /* The search reads every zone so it runs under the root
             * lock, which keeps zones from being replaced under it */
            if(root_held == true) {
                _iso_alloc_ptr_search(p);
            } else {
                LOCK_ROOT();
                _iso_alloc_ptr_search(p);
                UNLOCK_ROOT();
            }
        }
#endif

#if ARM_MTE
        if(_root->arm_mte_enabled == true) {
            p = iso_mte_set_tag_range(p, zone->chunk_size);
        }
#endif
        return zone;
    } else {
        iso_alloc_big_zone_t *big_zone = iso_find_big_zone(p, true);

        if(UNLIKELY(big_zone == NULL)) {
#if ABORT_ON_UNOWNED_PTR
            LOG_AND_ABORT("Could not find any zone for allocation at 0x%p", p);
#else
            return NULL;
#endif
        }

        iso_free_big_zone(big_zone, permanent);
        return NULL;
    }
}

/* Finds a big zone in the used list and optionally removes it */
/* The key a big zone has in big_zone_map, its untagged first page */
static INLINE uint64_t big_zone_key(const void *user_pages_start) {
#if ARM_MTE
    return ((uintptr_t) user_pages_start & TAGGED_PTR_MASK);
#else
    return (uintptr_t) user_pages_start;
#endif
}

/* Pushes a big zone onto the head of the used list and into
 * the map. The caller holds the big zone used lock */
static void big_zone_used_push(iso_alloc_big_zone_t *big) {
    iso_alloc_big_zone_t *head = _root->big_zone_used;

    big->prev = NULL;
    big->next = head;

    if(head != NULL) {
        UNMASK_BIG_ZONE_NEXT(head)->prev = MASK_BIG_ZONE_NEXT(big);
    }

    _root->big_zone_used = MASK_BIG_ZONE_NEXT(big);
    _root->big_zone_used_count++;
    iso_ptr_map_insert(&_root->big_zone_map, big_zone_key(big->user_pages_start), (uint64_t) MASK_BIG_ZONE_NEXT(big));
}

/* Unlinks a big zone from the used list and the map. The
 * caller holds the big zone used lock */
static void big_zone_used_unlink(iso_alloc_big_zone_t *big) {
    if(big->prev != NULL) {
        UNMASK_BIG_ZONE_NEXT(big->prev)->next = big->next;
    } else {
        _root->big_zone_used = big->next;
    }

    if(big->next != NULL) {
        UNMASK_BIG_ZONE_NEXT(big->next)->prev = big->prev;
    }

    big->next = NULL;
    big->prev = NULL;
    _root->big_zone_used_count--;

    if(UNLIKELY(iso_ptr_map_remove(&_root->big_zone_map, big_zone_key(big->user_pages_start), NULL) == false)) {
        LOG_AND_ABORT("Big zone 0x%p is missing from the big zone map", big->user_pages_start);
    }
}

/* Finds the big zone whose user pages start at p, optionally
 * removing it from the used list. One map probe replaces a
 * walk of every live big zone with a canary check per node */
INTERNAL_HIDDEN iso_alloc_big_zone_t *iso_find_big_zone(void *p, bool remove) {
#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        p = iso_mte_untag_ptr(p);
    }
#endif
    LOCK_BIG_ZONE_USED();

    uint64_t value = 0;

    if(iso_ptr_map_find(&_root->big_zone_map, big_zone_key(p), &value) == false) {
        UNLOCK_BIG_ZONE_USED();
        return NULL;
    }

    iso_alloc_big_zone_t *big_zone = UNMASK_BIG_ZONE_NEXT((iso_alloc_big_zone_t *) value);
    check_big_canary(big_zone);

    /* Only an exact match of the address is valid */
    if(UNLIKELY(big_zone_key(big_zone->user_pages_start) != big_zone_key(p))) {
        LOG_AND_ABORT("Big zone map entry for 0x%p names a zone at 0x%p", p, big_zone->user_pages_start);
    }

    if(remove == true) {
        big_zone_used_unlink(big_zone);
    }

    UNLOCK_BIG_ZONE_USED();
    return big_zone;
}

INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone_t *big_zone, bool permanent) {
    if(UNLIKELY(big_zone->free == true)) {
        LOG_AND_ABORT("Double free of big zone 0x%p has been detected!", big_zone);
    }

#if !ENABLE_ASAN && SANITIZE_CHUNKS
    __iso_memset(big_zone->user_pages_start, POISON_BYTE, big_zone->size);
#endif
    /* If this isn't a permanent free then all we need
     * to do is sanitize the mapping and mark it free.
     * The pages that back the big zone can be reused
     * if we aren't at max entries in the free list */
    LOCK_BIG_ZONE_FREE();

    if(LIKELY(permanent == false) && _root->big_zone_free_count < BIG_ZONE_MAX_FREE_LIST &&
       big_zone->ttl < BIG_ZONE_ALLOC_RETIRE) {
        POISON_BIG_ZONE(big_zone);
        big_zone->free = true;
        dont_need_pages(big_zone->user_pages_start, big_zone->size);

#if PROTECT_FREE_BIG_ZONES
        mprotect_pages(big_zone->user_pages_start, big_zone->size, PROT_NONE);
#endif

        big_zone->prev = NULL;
        big_zone->next = _root->big_zone_free;
        _root->big_zone_free = MASK_BIG_ZONE_NEXT(big_zone);
        _root->big_zone_free_count++;
        UNLOCK_BIG_ZONE_FREE();
        return;
    }

    UNLOCK_BIG_ZONE_FREE();

    /* Our zone is already removed from the list via find_big_zone() */
    if(UNLIKELY(permanent == true)) {
        big_zone->free = true;
        mprotect_pages(big_zone->user_pages_start, big_zone->size, PROT_NONE);
        dont_need_pages(big_zone->user_pages_start, big_zone->size);
        __iso_memset(big_zone, POISON_BYTE, sizeof(iso_alloc_big_zone_t));

        /* Big zone meta data is at a random offset from its base page */
        mprotect_pages(((void *) ROUND_DOWN_PAGE((uintptr_t) big_zone)), g_page_size, PROT_NONE);
    } else {
#if BIG_ZONE_GUARD
        /* Free the user pages first */
        unmap_guarded_pages(big_zone->user_pages_start, big_zone->size);
#else
        munmap(big_zone->user_pages_start, big_zone->size);
#endif

        /* The meta data struct sits at a random offset inside
         * its page so unmap from the page boundary, and with
         * a length in bytes rather than a page count */
        void *md_page = (void *) ROUND_DOWN_PAGE((uintptr_t) big_zone);
#if BIG_ZONE_META_DATA_GUARD
        unmap_guarded_pages(md_page, (g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT));
#else
        munmap(md_page, (g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT));
#endif
    }
}

/* alignment must be a power of 2. Every big zone mapping is
 * page aligned so only alignments larger than a page need
 * any special handling */
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_big_alloc(size_t size, size_t alignment, bool *fresh) {
    const size_t new_size = ROUND_UP_PAGE(size);
    const bool aligned = (alignment > g_page_size);

    if(new_size < size || new_size > BIG_SZ_MAX || size < SMALL_SIZE_MAX) {
#if ABORT_ON_NULL
        LOG_AND_ABORT("Cannot allocate a big zone of %ld bytes", new_size);
#endif
        LOG("Cannot allocate a big zone of %ld bytes", new_size);
        return NULL;
    }

    size = new_size;
    iso_alloc_big_zone_t *prev = NULL;

    LOCK_BIG_ZONE_FREE();

    /* There are two big zone lists, one for free chunks and a
     * second for in-use chunks. We first need to check the list
     * of free chunks to see if any can satisfy this request */
    if(_root->big_zone_free != NULL) {
        if(_root->big_zone_free_count <= 0) {
            LOG_AND_ABORT("Big zone free list not NULL (%p) but count is %d", _root->big_zone_free, _root->big_zone_free_count);
        }

        iso_alloc_big_zone_t *big = _root->big_zone_free;

        /* Unmask the free list head pointer */
        if(big != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_free);
        }

        /* Iterate the list looking for a zone we can use */
        while(big != NULL) {
            if(big->free == false) {
                LOG_AND_ABORT("Corrupted big zone free list, %p is in use", big);
            }

            /* Check the canary value */
            check_big_canary(big);

            /* We found a suitable big zone we can reuse */
            if(big->size >= size && (big->size - size) <= BIG_ZONE_WASTE * 2 &&
               (aligned == false || ((uintptr_t) big->user_pages_start & (alignment - 1)) == 0)) {
                big->free = false;
                _root->big_zone_free_count--;
                UNPOISON_BIG_ZONE(big);

                /* Remove this node from the list */
                if(big == UNMASK_BIG_ZONE_NEXT(_root->big_zone_free)) {
                    _root->big_zone_free = big->next;
                } else {
                    prev->next = big->next;
                }

                /* It is safe to unlock the free list here because
                 * we unlinked the zone we are about to return */
                UNLOCK_BIG_ZONE_FREE();

                LOCK_BIG_ZONE_USED();
                big->ttl++;
                big_zone_used_push(big);
                UNLOCK_BIG_ZONE_USED();
#if PROTECT_FREE_BIG_ZONES
                mprotect_pages(big->user_pages_start, big->size, PROT_READ | PROT_WRITE);
#endif
#if ARM_MTE
                if(_root->arm_mte_enabled == true) {
                    big->user_pages_start = iso_mte_set_tag_range(big->user_pages_start, big->size);
                }
#endif
                return big->user_pages_start;
            }

            prev = big;

            if(big->next != NULL) {
                big = UNMASK_BIG_ZONE_NEXT(big->next);
            } else {
                /* We've reached the end of the list */
                break;
            }
        }
    }

    UNLOCK_BIG_ZONE_FREE();

    /* The free list contained no usable entries
     * so we need to create a new one. An alignment
     * larger than a page is satisfied by mapping an
     * extra alignment bytes and trimming both ends */
    size_t map_size = size;

    if(aligned == true) {
        map_size += alignment;

        if(UNLIKELY(map_size < size)) {
            return NULL;
        }
    }

    void *user_pages = NULL;
#if ARM_MTE
#if BIG_ZONE_GUARD
    user_pages = mmap_guarded_rw_pages(map_size, false, BIG_ZONE_UD_NAME);
#else
    if(_root->arm_mte_enabled == true) {
        user_pages = mmap_rw_mte_pages(map_size, false, BIG_ZONE_UD_NAME);
    }
#endif
#else
#if BIG_ZONE_GUARD
    user_pages = mmap_guarded_rw_pages(map_size, false, BIG_ZONE_UD_NAME);
#else
    user_pages = mmap_rw_pages(map_size, false, BIG_ZONE_UD_NAME);
#endif
#endif

    if(UNLIKELY(user_pages == NULL)) {
#if ABORT_ON_NULL
        LOG_AND_ABORT("IsoAlloc configured to abort on NULL");
#endif
        return NULL;
    }

    if(aligned == true) {
        void *aligned_pages = (void *) (((uintptr_t) user_pages + (alignment - 1)) & ~(alignment - 1));
        const size_t head = (size_t) (aligned_pages - user_pages);
        const size_t tail = (alignment - head);
#if BIG_ZONE_GUARD
        /* Move the guard pages so they sit directly below and
         * above the aligned range, then release everything else.
         * The upper munmap also covers the original upper guard */
        if(head != 0) {
            munmap(user_pages - g_page_size, head);
            mprotect_pages(aligned_pages - g_page_size, g_page_size, PROT_NONE);
        }

        mprotect_pages(aligned_pages + size, g_page_size, PROT_NONE);
        munmap(aligned_pages + size + g_page_size, tail);
#else
        if(head != 0) {
            munmap(user_pages, head);
        }

        munmap(aligned_pages + size, tail);
#endif
        user_pages = aligned_pages;
    }

    /* We only need a single page for big zone meta data */
    static_assert(BIG_ZONE_META_DATA_PAGE_COUNT == 1, "Big zone meta data only needs a single page");
#if BIG_ZONE_META_DATA_GUARD
    iso_alloc_big_zone_t *new_big = (iso_alloc_big_zone_t *) mmap_guarded_rw_pages((g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT), false, BIG_ZONE_MD_NAME);
#else
    iso_alloc_big_zone_t *new_big = (iso_alloc_big_zone_t *) mmap_rw_pages((g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT), false, BIG_ZONE_MD_NAME);
#endif

    /* The second page is for meta data and it is placed
     * at a random offset from the start of the page. The
     * offset is reduced into range first and aligned after,
     * aligning the raw random value before the reduction
     * left the final offset with no alignment at all */
    /* No lock is held here so the shared root seed cannot be
     * used. A big allocation already costs a mapping, one more
     * call for entropy does not matter */
    const uint64_t random_offset = (uint32_t) rand_uint64();
    const size_t s = g_page_size - (sizeof(iso_alloc_big_zone_t) - 1);
    const size_t offset = ((random_offset * s) >> 32);
    new_big = (iso_alloc_big_zone_t *) ((void *) new_big + ALIGN_SZ_DOWN(offset));
    new_big->free = false;
    new_big->size = size;
    new_big->next = NULL;
    new_big->prev = NULL;
    new_big->ttl++;

    /* Save a pointer to the user pages */
    new_big->user_pages_start = user_pages;

    /* The canaries prevents a linear overwrite of the big
     * zone meta data structure from either direction */
    new_big->canary_a = ((uint64_t) new_big ^ __builtin_bswap64((uint64_t) new_big->user_pages_start) ^ _root->big_zone_canary_secret);
    new_big->canary_b = new_big->canary_a;

    LOCK_BIG_ZONE_USED();
    big_zone_used_push(new_big);
    UNLOCK_BIG_ZONE_USED();
#if ARM_MTE
    if(_root->arm_mte_enabled == true) {
        new_big->user_pages_start = iso_mte_set_tag_range(new_big->user_pages_start, new_big->size);
    }
#endif

    /* A new mapping is zero. A big zone reused from the free
     * list is not, its pages were only advised away */
    if(fresh != NULL) {
        *fresh = true;
    }

    return new_big->user_pages_start;
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

#if NO_ZERO_ALLOCATIONS
    if(UNLIKELY(p == _root->zero_alloc_page)) {
        return 0;
    }
#endif

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

    iso_alloc_zone_t *zone = iso_find_zone_range(p);

    if(UNLIKELY(zone == NULL)) {
        iso_alloc_big_zone_t *big_zone = iso_find_big_zone(p, false);

        if(UNLIKELY(big_zone == NULL)) {
#if ABORT_ON_UNOWNED_PTR
            LOG_AND_ABORT("Could not find any zone for allocation at 0x%p", p);
#else
            return 0;
#endif
        }

        return big_zone->size;
    }

    /* The chunk size of a zone never changes while it holds
     * a live chunk */
    return zone->chunk_size;
}

INTERNAL_HIDDEN void _free_big_zone_list(iso_alloc_big_zone_t *head) {
    iso_alloc_big_zone_t *big_zone = head;
    iso_alloc_big_zone_t *big = NULL;

    if(big_zone != NULL) {
        big_zone = UNMASK_BIG_ZONE_NEXT(head);
    }

    while(big_zone != NULL) {
        check_big_canary(big_zone);

        if(big_zone->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big_zone->next);
        } else {
            big = NULL;
        }

        /* Free the user pages first */
#if BIG_ZONE_GUARD
        unmap_guarded_pages(big_zone->user_pages_start, big_zone->size);
#else
        munmap(big_zone->user_pages_start, big_zone->size);
#endif

        /* The meta data struct sits at a random offset inside
         * its page so unmap from the page boundary, and with
         * a length in bytes rather than a page count */
        void *md_page = (void *) ROUND_DOWN_PAGE((uintptr_t) big_zone);
#if BIG_ZONE_META_DATA_GUARD
        unmap_guarded_pages(md_page, (g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT));
#else
        munmap(md_page, (g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT));
#endif
        big_zone = big;
    }
}

INTERNAL_HIDDEN void _iso_alloc_destroy(void) {
    LOCK_ROOT();

    flush_all_quarantines();

    const uint16_t zones_used = _root->zones_used;

#if HEAP_PROFILER
    _iso_output_profile();
#endif

#if NO_ZERO_ALLOCATIONS
    munmap(_root->zero_alloc_page, g_page_size);
#endif

#if UAF_PTR_PAGE
    munmap(_root->uaf_ptr_page, g_page_size);
#endif

#if DEBUG && (LEAK_DETECTOR || MEM_USAGE)
#if MEM_USAGE
    uint64_t mb = 0;
#endif

    for(uint16_t i = 0; i < zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];
        _iso_alloc_zone_leak_detector(zone, false);
    }

#if MEM_USAGE
    mb = __iso_alloc_mem_usage();
    mb += _iso_alloc_big_zone_mem_usage();
    LOG("Total megabytes consumed by all zones: %lu", mb);
    _iso_alloc_print_stats();
#endif

#endif

#if DEBUG || FUZZ_MODE || ISO_DTOR_CLEANUP
    for(uint16_t i = 0; i < zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];
        LOCK_ZONE(zone);
#if DEBUG || FUZZ_MODE
        _verify_zone(zone);
#endif
#if ISO_DTOR_CLEANUP
        _iso_alloc_destroy_zone_unlocked(zone, false);
#endif
        UNLOCK_ZONE(zone);
    }
#else
    (void) zones_used;
#endif

#if ISO_DTOR_CLEANUP
    /* Unmap all zone structures */
    munmap((void *) ((uintptr_t) _root->zones - g_page_size), _root->zones_size);
#endif

    LOCK_BIG_ZONE_USED();
    _free_big_zone_list(_root->big_zone_used);
    UNLOCK_BIG_ZONE_USED();

    LOCK_BIG_ZONE_FREE();
    _free_big_zone_list(_root->big_zone_free);
    UNLOCK_BIG_ZONE_FREE();

#if ISO_DTOR_CLEANUP
    iso_ptr_map_destroy(&_root->zone_map);
    iso_ptr_map_destroy(&_root->big_zone_map);
#if THREAD_SUPPORT
    /* Only this threads state can be released here. Other
     * threads release theirs when they exit */
    iso_thread_state_t *ts = thread_state_get();

    if(ts != NULL) {
        _root->quarantines = NULL;
        thread_state_set(NULL);
        unmap_guarded_pages(ts, sizeof(iso_thread_state_t));
    }
#else
    unmap_guarded_pages(thread_state_single, sizeof(iso_thread_state_t));
#endif

#if THREAD_SUPPORT && !USE_SPINLOCK
    UNLOCK_BIG_ZONE_FREE();
    pthread_mutex_destroy(&_root->big_zone_free_mutex);
    UNLOCK_BIG_ZONE_USED();
    pthread_mutex_destroy(&_root->big_zone_used_mutex);
#endif

    const int sbsi = (sizeof(small_bitmap_sizes) / sizeof(int)) - 1;

    for(int i = 0; i < sbsi; i++) {
        munmap(_root->bitmaps[i].bitmap, g_page_size);
    }

    unmap_guarded_pages(_root, sizeof(iso_alloc_root));
#endif
    UNLOCK_ROOT();

#if ISO_DTOR_CLEANUP && THREAD_SUPPORT
    /* The root is gone so a thread exiting after this must
     * not run the quarantine destructor against it */
    pthread_key_delete(thread_state_key);
#if !USE_SPINLOCK
#if ALLOC_SANITY
    pthread_mutex_destroy(&sane_cache_mutex);
#endif
    pthread_mutex_destroy(&root_busy_mutex);
#endif
#endif
}

#if UNIT_TESTING
/* Some tests require getting access to IsoAlloc internals
 * that aren't supported by the API. We never want these
 * in release builds of the library */
EXTERNAL_API iso_alloc_root *_get_root(void) {
    return _root;
}
#endif
