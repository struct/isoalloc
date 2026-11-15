/* iso_alloc_ds.h - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#pragma once

#include <stddef.h>

/* This header contains the core data structures,
 * caches, and typedef used by the allocator */
#define ZONE_FREE_LIST_SZ 255

/* Every chunk size is a multiple of this and every chunk is
 * aligned to it. 16 matches max_align_t on both supported
 * targets so malloc callers get the alignment they expect
 * without rounding a 16 byte request up to 32 */
#define SZ_ALIGNMENT 16

/* Size classes. A request is rounded up to SZ_ALIGNMENT and
 * then to a class, and is only ever served from an internal
 * zone whose chunk size is exactly that class. Requests up to
 * SIZE_CLASS_EXACT_MAX are their own class. Above that there
 * are four classes per power of two so a chunk is never more
 * than a quarter larger than the request. Every power of two
 * is a class, and so is every multiple of a power of two
 * alignment that _iso_alloc_aligned can produce, which is what
 * lets aligned requests share zones with plain ones.
 * STRONG_SIZE_ISOLATION makes every aligned size its own class */
#define SIZE_CLASS_EXACT_MAX 256
#define SIZE_CLASS_EXACT_COUNT (SIZE_CLASS_EXACT_MAX / SZ_ALIGNMENT)
#define SIZE_CLASS_STEPS_PER_POW2 4

#if STRONG_SIZE_ISOLATION
/* Only classes up to this size have a slot in the per thread
 * class table, larger requests always walk the lookup chain */
#define SIZE_CLASS_CACHE_MAX 4096
#define SIZE_CLASS_COUNT (SIZE_CLASS_CACHE_MAX / SZ_ALIGNMENT)
#else
#define SIZE_CLASS_COUNT (SIZE_CLASS_EXACT_COUNT + (SIZE_CLASS_STEPS_PER_POW2 * (__builtin_ctzll(SMALL_SIZE_MAX) - __builtin_ctzll(SIZE_CLASS_EXACT_MAX))))
#endif

/* Returned by size_class_index for a class without a table slot */
#define SIZE_CLASS_NONE SIZE_MAX

/* The size of this table scales with SMALL_SIZE_MAX. */
#define ZONE_LOOKUP_TABLE_SZ (SMALL_SIZE_MAX >> 4) * sizeof(uint32_t)
#define SZ_TO_ZONE_LOOKUP_IDX(size) size >> 4

/* The zone lookup table and the next_sz_index chain store a
 * zone index plus one so that 0 can mean empty. Zone 0 is a
 * real zone and was unreachable through the table before */
#define ZONE_CHAIN_LINK(index) ((index) + 1)
#define ZONE_CHAIN_INDEX(link) ((link) - 1)

typedef int64_t bit_slot_t;
typedef int64_t bitmap_index_t;
typedef uint16_t zone_lookup_table_t;

/* An entry in a pointer map. A key of 0 marks an empty slot */
typedef struct {
    uint64_t key;
    uint64_t value;
} iso_ptr_map_entry_t;

/* A hash map from 64 bit keys to 64 bit values, see
 * iso_alloc_map.c. The root holds one from zone user page
 * windows to zone indexes and one from big zone user pages
 * to their meta data */
typedef struct {
    iso_ptr_map_entry_t *entries;
    uint64_t mask;          /* Capacity minus one, capacity is a power of 2 */
    uint64_t count;         /* Live entries */
    uint64_t seq;           /* Odd while a writer is inside the map */
    uint32_t shift;         /* 64 minus log2 of the capacity */
    bool lock_free_readers; /* Readers take no lock, so the map never grows */
} iso_ptr_map_t;

#if ZONE_FREE_LIST_SZ >= 255
typedef uint16_t free_bit_slot_t;
#define FREE_LIST_SHF 16
#else
typedef uint8_t free_bit_slot_t;
#define FREE_LIST_SHF 8
#endif

