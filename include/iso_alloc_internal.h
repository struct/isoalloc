/* iso_alloc_internal.h - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */
#pragma once

#ifndef CPP_SUPPORT
#define _GNU_SOURCE
#endif

#if !__x86_64__
#pragma message "IsoAlloc is untested and unsupported on 32 bit platforms"
#endif

#if __linux__
#include <byteswap.h>
#elif __APPLE__
#include <libkern/OSByteOrder.h>
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#if THREAD_SUPPORT
#include <pthread.h>
#endif

#ifndef MADV_DONTNEED
#define MADV_DONTNEED POSIX_MADV_DONTNEED
#endif

#if ENABLE_ASAN
#include <sanitizer/asan_interface.h>

#define POISON_ZONE(zone)                                                  \
    if(IS_POISONED_RANGE(zone->user_pages_start, ZONE_USER_SIZE) == 0) {   \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, ZONE_USER_SIZE); \
    }

#define UNPOISON_ZONE(zone)                                                  \
    if(IS_POISONED_RANGE(zone->user_pages_start, ZONE_USER_SIZE) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(zone->user_pages_start, ZONE_USER_SIZE); \
    }

#define POISON_ZONE_CHUNK(zone, ptr)                      \
    if(IS_POISONED_RANGE(ptr, zone->chunk_size) == 0) {   \
        ASAN_POISON_MEMORY_REGION(ptr, zone->chunk_size); \
    }

#define UNPOISON_ZONE_CHUNK(zone, ptr)                      \
    if(IS_POISONED_RANGE(ptr, zone->chunk_size) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(ptr, zone->chunk_size); \
    }

#define POISON_BIG_ZONE(zone)                                          \
    if(IS_POISONED_RANGE(zone->user_pages_start, zone->size) == 0) {   \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, zone->size); \
    }

#define UNPOISON_BIG_ZONE(zone)                                          \
    if(IS_POISONED_RANGE(zone->user_pages_start, zone->size) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(zone->user_pages_start, zone->size); \
    }

#define IS_POISONED_RANGE(ptr, size) \
    __asan_region_is_poisoned(ptr, size)
#else
#define POISON_ZONE(zone)
#define UNPOISON_ZONE(zone)
#define POISON_ZONE_CHUNK(ptr, zone)
#define UNPOISON_ZONE_CHUNK(ptr, zone)
#define POISON_BIG_ZONE(zone)
#define UNPOISON_BIG_ZONE(zone)
#define IS_POISONED_RANGE(ptr, size) 0
#endif

#define OK 0
#define ERR -1

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/* GCC complains if your constructor priority is
 * 0-100 but Clang does not. We need the lowest
 * priority constructor for MALLOC_HOOK */
#define FIRST_CTOR 101
#define LAST_DTOR 65535

#define INTERNAL_HIDDEN __attribute__((visibility("hidden")))

#if UNIT_TESTING
#define EXTERNAL_API __attribute__((visibility("default")))
#endif

#if PERF_BUILD
#define INLINE
#else
#define INLINE __attribute__((always_inline))
#endif

