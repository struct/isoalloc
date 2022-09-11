/* iso_alloc_internal.h - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#if !__aarch64__ && !__x86_64__
#pragma error "IsoAlloc is untested and unsupported on 32 bit platforms"
assert(sizeof(size_t) >= 64)
#endif

#if __linux__
#include "os/linux.h"
#endif

#if __APPLE__
#include "os/macos.h"
#endif

#if __ANDROID__
#include "os/android.h"
#endif

#if __FreeBSD__
#include "os/freebsd.h"
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/types.h>

#if MEM_USAGE
#include <sys/resource.h>
#endif

#if THREAD_SUPPORT
#include <pthread.h>
#ifdef __cplusplus
#include <atomic>
    using namespace std;
#else
#include <stdatomic.h>
#endif
#endif

#if HEAP_PROFILER
#include <fcntl.h>
#endif

#include "iso_alloc.h"
#include "iso_alloc_sanity.h"
#include "iso_alloc_util.h"
#include "iso_alloc_ds.h"
#include "compiler.h"
#include "conf.h"

#ifndef MADV_DONTNEED
#define MADV_DONTNEED POSIX_MADV_DONTNEED
#endif

#if ENABLE_ASAN
#include <sanitizer/asan_interface.h>

#define POISON_ZONE(zone)                                                     \
    if(IS_POISONED_RANGE(zone->user_pages_start, ZONE_USER_SIZE) == 0) {      \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, ZONE_USER_SIZE);    \
    }                                                                         \
    if(IS_POISONED_RANGE(zone->bitmap_start, zone->bitmap_size) == 0) {       \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, zone->bitmap_size); \
    }

#define UNPOISON_ZONE(zone)                                                  \
    if(IS_POISONED_RANGE(zone->user_pages_start, ZONE_USER_SIZE) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(zone->user_pages_start, ZONE_USER_SIZE); \
    }                                                                        \
    if(IS_POISONED_RANGE(zone->bitmap_start, zone->bitmap_size) != 0) {      \
        ASAN_UNPOISON_MEMORY_REGION(zone->bitmap_start, zone->bitmap_size);  \
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

/* GCC complains if your constructor priority is
 * 0-100 but Clang does not. We need the lowest
 * priority constructor for MALLOC_HOOK */
#define FIRST_CTOR 101
#define LAST_DTOR 65535

