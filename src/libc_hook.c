/* libc_hook.c - Provides low level hooks for libc functions
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"
#include "iso_alloc_sanity.h"
#include "iso_alloc_util.h"

#if MEMCPY_SANITY
EXTERNAL_API void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    return _iso_alloc_memcpy(dest, src, n);
}
#endif

#if MEMSET_SANITY
EXTERNAL_API void *memset(void *dest, int b, size_t n) {
    return _iso_alloc_memset(dest, b, n);
}
#endif
