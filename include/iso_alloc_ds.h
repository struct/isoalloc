/* iso_alloc_ds.h - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#pragma once

/* This header contains the core data structures,
 * caches, and typedef used by the allocator */

typedef int64_t bit_slot_t;
typedef int64_t bitmap_index_t;
typedef uint16_t zone_lookup_table_t;
typedef uint16_t chunk_lookup_table_t;

#define BIT_SLOT_CACHE_SZ 255

typedef struct {
    void *user_pages_start;     /* Start of the pages backing this zone */
    void *bitmap_start;         /* Start of the bitmap */
    int64_t next_free_bit_slot; /* The last bit slot returned by get_next_free_bit_slot */
    /* These indexes must be bumped to uint16_t if BIT_SLOT_CACHE_SZ >= MAX_UINT8 */
    uint8_t free_bit_slot_cache_index;                 /* Tracks how many entries in the cache are filled */
    uint8_t free_bit_slot_cache_usable;                /* The oldest members of the free cache are served first */
    bit_slot_t free_bit_slot_cache[BIT_SLOT_CACHE_SZ]; /* A cache of bit slots that point to freed chunks */
    uint64_t canary_secret;                            /* Each zone has its own canary secret */
    uint64_t pointer_mask;                             /* Each zone has its own pointer protection secret */
    uint32_t chunk_size;                               /* Size of chunks managed by this zone */
    uint32_t bitmap_size;                              /* Size of the bitmap in bytes */
    bitmap_index_t max_bitmap_idx;                     /* Max bitmap index for this bitmap */
    bool internal;                                     /* Zones can be managed by iso_alloc or private */
    bool is_full;                                      /* Flags whether this zone is full to avoid bit slot searches */
    uint16_t index;                                    /* Zone index */
    uint16_t next_sz_index;                            /* What is the index of the next zone of this size */
    uint32_t alloc_count;                              /* Total number of lifetime allocations */
    uint32_t af_count;                                 /* Increment/Decrement with each alloc/free operation */
    uint32_t chunk_count;                              /* Total number of chunks in this zone */
    uint8_t chunk_size_pow2;                           /* Computed by _log2(chunk_size) at zone creation */
#if MEMORY_TAGGING
    bool tagged; /* Zone supports memory tagging */
#endif
#if CPU_PIN
    uint8_t cpu_core; /* What CPU core this zone is pinned to */
#endif
} __attribute__((packed, aligned(sizeof(int64_t)))) iso_alloc_zone_t;

/* Meta data for big allocations are allocated near the
 * user pages themselves but separated via guard pages.
 * This meta data is stored at a random offset from the
 * beginning of the page it resides on */
typedef struct iso_alloc_big_zone_t {
    uint64_t canary_a;
    bool free;
    uint64_t size;
    void *user_pages_start;
    struct iso_alloc_big_zone_t *next;
    uint64_t canary_b;
} __attribute__((packed, aligned(sizeof(int64_t)))) iso_alloc_big_zone_t;

/* There is only one iso_alloc root per-process.
 * It contains an array of zone structures. Each
 * Zone represents a number of contiguous pages
 * that hold chunks containing caller data */
typedef struct {
    uint16_t zones_used;
    void *guard_below;
    void *guard_above;
    uint32_t zone_retirement_shf;
    uintptr_t *chunk_quarantine;
    size_t chunk_quarantine_count;
    /* Zones are linked by their next_sz_index member which
     * tells the allocator where in the _root->zones array
     * it can find the next zone that holds the same size
     * chunks. The lookup table helps us find the first zone
     * that holds a specific size in O(1) time */
    zone_lookup_table_t *zone_lookup_table;
    /* The chunk to zone lookup table provides a high hit
     * rate cache for finding which zone owns a user chunk.
     * It works by mapping the MSB of the chunk addressq
     * to a zone index. Misses are gracefully handled and
     * more common with a higher RSS and more mappings. */
    chunk_lookup_table_t *chunk_lookup_table;
    uint64_t zone_handle_mask;
    uint64_t big_zone_next_mask;
    uint64_t big_zone_canary_secret;
    iso_alloc_big_zone_t *big_zone_head;
    iso_alloc_zone_t *zones;
    size_t zones_size;
} __attribute__((aligned(sizeof(int64_t)))) iso_alloc_root;

typedef struct {
    void *user_pages_start;
    void *bitmap_start;
    uint32_t bitmap_size;
    uint8_t ttl;
} __attribute__((aligned(sizeof(int64_t)))) zone_quarantine_t;

/* Each thread gets a local cache of the most recently
 * used zones. This can greatly speed up allocations
 * if your threads are reusing the same zones. This
 * cache is first in last out, and is populated during
 * both alloc and free operations */
typedef struct {
    size_t chunk_size;
    iso_alloc_zone_t *zone;
} __attribute__((aligned(sizeof(int64_t)))) _tzc;
