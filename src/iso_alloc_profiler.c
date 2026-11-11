/* iso_alloc_profiler.c - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

#if MEMCPY_SANITY || MEMSET_SANITY
#include "iso_alloc_sanity.h"
#endif

#if HEAP_PROFILER
#include "iso_alloc_profiler.h"
#endif

#include <dlfcn.h>

#if HEAP_PROFILER
uint64_t _alloc_count;
uint64_t _free_count;
uint64_t _alloc_sampled_count;
uint64_t _free_sampled_count;
int32_t profiler_fd;
zone_profiler_map_t _zone_profiler_map[SMALL_SIZE_MAX + 1];
iso_alloc_traces_t _alloc_bts[BACKTRACE_DEPTH_SZ];
size_t _alloc_bts_count;
iso_free_traces_t _free_bts[BACKTRACE_DEPTH_SZ];
size_t _free_bts_count;
#endif

INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks_in_zone(iso_alloc_zone_t *zone) {
    LOCK_ROOT();
    uint64_t leaks = _iso_alloc_zone_leak_detector(zone, false);
    UNLOCK_ROOT();
    return leaks;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage(void) {
    LOCK_ROOT();
    uint64_t mem_usage = __iso_alloc_mem_usage();
    mem_usage += _iso_alloc_big_zone_mem_usage();
    UNLOCK_ROOT();
    return mem_usage;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_big_zone_mem_usage(void) {
    LOCK_BIG_ZONE_USED();
    uint64_t mem_usage = __iso_alloc_big_zone_mem_usage(_root->big_zone_used);
    UNLOCK_BIG_ZONE_USED();

    LOCK_BIG_ZONE_FREE();
    mem_usage += __iso_alloc_big_zone_mem_usage(_root->big_zone_free);
    UNLOCK_BIG_ZONE_FREE();

    return mem_usage;
}

INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone_t *zone) {
    LOCK_ROOT();
    uint64_t zone_mem_usage = __iso_alloc_zone_mem_usage(zone);
    UNLOCK_ROOT();
    return zone_mem_usage;
}

#if DEBUG && MEM_USAGE
INTERNAL_HIDDEN size_t _iso_alloc_print_stats(void) {
    struct rusage _rusage = {0};

    int32_t ret = getrusage(RUSAGE_SELF, &_rusage);

    if(ret == ERR) {
        return ERR;
    }

#if __linux__ || __FreeBSD__
    LOG("RSS: %d (mb)", (_rusage.ru_maxrss / KILOBYTE_SIZE));
#elif __APPLE__
    LOG("RSS: %d (mb)", (_rusage.ru_maxrss / MEGABYTE_SIZE));
#endif
    LOG("Soft Page Faults: %d", _rusage.ru_minflt);
    LOG("Hard Page Faults: %d", _rusage.ru_majflt);
    return OK;
}
#endif

INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks(void) {
    uint64_t total_leaks = 0;
    uint64_t big_leaks = 0;

    LOCK_ROOT();

    for(uint16_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];
        total_leaks += _iso_alloc_zone_leak_detector(zone, false);
    }

    UNLOCK_ROOT();
    LOCK_BIG_ZONE_USED();

    iso_alloc_big_zone_t *big = _root->big_zone_used;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(_root->big_zone_used);
    }

    /* All allocations on the used list are 'leaked' */
    while(big != NULL) {
        big_leaks += big->size;
        LOG("Big zone leaked %lu bytes", big->size);

        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
        }
    }

    UNLOCK_BIG_ZONE_USED();

    LOG("Total leaked in big zones: bytes (%lu) megabytes (%lu)", big_leaks, (big_leaks / MEGABYTE_SIZE));
    return total_leaks + big_leaks;
}