#if DEBUG
#define LOG(msg, ...)                                                                                              \
    fprintf(stdout, "[LOG][%d](%s:%d %s()) " msg "\n", getpid(), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    fflush(stdout);
#else
#define LOG(msg, ...)
#endif

#define LOG_AND_ABORT(msg, ...)                                                                                         \
    fprintf(stdout, "[ABORTING][%d](%s:%d %s()) " msg "\n", getpid(), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    fflush(stdout);                                                                                                     \
    abort();

/* The number of bits in the bitmap that correspond
 * to a user chunk. We use 2 bits:
 *  00 free, never used
 *  10 currently in use
 *  01 was used, now free
 *  11 canary chunk / permanently free'd */
#define BITS_PER_CHUNK 2

#define BITS_PER_BYTE 8

#define BITS_PER_QWORD 64

#define CANARY_SIZE 8

#define CANARY_COUNT_DIV 100

#define ALIGNMENT 8

#define GET_BIT(n, k) \
    (n >> k) & 1UL

#define SET_BIT(n, k) \
    n |= 1UL << k;

#define UNSET_BIT(n, k) \
    n &= ~(1UL << k);

#define ALIGN_SZ_UP(n) \
    ((((n) + (ALIGNMENT) -1) / (ALIGNMENT)) * (ALIGNMENT))

#define ALIGN_SZ_DOWN(n) \
    ((((n) + (ALIGNMENT) -1) / (ALIGNMENT)) * (ALIGNMENT)) - ALIGNMENT

#define ROUND_UP_PAGE(n) \
    ((((n) + (g_page_size) -1) / (g_page_size)) * (g_page_size))

#define ROUND_DOWN_PAGE(n) \
    (ROUND_UP_PAGE(n) - g_page_size)

#define MASK_ZONE_PTRS(zone) \
    MASK_BITMAP_PTRS(zone);  \
    MASK_USER_PTRS(zone);

#define UNMASK_ZONE_PTRS(zone) \
    MASK_ZONE_PTRS(zone);

#define MASK_BITMAP_PTRS(zone) \
    zone->bitmap_start = (void *) ((uintptr_t) zone->bitmap_start ^ (uintptr_t) zone->pointer_mask);

#define MASK_USER_PTRS(zone) \
    zone->user_pages_start = (void *) ((uintptr_t) zone->user_pages_start ^ (uintptr_t) zone->pointer_mask);

#define MASK_BIG_ZONE_NEXT(bnp) \
    UNMASK_BIG_ZONE_NEXT(bnp)

#define UNMASK_BIG_ZONE_NEXT(bnp) \
    ((iso_alloc_big_zone *) ((uintptr_t) _root->big_zone_next_mask ^ (uintptr_t) bnp))

#if THREAD_SUPPORT
#define LOCK_ROOT_MUTEX() \
    pthread_mutex_lock(&_root->zone_mutex);

#define UNLOCK_ROOT_MUTEX() \
    pthread_mutex_unlock(&_root->zone_mutex);
#else
#define LOCK_ROOT_MUTEX()
#define UNLOCK_ROOT_MUTEX()
#endif

#define GET_CHUNK_COUNT(zone) \
    (ZONE_USER_SIZE / zone->chunk_size)

/* This is the maximum number of zones iso_alloc can
 * create. This is a completely arbitrary number but
 * it does correspond to the size of the _root.zones
 * array that lives in global memory */
#define MAX_ZONES 8192

/* Each user allocation zone we make is 8mb in size */
#define ZONE_USER_SIZE 8388608

/* This is the largest divisor of ZONE_USER_SIZE we can
 * get from (BITS_PER_QWORD/BITS_PER_CHUNK). Anything
 * above this size will need to go through the large
 * mapping code path */
#define SMALL_SZ_MAX 262144

#define WASTED_SZ_MULTIPLIER 8

#define BIG_ZONE_META_DATA_PAGE_COUNT 3
#define BIG_ZONE_USER_PAGE_COUNT 2

/* We allocate (1) zone at startup for common sizes.
 * Each of these default zones is ZONE_USER_SIZE bytes
 * so ZONE_8192 holds less chunks than ZONE_128 for
 * example. These are inexpensive for us to create */
#define ZONE_16 16
#define ZONE_32 32
#define ZONE_64 64
#define ZONE_128 128
#define ZONE_256 256
#define ZONE_512 512
#define ZONE_1024 1024
#define ZONE_2048 2048
#define ZONE_4096 4096
#define ZONE_8192 8192

#define MAX_DEFAULT_ZONE_SZ ZONE_8192

/* The size of our bit slot freelist */
#define BIT_SLOT_CACHE_SZ 254

#define MEGABYTE_SIZE 1000000

/* This byte value will overwrite the contents
 * of all free'd user chunks */
#define POISON_BYTE 0xde

#define CANARY_VALIDATE_MASK 0xffffffffffffff00

#define BAD_BIT_SLOT -1

/* Calculate the user pointer given a zone and a bit slot */
#define POINTER_FROM_BITSLOT(zone, bit_slot) \
    ((void *) zone->user_pages_start + ((bit_slot / BITS_PER_CHUNK) * zone->chunk_size));

/* This global is used by the page rounding macros.
 * The value stored in _root->system_page_size is
 * preferred but we need this to setup the root. */
uint32_t g_page_size;

/* iso_alloc makes a number of default zones for common
 * allocation sizes. Anything above these sizes will
 * be created and initialized on demand */
static uint64_t default_zones[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128, ZONE_256, ZONE_512,
                                   ZONE_1024, ZONE_2048, ZONE_4096, ZONE_8192};

typedef uint64_t bit_slot_t;
typedef int64_t bitmap_index_t;

/* The API allows for consumers of the library to
 * create their own zones for unique data/object
 * types. This structure allows the caller to define
 * which security mitigations should be applied to
 * all allocations within the zone */
typedef struct {
    bool random_allocation_pattern;
    bool adjacent_canary_verification_on_alloc;
    bool adjacent_canary_verification_on_free;
    bool clear_chunk_on_free;
    bool double_free_detection;
} iso_alloc_zone_configuration;

typedef struct {
    void *user_pages_start;                                /* Start of the pages backing this zone */
    void *bitmap_start;                                    /* Start of the bitmap */
    int32_t free_bit_slot_cache_index;                     /* Tracks how many entries in the cache are filled */
    int32_t free_bit_slot_cache_usable;                    /* The oldest members of the free cache are served first */
    int64_t next_free_bit_slot;                            /* The last bit slot returned by get_next_free_bit_slot */
    int32_t index;                                         /* Zone index */
    uint64_t canary_secret;                                /* Each zone has its own canary secret */
    uint64_t pointer_mask;                                 /* Each zone has its own pointer protection secret */
    uint32_t chunk_size;                                   /* Size of chunks managed by this zone */
    uint32_t bitmap_size;                                  /* Size of the bitmap in bytes */
    bool internally_managed;                               /* Zones can be managed by iso_alloc or custom */
    bool is_full;                                          /* Indicates whether this zone is full to avoid expensive free bit slot searches */
    bit_slot_t free_bit_slot_cache[BIT_SLOT_CACHE_SZ + 1]; /* A cache of bit slots that point to freed chunks */
} iso_alloc_zone;

/* Meta data for big allocations are allocated near the
 * user pages themselves but separated via guard pages.
 * This meta data is stored at a random offset from the
 * beginning of the page it resides on */
typedef struct iso_alloc_big_zone {
    uint64_t canary_a;
    bool free;
    uint64_t size;
    void *user_pages_start;
    struct iso_alloc_big_zone *next;
    uint64_t canary_b;
} iso_alloc_big_zone;

/* There is only one iso_alloc root per-process.
 * It contains an array of zone structures. Each
 * Zone represents a number of contiguous pages
 * that hold chunks containing caller data */
typedef struct {
    uint32_t zones_used;
    uint32_t system_page_size;
    void *guard_below;
    void *guard_above;
    uint64_t zone_handle_mask;
    uint64_t big_zone_next_mask;
    uint64_t big_zone_canary_secret;
#if THREAD_SUPPORT
    pthread_mutex_t zone_mutex;
#endif
    iso_alloc_zone zones[MAX_ZONES];
    iso_alloc_big_zone *big_zone_head;
} iso_alloc_root;

/* The global root */
iso_alloc_root *_root;

INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone *big);
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE void iso_clear_user_chunk(uint8_t *p, size_t size);
INTERNAL_HIDDEN INLINE void fill_free_bit_slot_cache(iso_alloc_zone *zone);
INTERNAL_HIDDEN INLINE void insert_free_bit_slot(iso_alloc_zone *zone, int64_t bit_slot);
INTERNAL_HIDDEN iso_alloc_zone *is_zone_usable(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_fit(size_t size);
INTERNAL_HIDDEN iso_alloc_zone *iso_new_zone(size_t size, bool internal);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_range(void *p);
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot_slow(iso_alloc_zone *zone);
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot(iso_alloc_zone *zone);
INTERNAL_HIDDEN bit_slot_t get_next_free_bit_slot(iso_alloc_zone *zone);
INTERNAL_HIDDEN void write_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN void mprotect_pages(void *p, size_t size, int32_t protection);
INTERNAL_HIDDEN void create_guard_page(void *p);
INTERNAL_HIDDEN void *mmap_rw_pages(size_t size, bool populate);
INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void iso_alloc_new_root(void);
INTERNAL_HIDDEN void verify_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void verify_all_zones(void);
INTERNAL_HIDDEN void _iso_free(void *p, bool permanent);
INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone *big_zone, bool permanent);
INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone *zone, void *p, bool permanent);
INTERNAL_HIDDEN void _iso_alloc_protect_root(void);
INTERNAL_HIDDEN void _iso_alloc_unprotect_root(void);
INTERNAL_HIDDEN void *_iso_big_alloc(size_t size);
INTERNAL_HIDDEN void *_iso_alloc(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN void *_iso_calloc(size_t nmemb, size_t size);
INTERNAL_HIDDEN int64_t check_canary_no_abort(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks(void);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage(void);
INTERNAL_HIDDEN uint64_t rand_uint64(void);
INTERNAL_HIDDEN size_t _iso_chunk_size(void *p);

#if UNIT_TESTING
EXTERNAL_API iso_alloc_root *_get_root();
#endif