/* Entries in the per zone free bit slot cache. A bit slot
 * is at most (ZONE_USER_SIZE / SMALLEST_CHUNK_SZ) * 2 which
 * fits in 32 bits, halving the size of the cache compared
 * to storing bit_slot_t. BAD_BIT_SLOT is stored as all ones */
typedef uint32_t bit_slot_cache_t;

/* One bit per page of user memory that records whether the
 * canary chunks on that page have been written. Sized for
 * the smallest supported page size of 4096 bytes */
#define CANARY_PAGE_BITMAP_QWORDS 16

typedef struct {
    /* The zone lock. Every access to the fields below and to
     * the bitmap happens with it held, except for the fields
     * that never change after creation. It is the first field
     * so a zone can be reinitialized in place without touching
     * it, a replacement zone stays locked by the thread that
     * retired the old one. The padding keeps the layout the
     * same with and without THREAD_SUPPORT */
#if THREAD_SUPPORT
    atomic_flag busy;
    uint8_t lock_pad[7];
#else
    uint8_t lock_pad[8];
#endif
    /* Hot fields: everything touched by every allocation
     * fits in the first 64 byte cache line. The struct is
     * 64 byte aligned so this holds for every zone in the
     * zones array. The two pointers are always stored
     * masked, see UNMASK_USER_PTR */
    void *user_pages_start;                /* Start of the pages backing this zone */
    void *bitmap_start;                    /* Start of the bitmap */
    int64_t next_free_bit_slot;            /* The last bit slot returned by get_next_free_bit_slot */
    uint64_t canary_secret;                /* Each zone has its own canary secret */
    uint64_t pointer_mask;                 /* Each zone has its own pointer protection secret */
    uint32_t chunk_size;                   /* Size of chunks managed by this zone */
    uint32_t chunk_div_magic;              /* Reciprocal of chunk_size, see iso_free_chunk_from_zone */
    uint32_t af_count;                     /* Increment/Decrement with each alloc/free operation */
    free_bit_slot_t free_bit_slots_usable; /* The oldest members of the free cache are served first */
    free_bit_slot_t free_bit_slots_index;  /* Tracks how many entries in the cache are filled */
    bool is_full;                          /* Flags whether this zone is full to avoid bit slot searches */
    bool internal;                         /* Zones can be managed by iso_alloc or private */
    uint8_t chunk_div_shift;               /* Shift that pairs with chunk_div_magic */
    int8_t preallocated_bitmap_idx;        /* The bitmap is preallocated and its index */
    /* Warm fields: touched on the free path */
    uint32_t max_bitmap_idx; /* Number of qwords in the bitmap, the bitmap covers exactly chunk_count chunks */
#if MEMORY_TAGGING
    bool tagged; /* Zone supports memory tagging */
#endif
#if CPU_PIN
    uint8_t cpu_core; /* What CPU core this zone is pinned to */
#endif
    uint32_t alloc_count; /* Total number of lifetime allocations */
    uint32_t bitmap_size; /* Size of the bitmap in bytes */
    uint32_t chunk_count; /* Total number of chunks in this zone */
    /* Random state for this zone, see fill_free_bit_slots. Each
     * zone draws from its own stream so refilling the free list
     * costs no system call and no shared state is written */
    uint64_t seed;
    uint32_t refills;        /* Free list refills since creation, reseeds every so often */
    uint32_t purge_snapshot; /* alloc_count seen by the last idle purge pass, see _iso_alloc_purge_idle_zones */
    /* The first chunk starts this many bytes into the user pages.
     * It is a random multiple of the largest power of two that
     * divides chunk_size, below one page, so chunks keep their
     * natural alignment while zones of one class spread their
     * chunks over different cache sets. Chunks that no longer fit
     * at the end have their bitmap bits set to 11 */
    uint16_t chunk_offset;
#if MEMORY_TAGGING
    uint32_t tags_refreshed_at; /* Value of alloc_count at the last whole zone tag refresh */
#endif
    uint16_t index;         /* Zone index */
    uint16_t next_sz_index; /* Index plus one of the next zone of this size, 0 ends the chain */
    /* Cold: which user pages have had their canary chunks
     * written. Canary chunks are only written when the free
     * list first reaches their page so an untouched zone
     * stays unmapped, see materialize_canaries */
    uint64_t canary_pages[CANARY_PAGE_BITMAP_QWORDS];
    /* Large cold array: only accessed when refilling the free list */
    bit_slot_cache_t free_bit_slots[ZONE_FREE_LIST_SZ]; /* A cache of bit slots that point to freed chunks */
} __attribute__((aligned(64))) iso_alloc_zone_t;

