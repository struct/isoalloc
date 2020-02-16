/* iso_alloc_internal.h - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#ifndef CPP_SUPPORT
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if THREAD_SUPPORT
#include <pthread.h>
#endif

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

#define ROUND_PAGE_UP(N) ((((N) + (g_page_size) -1) / (g_page_size)) * (g_page_size))
#define ROUND_PAGE_DOWN(N) (ROUND_PAGE_UP(N) - g_page_size)

/* The number of bits in the bitmap that correspond
 * to a user chunk. We use 2 bits:
 *  00 free, never used
 *  10 currently in use
 *  01 was used, now free
 *  11 canary chunk / permanently free'd */
#define BITS_PER_CHUNK 2

#define BITS_PER_BYTE 8

#define BITS_PER_DWORD 32

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

#define ROUND_UP_SZ(n) \
    ((((n) + (g_page_size) -1) / (g_page_size)) * (g_page_size))

#define ROUND_UP_PAGE(n) \
    ((((n) + (g_page_size) -1) / (g_page_size)) * (g_page_size))

#define ROUND_DOWN_PAGE(n) \
    (ROUND_UP_PAGE(n) - g_page_size)

#define UNMASK_ZONE_PTRS(zone) \
    MASK_BITMAP_PTRS(zone);    \
    MASK_USER_PTRS(zone);

#define MASK_ZONE_PTRS(zone) \
    MASK_BITMAP_PTRS(zone);  \
    MASK_USER_PTRS(zone);

#define MASK_BITMAP_PTRS(zone)                                                                       \
    zone->bitmap_start = (void *) ((uintptr_t) zone->bitmap_start ^ (uintptr_t) zone->pointer_mask); \
    zone->bitmap_end = (void *) ((uintptr_t) zone->bitmap_end ^ (uintptr_t) zone->pointer_mask);

#define MASK_USER_PTRS(zone)                                                                                 \
    zone->user_pages_start = (void *) ((uintptr_t) zone->user_pages_start ^ (uintptr_t) zone->pointer_mask); \
    zone->user_pages_end = (void *) ((uintptr_t) zone->user_pages_end ^ (uintptr_t) zone->pointer_mask);

#if THREAD_SUPPORT
#define LOCK_ZONE_MUTEX(zone) \
    pthread_mutex_lock(&zone->mutex);
#define UNLOCK_ZONE_MUTEX(zone) \
    pthread_mutex_unlock(&zone->mutex);
#else
#define LOCK_ZONE_MUTEX(zone)
#define UNLOCK_ZONE_MUTEX(zone)
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

#define WASTED_SZ_MULTIPLIER 8

/* We allocate (1) zone at startup for common sizes.
 * Each of these default zones is ZONE_SIZE in bytes
 * so ZONE_65535 holds less chunks than ZONE_128 for
 * example. These are inexpensive for us to create
 * and only have a cost at startup time only */
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
static uint32_t default_zones[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128, ZONE_256, ZONE_512,
                                   ZONE_1024, ZONE_2048, ZONE_4096, ZONE_8192};

#define MAX_DEFAULT_ZONE_SZ ZONE_8192

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
    size_t chunk_size;                                  /* Size of chunks managed by this zone */
    size_t bitmap_size;                                 /* Size of the bitmap in bytes */
    void *bitmap_start;                                 /* Start of the bitmap */
    void *bitmap_end;                                   /* End of the bitmap */
    void *bitmap_pages_guard_below;                     /* Bitmap pages guard below */
    void *bitmap_pages_guard_above;                     /* Bitmap pages guard above */
    void *user_pages_start;                             /* Start of the pages backing this zone */
    void *user_pages_end;                               /* End of the pages backing this zone */
    void *user_pages_guard_below;                       /* User pages guard below */
    void *user_pages_guard_above;                       /* User pages guard below */
    int64_t free_bit_slot_cache[BIT_SLOT_CACHE_SZ + 1]; /* A cache of bit slots that point to freed chunks */
    int32_t free_bit_slot_cache_index;                  /* Tracks how many entries in the cache are filled */
    int32_t free_bit_slot_cache_usable;                 /* The oldest members of the free cache are served first */
    int64_t next_free_bit_slot;                         /* The last bit slot returned by get_random_free_bit_slot */
    int32_t index;                                      /* Zone index */
    int32_t canary_count;                               /* Number of canaries in this zone */
    uint64_t canary_secret;                             /* Each zone has its own canary secret */
    uint64_t pointer_mask;                              /* Each zone has its own pointer protection secret */
    bool internally_managed;                            /* Zones can be managed by iso_alloc or custom */
    bool is_full;                                       /* Indicates whether this zone is full to avoid expensive free bit slot searches */
#if THREAD_SUPPORT
    pthread_mutex_t mutex; /* Each zone has its own mutex which protects both allocations and frees */
#endif
} iso_alloc_zone;

/* There is only one iso_alloc root per-process.
 * It contains an array of zone structures. Each
 * Zone represents a number of contiguous pages
 * that hold chunks containing caller data */
typedef struct {
    uint32_t zones_used;
    uint32_t system_page_size;
    void *guard_below;
    void *guard_above;
    iso_alloc_zone zones[MAX_ZONES];
} iso_alloc_root;

/* The global root */
iso_alloc_root *_root;
bool iso_alloc_initialized;

INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE int32_t check_canary_no_abort(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE void mprotect_pages(void *p, size_t size, int32_t protection);
INTERNAL_HIDDEN INLINE void *mmap_rw_pages(size_t size);
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
INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone *zone, void *p, bool permanent);
INTERNAL_HIDDEN void *_iso_alloc(size_t size, iso_alloc_zone *zone);
INTERNAL_HIDDEN void *_iso_calloc(size_t nmemb, size_t size);
INTERNAL_HIDDEN int32_t _iso_alloc_zone_leak_detector(iso_alloc_zone *zone);
INTERNAL_HIDDEN int32_t _iso_alloc_leak_detector(iso_alloc_zone *zone);
INTERNAL_HIDDEN int32_t _iso_alloc_detect_leaks();
INTERNAL_HIDDEN int32_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone);
INTERNAL_HIDDEN int32_t _iso_alloc_mem_usage();
INTERNAL_HIDDEN int32_t _iso_chunk_size(void *p);
INTERNAL_HIDDEN void _iso_alloc_protect_root();
INTERNAL_HIDDEN void _iso_alloc_unprotect_root();
