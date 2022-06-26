/* libc_hook.c - Provides low level hooks for libc functions
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"
#include "iso_alloc_sanity.h"

#if MEMCPY_SANITY

EXTERNAL_API void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    return _iso_alloc_memcpy(dest, src, n);
}

#endif