/* This is the built-in leak detector. It works by scanning
 * the bitmap for every allocated zone and looking for
 * uncleared bits. This does not search for references from
 * a root like a GC, so if you purposefully did not free a
 * chunk then expect it to show up as leaked! */
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone_t *zone, bool profile) {
    uint32_t in_use = 0;

#if LEAK_DETECTOR || HEAP_PROFILER
    if(zone == NULL) {
        return 0;
    }

    UNMASK_ZONE_PTRS(zone);

    bitmap_index_t *bm = (bitmap_index_t *) zone->bitmap_start;
    uint32_t was_used = 0;
    int64_t bms = zone->bitmap_size / sizeof(bitmap_index_t);

    for(int64_t i = 0; i < bms;) {
#if USE_NEON
        /* Two-qword quick-reject: load 16 bytes of bitmap and skip both
         * qwords when every chunk in those 64 slots is free and never used. */
        if(i + 1 < bms) {
            int64x2_t v = vld1q_s64((const int64_t *) &bm[i]);
            if((vgetq_lane_s64(v, 0) | vgetq_lane_s64(v, 1)) == 0) {
                i += 2;
                continue;
            }
        }
#endif
        uint64_t bts = (uint64_t) bm[i];
        if(bts == 0) {
            i++;
            continue;
        }

        /* was_used (encoding 01: low=0, high=1) — popcount the odd-bit
         * positions whose paired even bit is clear. */
        was_used += __builtin_popcountll((~bts) & (bts >> 1) & USED_BIT_VECTOR);

        /* Chunks with low bit set are either in-use (10) or canary (11).
         * Walk just those positions with ctz instead of testing all 32. */
        uint64_t in_use_low = bts & USED_BIT_VECTOR;
        while(in_use_low) {
            int j = __builtin_ctzll(in_use_low);
            in_use_low &= in_use_low - 1;

            int64_t bit_two = GET_BIT(bts, (j + 1));
            bit_slot_t bit_slot = ((bitmap_index_t) i * BITS_PER_QWORD) + j;
            const void *leak = (zone->user_pages_start + ((bit_slot >> 1) * zone->chunk_size));

            if(bit_two == 1 && (check_canary_no_abort(zone, leak) != ERR)) {
                continue;
            } else {
                in_use++;

                if(profile == false) {
                    LOG("Leaked chunk (%d) in zone[%d] of %d bytes detected at 0x%p (bit position = %d)", in_use, zone->index, zone->chunk_size, leak, bit_slot);
                }
            }
        }
        i++;
    }

    if(profile == false) {
        LOG("Zone[%d] Total number of %d byte chunks(%d) used and free'd (%d) (%d percent), in use = %d", zone->index, zone->chunk_size, zone->chunk_count,
            was_used, (int32_t) (((float) was_used / zone->chunk_count) * 100), in_use);
    }

    MASK_ZONE_PTRS(zone);
#endif

#if HEAP_PROFILER
    /* When profiling this zone we want to capture
     * the total number of allocations both currently
     * in use and previously used by this zone */
    if(profile == true) {
        return (uint64_t) (in_use + was_used);
    }
#endif
    return in_use;
}

INTERNAL_HIDDEN uint64_t __iso_alloc_zone_mem_usage(iso_alloc_zone_t *zone) {
    uint64_t mem_usage = 0;
    mem_usage += zone->bitmap_size;
    mem_usage += ZONE_USER_SIZE;
    LOG("Zone[%d] holds %d byte chunks. Total bytes (%lu), megabytes (%lu)", zone->index, zone->chunk_size,
        mem_usage, (mem_usage / MEGABYTE_SIZE));
    return (mem_usage / MEGABYTE_SIZE);
}

INTERNAL_HIDDEN uint64_t __iso_alloc_mem_usage(void) {
    uint64_t mem_usage = 0;

    for(uint16_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];
        mem_usage += zone->bitmap_size;
        mem_usage += ZONE_USER_SIZE;
        LOG("Zone[%d] holds %d byte chunks, megabytes (%d) next zone = %d, total allocations = %d, in use = %d", zone->index, zone->chunk_size,
            (ZONE_USER_SIZE / MEGABYTE_SIZE), zone->next_sz_index, zone->alloc_count, zone->af_count);
    }

    return (mem_usage / MEGABYTE_SIZE);
}