#if DEBUG
#define LOG(msg, ...) \
    _iso_alloc_printf(STDOUT_FILENO, "[LOG][%d](%s:%d %s()) " msg "\n", getpid(), __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#else
#define LOG(msg, ...)
#endif

#define LOG_AND_ABORT(msg, ...)                                                                                                      \
    _iso_alloc_printf(STDOUT_FILENO, "[ABORTING][%d](%s:%d %s()) " msg "\n", getpid(), __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    abort();

/* The number of bits in the bitmap that correspond
 * to a user chunk. We use 2 bits:
 *  00 free, never used
 *  10 currently in use
 *  01 was used, now free
 *  11 canary chunk / permanently free'd */
#define BITS_PER_CHUNK 2
#define BITS_PER_CHUNK_SHIFT 1

#define BITS_PER_BYTE 8
#define BITS_PER_BYTE_SHIFT 3

#define BITS_PER_QWORD 64
#define BITS_PER_QWORD_SHIFT 6

#define CANARY_SIZE 8

/* All chunks are 8 byte aligned */
#define ALIGNMENT 8

#define WHICH_BIT(bit_slot) \
    (bit_slot & (BITS_PER_QWORD - 1))

#define IS_ALIGNED(v) \
    (v & (ALIGNMENT - 1))

#define IS_PAGE_ALIGNED(v) \
    (v & (g_page_size - 1))

#define GET_BIT(n, k) \
    (n >> k) & 1UL

#define SET_BIT(n, k) \
    n |= 1UL << k;

#define UNSET_BIT(n, k) \
    n &= ~(1UL << k);

#define ALIGN_SZ_UP(n) \
    ((((n + ALIGNMENT) - 1) >> 3) * ALIGNMENT)

#define ALIGN_SZ_DOWN(n) \
    ((((n + ALIGNMENT) - 1) >> 3) * ALIGNMENT) - ALIGNMENT

#define ROUND_UP_PAGE(n) \
    ((((n + g_page_size) - 1) >> g_page_size_shift) * (g_page_size))

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

#define UNMASK_USER_PTR(zone) \
    (void *) ((uintptr_t) zone->user_pages_start ^ (uintptr_t) zone->pointer_mask)

#define UNMASK_BITMAP_PTR(zone) \
    (void *) ((uintptr_t) zone->bitmap_start ^ (uintptr_t) zone->pointer_mask)

#define MASK_BIG_ZONE_NEXT(bnp) \
    UNMASK_BIG_ZONE_NEXT(bnp)

#define UNMASK_BIG_ZONE_NEXT(bnp) \
    ((iso_alloc_big_zone_t *) ((uintptr_t) _root->big_zone_next_mask ^ (uintptr_t) bnp))

/* Each user allocation zone we make is 4mb in size.
 * With MAX_ZONES at 8192 this means we top out at
 * about 32~ gb of heap. If you adjust this then
 * you need to make sure that SMALL_SZ_MAX is correctly
 * adjusted or you will calculate chunks outside of
 * the zone user memory! */
#define ZONE_USER_SIZE 4194304

/* This is the largest divisor of ZONE_USER_SIZE we can
 * get from (BITS_PER_QWORD/BITS_PER_CHUNK). Anything
 * above this size will need to go through the big
 * mapping code path */
#define SMALL_SZ_MAX 131072

/* Cap our big zones at 4GB of memory */
#define BIG_SZ_MAX 4294967296

#define WASTED_SZ_MULTIPLIER 8
#define WASTED_SZ_MULTIPLIER_SHIFT 3

#define BIG_ZONE_META_DATA_PAGE_COUNT 3
#define BIG_ZONE_USER_PAGE_COUNT 2
#define BIG_ZONE_USER_PAGE_COUNT_SHIFT 1

#define TAGGED_PTR_MASK 0x00ffffffffffffff
#define IS_TAGGED_PTR_MASK 0xff00000000000000
#define UNTAGGED_BITS 56

/* A uint64_t of bitslots below this value will
 * have at least 1 single free bit slot */
#define ALLOCATED_BITSLOTS 0x5555555555555555

#define MEGABYTE_SIZE 1048576
#define KILOBYTE_SIZE 1024

/* We don't validate the last byte of the canary.
 * It is always 0 to prevent an out of bounds read
 * from exposing it's value */
#define CANARY_VALIDATE_MASK 0xffffffffffffff00

#define BAD_BIT_SLOT -1

/* Calculate the user pointer given a zone and a bit slot */
#define POINTER_FROM_BITSLOT(zone, bit_slot) \
    ((void *) zone->user_pages_start + ((bit_slot >> 1) * zone->chunk_size));

/* This global is used by the page rounding macros.
 * The value stored in _root->system_page_size is
 * preferred but we need this to setup the root. */
extern uint32_t g_page_size;

/* We need to know what power of 2 the page size is */
extern uint32_t g_page_size_shift;

/* iso_alloc makes a number of default zones for common
 * allocation sizes. Allocations are 'first fit' up until
 * ZONE_1024 at which point a new zone is created for that
 * specific size request. */
#define DEFAULT_ZONE_COUNT sizeof(default_zones) >> 3

#define MEM_TAG_SIZE 1

#if SMALLEST_CHUNK_SZ < ZONE_8
#error "Smallest chunk size is 8 bytes, 16 is recommended!"
#endif

#if THREAD_SUPPORT
#if USE_SPINLOCK
extern atomic_flag root_busy_flag;
#define LOCK_ROOT() \
    do {            \
    } while(atomic_flag_test_and_set(&root_busy_flag));

#define UNLOCK_ROOT() \
    atomic_flag_clear(&root_busy_flag);

#define LOCK_BIG_ZONE() \
    do {                \
    } while(atomic_flag_test_and_set(&_root->big_zone_busy_flag));

#define UNLOCK_BIG_ZONE() \
    atomic_flag_clear(&_root->big_zone_busy_flag);
#else
extern pthread_mutex_t root_busy_mutex;
#define LOCK_ROOT() \
    pthread_mutex_lock(&root_busy_mutex);

#define UNLOCK_ROOT() \
    pthread_mutex_unlock(&root_busy_mutex);

#define LOCK_BIG_ZONE() \
    pthread_mutex_lock(&_root->big_zone_busy_mutex);

#define UNLOCK_BIG_ZONE() \
    pthread_mutex_unlock(&_root->big_zone_busy_mutex);
#endif
#else
#define LOCK_ROOT()
#define UNLOCK_ROOT()
#define LOCK_BIG_ZONE()
#define UNLOCK_BIG_ZONE()
#endif

#if NO_ZERO_ALLOCATIONS
extern void *_zero_alloc_page;
#endif

/* The global root */
extern iso_alloc_root *_root;

INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone_t *big);
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone_t *zone, const void *p);
INTERNAL_HIDDEN INLINE void iso_clear_user_chunk(uint8_t *p, size_t size);
INTERNAL_HIDDEN INLINE void insert_free_bit_slot(iso_alloc_zone_t *zone, int64_t bit_slot);
INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone_t *zone, void *p);
INTERNAL_HIDDEN INLINE void populate_zone_cache(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN INLINE void flush_chunk_quarantine(void);
INTERNAL_HIDDEN INLINE void clear_zone_cache(void);
INTERNAL_HIDDEN iso_alloc_zone_t *is_zone_usable(iso_alloc_zone_t *zone, size_t size);
INTERNAL_HIDDEN iso_alloc_zone_t *find_suitable_zone(size_t size);
INTERNAL_HIDDEN iso_alloc_zone_t *iso_new_zone(size_t size, bool internal);
INTERNAL_HIDDEN iso_alloc_zone_t *_iso_new_zone(size_t size, bool internal, int32_t index);
INTERNAL_HIDDEN iso_alloc_zone_t *iso_find_zone_bitmap_range(const void *p);
INTERNAL_HIDDEN iso_alloc_zone_t *iso_find_zone_range(const void *p);
INTERNAL_HIDDEN iso_alloc_zone_t *search_chunk_lookup_table(const void *p);
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot_slow(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN bit_slot_t get_next_free_bit_slot(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN iso_alloc_root *iso_alloc_new_root(void);
INTERNAL_HIDDEN bool is_pow2(uint64_t sz);
INTERNAL_HIDDEN bool _is_zone_retired(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN bool _refresh_zone_mem_tags(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN iso_alloc_zone_t *_iso_free_internal_unlocked(void *p, bool permanent, iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void fill_free_bit_slot_cache(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void flush_caches(void);
INTERNAL_HIDDEN void iso_free_chunk_from_zone(iso_alloc_zone_t *zone, void *p, bool permanent);
INTERNAL_HIDDEN void create_canary_chunks(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void iso_alloc_initialize_global_root(void);
INTERNAL_HIDDEN void _iso_alloc_destroy_zone_unlocked(iso_alloc_zone_t *zone, bool flush_caches, bool replace);
INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void _verify_zone(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void _verify_all_zones(void);
INTERNAL_HIDDEN void verify_zone(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void verify_all_zones(void);
INTERNAL_HIDDEN void _iso_free(void *p, bool permanent);
INTERNAL_HIDDEN void _iso_free_internal(void *p, bool permanent);
INTERNAL_HIDDEN void _iso_free_size(void *p, size_t size);
INTERNAL_HIDDEN void _iso_free_from_zone(void *p, iso_alloc_zone_t *zone, bool permanent);
INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone_t *big_zone, bool permanent);
INTERNAL_HIDDEN void _iso_alloc_protect_root(void);
INTERNAL_HIDDEN void _iso_free_quarantine(void *p);
INTERNAL_HIDDEN void _iso_alloc_unprotect_root(void);
INTERNAL_HIDDEN void *_tag_ptr(void *p, iso_alloc_zone_t *zone);
INTERNAL_HIDDEN void *_untag_ptr(void *p, iso_alloc_zone_t *zone);
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_big_alloc(size_t size);
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_alloc(iso_alloc_zone_t *zone, size_t size);
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_alloc_bitslot_from_zone(bit_slot_t bitslot, iso_alloc_zone_t *zone);
INTERNAL_HIDDEN ASSUME_ALIGNED void *_iso_calloc(size_t nmemb, size_t size);
INTERNAL_HIDDEN void *_iso_alloc_ptr_search(void *n, bool poison);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone_t *zone, bool profile);
INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks_in_zone(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks(void);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN uint64_t __iso_alloc_zone_mem_usage(iso_alloc_zone_t *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_big_zone_mem_usage();
INTERNAL_HIDDEN uint64_t __iso_alloc_big_zone_mem_usage();
INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage(void);
INTERNAL_HIDDEN uint64_t __iso_alloc_mem_usage(void);
INTERNAL_HIDDEN uint64_t rand_uint64(void);
INTERNAL_HIDDEN uint8_t _iso_alloc_get_mem_tag(void *p, iso_alloc_zone_t *zone);
INTERNAL_HIDDEN size_t _iso_alloc_print_stats();
INTERNAL_HIDDEN size_t _iso_chunk_size(void *p);
INTERNAL_HIDDEN int64_t check_canary_no_abort(iso_alloc_zone_t *zone, const void *p);
INTERNAL_HIDDEN void _iso_alloc_initialize(void);
INTERNAL_HIDDEN void _iso_alloc_destroy(void);

#if EXPERIMENTAL
INTERNAL_HIDDEN void _iso_alloc_search_stack(uint8_t *stack_start);
#endif

#if UNIT_TESTING
EXTERNAL_API iso_alloc_root *_get_root(void);
#endif