/* Reinitializing a zone in place clears everything after the
 * lock, see _iso_new_zone */
#define ZONE_RESET_OFFSET offsetof(iso_alloc_zone_t, user_pages_start)

/* Meta data for big allocations are allocated near the
 * user pages themselves but separated via guard pages.
 * This meta data is stored at a random offset from the
 * beginning of the page it resides on */
typedef struct iso_alloc_big_zone_t {
    uint64_t canary_a;
    bool free;
    uint64_t size;
    uint32_t ttl;
    void *user_pages_start;
    /* Both links are stored masked, NULL is stored as NULL.
     * prev is only maintained on the used list so a zone
     * found through big_zone_map can be unlinked in O(1) */
    struct iso_alloc_big_zone_t *next;
    struct iso_alloc_big_zone_t *prev;
    uint64_t canary_b;
} __attribute__((packed, aligned(sizeof(int64_t)))) iso_alloc_big_zone_t;

#define BITMAP_SIZE_16 16
#define BITMAP_SIZE_32 32
#define BITMAP_SIZE_64 64
#define BITMAP_SIZE_128 128
#define BITMAP_SIZE_256 256
#define BITMAP_SIZE_512 512
#define BITMAP_SIZE_1024 1024

/* Small bitmap sizes are in reverse order as
 * smaller chunks are more likely to be needed.
 * The extra 0 is for alignment, we subtract it
 * when iterating _root->bitmaps */
const static int small_bitmap_sizes[] = {
    BITMAP_SIZE_1024,
    BITMAP_SIZE_512,
    BITMAP_SIZE_256,
    BITMAP_SIZE_128,
    BITMAP_SIZE_64,
    BITMAP_SIZE_32,
    BITMAP_SIZE_16,
    0};

/* Preallocated pages for bitmaps are managed using
 * an array of these structures placed in the root */
typedef struct {
    void *bitmap;
    /* Our bitmap has a bitmap */
    uint64_t in_use; /* One bit per bitmap slice handed out from this page */
    /* Bucket value determines how many times we divy up
     * the bitmap page. eg For BITMAP_SIZE_16 its 256 times */
    uint32_t bucket;
} __attribute__((packed, aligned(sizeof(int64_t)))) iso_alloc_bitmap_t;

/* There is only one iso_alloc root per-process.
 * It contains an array of zone structures. Each
 * Zone represents a number of contiguous pages
 * that hold chunks containing caller data */