INTERNAL_HIDDEN uint64_t __iso_alloc_big_zone_mem_usage(iso_alloc_big_zone_t *head) {
    uint64_t mem_usage = 0;
    iso_alloc_big_zone_t *big = head;

    if(big != NULL) {
        big = UNMASK_BIG_ZONE_NEXT(head);
    }

    while(big != NULL) {
        size_t total_size = big->size + (g_page_size * BIG_ZONE_META_DATA_PAGE_COUNT);
        /* Meta data has 2 guard pages, user data has 2 guard pages */
#if BIG_ZONE_META_DATA_GUARD
        total_size += (g_page_size * 4);
#else
        total_size += (g_page_size * 2);
#endif
        LOG("Big Zone Total bytes mapped (%lu), megabytes (%lu)", big->size, (big->size / MEGABYTE_SIZE));
        mem_usage += total_size;
        if(big->next != NULL) {
            big = UNMASK_BIG_ZONE_NEXT(big->next);
        } else {
            big = NULL;
        }
    }

    if(mem_usage != 0) {
        LOG("Total megabytes allocated (%lu)", (mem_usage / MEGABYTE_SIZE));
        return (mem_usage / MEGABYTE_SIZE);
    } else {
        return 0;
    }
}

#if HEAP_PROFILER

/* Returns a documented data structure that can
 * be used to interpret allocation patterns */
INTERNAL_HIDDEN size_t _iso_get_alloc_traces(iso_alloc_traces_t *traces_out) {
    LOCK_ROOT();
    size_t sz = _alloc_bts_count;
    __iso_memcpy(traces_out, _alloc_bts, sz * sizeof(iso_alloc_traces_t));
    UNLOCK_ROOT();
    return sz;
}

INTERNAL_HIDDEN size_t _iso_get_free_traces(iso_free_traces_t *traces_out) {
    LOCK_ROOT();
    size_t sz = _free_bts_count;
    __iso_memcpy(traces_out, _free_bts, sz * sizeof(iso_free_traces_t));
    UNLOCK_ROOT();
    return sz;
}

INTERNAL_HIDDEN void _iso_alloc_reset_traces(void) {
    LOCK_ROOT();
    __iso_memset(_alloc_bts, 0x0, sizeof(_alloc_bts));
    __iso_memset(_free_bts, 0x0, sizeof(_free_bts));
    _alloc_bts_count = 0;
    _free_bts_count = 0;
    UNLOCK_ROOT();
}

/* Walks the frame pointer chain. Both x86_64 and aarch64
 * save the previous frame pointer at fp[0] and the return
 * address at fp[1] when built with frame pointers. We walk
 * the chain ourselves because __builtin_return_address(n)
 * for n > 0 is a constant 0 on aarch64 gcc, which left the
 * profiler without any backtraces on that target. The walk
 * stops at the first frame that is NULL, misaligned, or does
 * not sit above the current one on the stack */
static INLINE uintptr_t *_next_frame(uintptr_t *fp) {
    uintptr_t *next = (uintptr_t *) fp[0];

    if(next <= fp || ((uintptr_t) next & (sizeof(uintptr_t) * 2 - 1)) != 0) {
        return NULL;
    }

    return next;
}

INTERNAL_HIDDEN INLINE uint64_t _get_backtrace_hash(void) {
    uint64_t hash = 0;
    uintptr_t *fp = (uintptr_t *) __builtin_frame_address(0);

    for(size_t i = 0; i < BACKTRACE_DEPTH && fp != NULL; i++) {
        hash ^= (uint64_t) fp[1];
        fp = _next_frame(fp);
    }

    return hash;
}

