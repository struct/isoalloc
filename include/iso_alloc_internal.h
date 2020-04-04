/* iso_alloc_internal.h - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#ifndef CPP_SUPPORT
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define OK 0
#define ERR -1

#define INTERNAL_HIDDEN __attribute__((visibility("hidden")))

#if PERF_BUILD
#define INLINE
#else
#define INLINE __attribute__((always_inline))
#endif

#if DEBUG
#define LOG(msg, ...)                                                                  \
    fprintf(stdout, "[LOG][%d](%s) " msg "\n", getpid(), __FUNCTION__, ##__VA_ARGS__); \
    fflush(stdout);
#else
#define LOG(msg, ...)
#endif

#define LOG_AND_ABORT(msg, ...)                                                             \
    fprintf(stdout, "[ABORTING][%d](%s) " msg "\n", getpid(), __FUNCTION__, ##__VA_ARGS__); \
    fflush(stdout);                                                                         \
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

#define BIG_ALLOCATION_PAGE_COUNT 4

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
    void *user_pages_start;                             /* Start of the pages backing this zone */
    void *bitmap_start;                                 /* Start of the bitmap */
    int32_t free_bit_slot_cache_index;                  /* Tracks how many entries in the cache are filled */
    int32_t free_bit_slot_cache_usable;                 /* The oldest members of the free cache are served first */
    int64_t next_free_bit_slot;                         /* The last bit slot returned by get_next_free_bit_slot */
    int32_t index;                                      /* Zone index */
    uint64_t canary_secret;                             /* Each zone has its own canary secret */
    uint64_t pointer_mask;                              /* Each zone has its own pointer protection secret */
    uint32_t chunk_size;                                /* Size of chunks managed by this zone */
    uint32_t bitmap_size;                               /* Size of the bitmap in bytes */
    bool internally_managed;                            /* Zones can be managed by iso_alloc or custom */
    bool is_full;                                       /* Indicates whether this zone is full to avoid expensive free bit slot searches */
    int64_t free_bit_slot_cache[BIT_SLOT_CACHE_SZ + 1]; /* A cache of bit slots that point to freed chunks */
} iso_alloc_zone;

/* Meta data for big allocations are allocated near the
 * user pages themselves but separated via guard pages.
 * This meta data is stored at a random offset from the
 * beginning of the page it resides on */
typedef struct iso_alloc_big_zone {
    bool free;
    size_t size;
    void *user_pages_start;
    struct iso_alloc_big_zone *next;
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
    pthread_mutex_t zone_mutex;
    iso_alloc_zone zones[MAX_ZONES];
    iso_alloc_big_zone *big_alloc_zone_head;
} iso_alloc_root;

/* The global root */
iso_alloc_root *_root;
bool iso_alloc_initialized;

INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE int64_t check_canary_no_abort(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE void mprotect_pages(void *p, size_t size, int32_t protection);
INTERNAL_HIDDEN INLINE void *mmap_rw_pages(size_t size, bool populate);
INTERNAL_HIDDEN INLINE void iso_clear_user_chunk(uint8_t *p, size_t size);
INTERNAL_HIDDEN INLINE void *get_base_page(void *addr);
INTERNAL_HIDDEN INLINE int64_t iso_scan_zone_free_slot_slow(iso_alloc_zone *zone);
INTERNAL_HIDDEN INLINE void fill_free_bit_slot_cache(iso_alloc_zone *zone);
INTERNAL_HIDDEN iso_alloc_zone *is_zone_usable(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_fit(size_t size);
INTERNAL_HIDDEN iso_alloc_zone *iso_new_zone(size_t size, bool internal);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_range(void *p);
INTERNAL_HIDDEN int64_t iso_scan_zone_free_slot(iso_alloc_zone *zone);
INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void iso_alloc_new_root();
INTERNAL_HIDDEN void verify_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void verify_all_zones();
INTERNAL_HIDDEN int64_t get_next_free_bit_slot(iso_alloc_zone *zone);
INTERNAL_HIDDEN void insert_free_bit_slot(iso_alloc_zone *zone, int64_t bit_slot);
INTERNAL_HIDDEN void _iso_free(void *p, bool permanent);
INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone *big_zone, bool permanent);
INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone *zone, void *p, bool permanent);
INTERNAL_HIDDEN void *_iso_big_alloc(size_t size);
INTERNAL_HIDDEN void *_iso_alloc(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN void *_iso_calloc(size_t nmemb, size_t size);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks();
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage();
INTERNAL_HIDDEN size_t _iso_chunk_size(void *p);
INTERNAL_HIDDEN void _iso_alloc_protect_root();
INTERNAL_HIDDEN void _iso_alloc_unprotect_root();