typedef struct {
    iso_alloc_zone_t *zones;
    /* Maps the 4 MB window a pointer lies in to the index of
     * the zone whose user pages fill that window. Zone user
     * pages are aligned to their own size so every zone is
     * exactly one key. Readers take no lock, writers hold the
     * root lock. See iso_find_zone_range */
    iso_ptr_map_t zone_map;
    /* Maps the first page of a big zone to its meta data.
     * Guarded by the big zone used lock */
    iso_ptr_map_t big_zone_map;
#if THREAD_SUPPORT
    /* Every thread links the quarantine inside its thread state
     * here when the state is created so a zone can be destroyed
     * safely, see flush_all_quarantines. Guarded by the thread
     * registry lock in iso_alloc.c */
    struct iso_quarantine_t *quarantines;
#endif
    iso_alloc_big_zone_t *big_zone_free;
    iso_alloc_big_zone_t *big_zone_used;
#if NO_ZERO_ALLOCATIONS
    void *zero_alloc_page;
#endif
#if UAF_PTR_PAGE
    void *uaf_ptr_page;
#endif
    /* Zones are linked by their next_sz_index member which
     * tells the allocator where in the _root->zones array
     * it can find the next zone that holds the same size
     * chunks. The lookup table helps us find the first zone
     * that holds a specific size in O(1) time */
    /* Array sized to cover indices 0..(SMALL_SIZE_MAX>>4) inclusive, then
     * rounded to a multiple of 4 entries so the array occupies a whole
     * number of 8-byte words and bitmaps[] remains naturally aligned. */
    zone_lookup_table_t zone_lookup_table[(SMALL_SIZE_MAX >> 4) + 4];
    /* For chunk sizes >= 1024 our bitmap size is smaller
     * than a page. This optimization preallocates pages tog
     * hold multiple bitmaps for these zones */
    iso_alloc_bitmap_t bitmaps[sizeof(small_bitmap_sizes) / sizeof(int)];
    uint64_t zone_handle_mask;
    uint64_t big_zone_next_mask;
    uint64_t big_zone_canary_secret;
    uint64_t seed;
    size_t zones_size;
#if THREAD_SUPPORT
#if USE_SPINLOCK
    atomic_flag big_zone_free_flag;
    atomic_flag big_zone_used_flag;
#else
    pthread_mutex_t big_zone_free_mutex;
    pthread_mutex_t big_zone_used_mutex;
#endif
#endif
    uint32_t zone_retirement_shf;
    /* Idle zone purging, see _iso_alloc_purge_idle_zones */
    uint64_t last_purge_ms;
#if THREAD_SUPPORT
    atomic_flag purging;
#endif
    /* How many zones of one size class the allocator will
     * create to avoid waiting for a zone another thread is
     * using. Twice the CPU count, see acquire_zone_for_class */
    uint32_t zones_per_class_cap;
    int32_t big_zone_free_count;
    int32_t big_zone_used_count;
    uint16_t zones_used;
#if ARM_MTE
    bool arm_mte_enabled;
#endif
} __attribute__((aligned(sizeof(int64_t)))) iso_alloc_root;

typedef struct {
    void *user_pages_start;
    void *bitmap_start;
    uint32_t bitmap_size;
    uint8_t ttl;
} __attribute__((aligned(sizeof(int64_t)))) zone_quarantine_t;

/* Each thread remembers the last internal zone it used for
 * each size class. The alloc path reads one slot and because a
 * slot only ever holds a zone of exactly its class a request
 * can never land in a bigger chunk than its class. Internal
 * zones are never unmapped, a retired one is replaced in place
 * with the same chunk size, so a stale slot still points at a
 * zone of the right class and is revalidated under the lock.
 * This replaced a ring of recently used zones that served a
 * request from the first zone that was big enough */
typedef struct {
    iso_alloc_zone_t *by_class[SIZE_CLASS_COUNT];
} __attribute__((aligned(sizeof(int64_t)))) iso_zone_cache_t;

/* Chunks passed to iso_free wait here until the quarantine
 * is full, then they are freed in one batch under the root
 * lock. With THREAD_SUPPORT each thread owns one of these in
 * thread local storage so the common free path never takes
 * the root lock. The busy flag serializes the owning thread
 * against another thread flushing this quarantine when it
 * destroys a zone, see flush_all_quarantines */
typedef struct iso_quarantine_t {
    uintptr_t chunks[CHUNK_QUARANTINE_SZ];
    uint32_t count;
#if THREAD_SUPPORT
    atomic_flag busy;
    struct iso_quarantine_t *next;
#endif
} __attribute__((aligned(sizeof(int64_t)))) iso_quarantine_t;

/* Everything a thread keeps for itself: its zone cache and its
 * quarantine. One of these is mapped per thread on first use,
 * surrounded by guard pages, and reached through a pthread key.
 * Without THREAD_SUPPORT there is a single one for the process */
typedef struct {
    iso_zone_cache_t cache;
    iso_quarantine_t quarantine;
} __attribute__((aligned(sizeof(int64_t)))) iso_thread_state_t;
