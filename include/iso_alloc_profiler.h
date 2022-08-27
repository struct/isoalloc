/* iso_alloc_profiler.h - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#pragma once

#if HEAP_PROFILER
#include "compiler.h"

#define PROFILER_ODDS 10000
#define HG_SIZE 65535
#define CHUNK_USAGE_THRESHOLD 75
#define PROFILER_ENV_STR "ISO_ALLOC_PROFILER_FILE_PATH"
#define PROFILER_FILE_PATH "iso_alloc_profiler.data"
#define BACKTRACE_DEPTH 8
#define BACKTRACE_DEPTH_SZ 128

/* The IsoAlloc profiler is not thread local but these
 * globals should only ever be touched by internal
 * allocator functions when the root is locked */
uint64_t _alloc_count;
uint64_t _free_count;
uint64_t _alloc_sampled_count;
uint64_t _free_sampled_count;

int32_t profiler_fd;

typedef struct {
    uint64_t total;
    uint64_t count;
} zone_profiler_map_t;

zone_profiler_map_t _zone_profiler_map[SMALL_SZ_MAX];

/* iso_alloc_traces_t is a public structure, and
 * is defined in the public header iso_alloc.h */
iso_alloc_traces_t _alloc_bts[BACKTRACE_DEPTH_SZ];
size_t _alloc_bts_count;

/* iso_free_traces_t is a public structure, and
 * is defined in the public header iso_alloc.h */
iso_free_traces_t _free_bts[BACKTRACE_DEPTH_SZ];
size_t _free_bts_count;

INTERNAL_HIDDEN INLINE uint64_t _get_backtrace_hash(void);
INTERNAL_HIDDEN INLINE void _save_backtrace(iso_alloc_traces_t *abts);
INTERNAL_HIDDEN INLINE uint64_t _call_count_from_hash(uint16_t hash);
INTERNAL_HIDDEN void _iso_output_profile(void);
INTERNAL_HIDDEN void _initialize_profiler(void);
INTERNAL_HIDDEN void _iso_alloc_profile(size_t size);
INTERNAL_HIDDEN void _iso_free_profile(void);
INTERNAL_HIDDEN size_t _iso_get_alloc_traces(iso_alloc_traces_t *traces_out);
INTERNAL_HIDDEN size_t _iso_get_free_traces(iso_free_traces_t *traces_out);
INTERNAL_HIDDEN void _iso_alloc_reset_traces();
#endif