/* Stores up to BACKTRACE_DEPTH return addresses in callers */
static INLINE void _save_backtrace_frames(uint64_t *callers) {
    uintptr_t *fp = (uintptr_t *) __builtin_frame_address(0);

    for(size_t i = 0; i < BACKTRACE_DEPTH && fp != NULL; i++) {
        uint64_t r = (uint64_t) fp[1];

        if(r > 0x1000) {
            callers[i] = r;
        }

        fp = _next_frame(fp);
    }
}

INTERNAL_HIDDEN INLINE void _save_alloc_backtrace(iso_alloc_traces_t *abts) {
    _save_backtrace_frames(abts->callers);
}

INTERNAL_HIDDEN INLINE void _save_free_backtrace(iso_free_traces_t *fbts) {
    _save_backtrace_frames(fbts->callers);
}

INTERNAL_HIDDEN void _iso_output_profile(void) {
    _iso_alloc_printf(profiler_fd, "allocated=%lu\n", _alloc_count);
    _iso_alloc_printf(profiler_fd, "alloc_sampled=%lu\n", _alloc_sampled_count);
    _iso_alloc_printf(profiler_fd, "freed=%lu\n", _free_count);
    _iso_alloc_printf(profiler_fd, "free_sampled=%lu\n", _free_sampled_count);

    for(uint16_t i = 0; i < _root->zones_used; i++) {
        iso_alloc_zone_t *zone = &_root->zones[i];
        _zone_profiler_map[zone->chunk_size].total++;
    }

    for(size_t i = 0; i < _alloc_bts_count; i++) {
        iso_alloc_traces_t *abts = &_alloc_bts[i];
        _iso_alloc_printf(profiler_fd, "alloc_backtrace=%lu,backtrace_hash=0x%x,calls=%lu,lower_bound_size=%lu,upper_bound_size=%lu\n",
                          i, (uint64_t) abts->backtrace_hash, abts->call_count, abts->lower_bound_size, abts->upper_bound_size);

        for(int j = 0; j < BACKTRACE_DEPTH; j++) {
            if(abts->callers[j] < 0x1000) {
                continue;
            }

            Dl_info dl = {0};

            if(dladdr((void *) abts->callers[j], &dl) != 0 && dl.dli_sname != NULL) {
                _iso_alloc_printf(profiler_fd, "\t0x%x -> %s %s\n", abts->callers[j], dl.dli_sname, dl.dli_fname);
            } else {
                _iso_alloc_printf(profiler_fd, "\t0x%x -> [?]\n", abts->callers[j]);
            }
        }
    }

    for(size_t i = 0; i < _free_bts_count; i++) {
        iso_free_traces_t *fbts = &_free_bts[i];
        _iso_alloc_printf(profiler_fd, "free_backtrace=%lu,backtrace_hash=0x%x,calls=%lu\n",
                          i, (uint64_t) fbts->backtrace_hash, fbts->call_count);

        for(int j = 0; j < BACKTRACE_DEPTH; j++) {
            if(fbts->callers[j] < 0x1000) {
                continue;
            }

            Dl_info dl = {0};

            if(dladdr((void *) fbts->callers[j], &dl) != 0 && dl.dli_sname != NULL) {
                _iso_alloc_printf(profiler_fd, "\t0x%x -> %s %s\n", fbts->callers[j], dl.dli_sname, dl.dli_fname);
            } else {
                _iso_alloc_printf(profiler_fd, "\t0x%x -> [?]\n", fbts->callers[j]);
            }
        }
    }

    for(int i = 0; i <= SMALL_SIZE_MAX; i++) {
        if(_zone_profiler_map[i].count != 0) {
            _iso_alloc_printf(profiler_fd, "%d,%lu,%lu\n", i, _zone_profiler_map[i].total, _zone_profiler_map[i].count);
        }
    }

    if(profiler_fd != ERR) {
        close(profiler_fd);
        profiler_fd = ERR;
    }
}

INTERNAL_HIDDEN void _iso_alloc_profile(size_t size) {
    _alloc_count++;

    /* Don't run the profiler on every allocation */
    if(LIKELY((us_rand_uint64(&_root->seed) % PROFILER_ODDS) != 1)) {
        return;
    }

    _alloc_sampled_count++;

    for(uint16_t i = 0; i < _root->zones_used; i++) {
        uint32_t used = 0;
        iso_alloc_zone_t *zone = &_root->zones[i];

        /* For the purposes of the profiler we don't care about
         * the differences between canary and leaked chunks.
         * So lets just use the full count */
        if(zone->is_full) {
            used = zone->chunk_count;
        } else {
            used = _iso_alloc_zone_leak_detector(zone, true);
        }

        /* Convert the chunk count to a percentage of the zone */
        used = (uint32_t) (((float) used / zone->chunk_count) * 100.0f);

        if(used > CHUNK_USAGE_THRESHOLD) {
            _zone_profiler_map[zone->chunk_size].count++;
        }
    }

    if(_alloc_bts_count < BACKTRACE_DEPTH_SZ) {
        iso_alloc_traces_t *abts = NULL;
        uint16_t hash = (_get_backtrace_hash() & HG_SIZE);

        /* Take the backtrace hash and determine if its already been seen */
        for(size_t i = 0; i < _alloc_bts_count; i++) {
            if(_alloc_bts[i].backtrace_hash == hash) {
                abts = &_alloc_bts[i];

                if(abts->lower_bound_size == 0 || size < abts->lower_bound_size) {
                    abts->lower_bound_size = size;
                }

                if(abts->upper_bound_size == 0 || size > abts->upper_bound_size) {
                    abts->upper_bound_size = size;
                }

                abts->call_count++;
                break;
            }
        }

        /* We haven't seen this backtrace before */
        if(abts == NULL) {
            abts = &_alloc_bts[_alloc_bts_count];
            abts->backtrace_hash = hash;

            _save_alloc_backtrace(abts);
            _alloc_bts_count++;
        }
    }
}

/* Must be called with the root locked, it touches
 * the profiler globals and the root seed */
INTERNAL_HIDDEN void _iso_free_profile(void) {
    _free_count++;

    /* Don't run the profiler on every allocation */
    if(LIKELY((us_rand_uint64(&_root->seed) % PROFILER_ODDS) != 1)) {
        return;
    }

    _free_sampled_count++;

    if(_free_bts_count < BACKTRACE_DEPTH_SZ) {
        iso_free_traces_t *fbts = NULL;
        uint16_t hash = (_get_backtrace_hash() & HG_SIZE);

        /* Take the backtrace hash and determine if its already been seen */
        for(size_t i = 0; i < _free_bts_count; i++) {
            if(_free_bts[i].backtrace_hash == hash) {
                fbts = &_free_bts[i];
                fbts->call_count++;
                break;
            }
        }

        /* We haven't seen this backtrace before */
        if(fbts == NULL) {
            fbts = &_free_bts[_free_bts_count];
            fbts->backtrace_hash = hash;

            _save_free_backtrace(fbts);
            _free_bts_count++;
        }
    }
}

INTERNAL_HIDDEN void _initialize_profiler(void) {
    /* We don't need thread safety for this file descriptor
     * as long as we guarantee to never use it if the root
     * is not locked */
    if(getenv(PROFILER_ENV_STR) != NULL) {
        profiler_fd = open(getenv(PROFILER_ENV_STR), O_RDWR | O_CREAT | O_SYNC, 0666);
    } else {
        profiler_fd = open(PROFILER_FILE_PATH, O_RDWR | O_CREAT | O_SYNC, 0666);
    }

    if(profiler_fd == ERR) {
        LOG_AND_ABORT("Cannot open file descriptor for %s", PROFILER_FILE_PATH);
    }
}
#endif
