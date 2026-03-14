/* malloc_hook.c - Provides low level hooks for malloc/free
 * Copyright 2023 - chris.rohlf@gmail.com */

/* This file must be included from iso_alloc_interfaces.c so that
 * alias targets (iso_alloc, iso_free, etc.) are defined in the same
 * translation unit. When compiled directly the guard
 * below produces an empty translation unit with no effect. */
#if !defined(ISO_IN_INTERFACES_C)
#error "malloc_hook.c must be included from iso_alloc_interfaces.c (so aliases can work)"
#endif

#include "iso_alloc_hook.h"

/* The MALLOC_HOOK configuration allows us to hook the usual
 * malloc interfaces and redirect them to the iso_alloc API.
 * This may not be desired, especially if you intend to call
 * iso_alloc interfaces directly. These hook points are
 * useful because they allow us to use iso_alloc even in
 * existing and closed source programs that call malloc/free
 */
#if MALLOC_HOOK

/* malloc/free/calloc/realloc and their __libc_ variants are
 * direct linker aliases for the iso_ equivalents on GCC >= 9
 * and Clang >= 10 (see iso_alloc_hook.h). This propagates all
 * function attributes (malloc, alloc_size, nothrow, etc.) from
 * the iso_ symbol to the exported symbol via copy(fun), and
 * eliminates the wrapper call entirely.
 *
 * Functions with differing signatures or custom logic (posix_memalign,
 * aligned_alloc, memalign, malloc_size, malloc_good_size) remain
 * as wrapper functions. */

EXTERNAL_API void *__libc_malloc(size_t s) ISO_FORWARD1(iso_alloc, s)
EXTERNAL_API void *malloc(size_t s) ISO_FORWARD1(iso_alloc, s)

EXTERNAL_API void __libc_free(void *p) ISO_FORWARD0(iso_free, p)
EXTERNAL_API void free(void *p) ISO_FORWARD0(iso_free, p)

EXTERNAL_API void *__libc_calloc(size_t n, size_t s) ISO_FORWARD2(iso_calloc, n, s)
EXTERNAL_API void *calloc(size_t n, size_t s) ISO_FORWARD2(iso_calloc, n, s)

EXTERNAL_API void *__libc_realloc(void *p, size_t s) ISO_FORWARD2(iso_realloc, p, s)
EXTERNAL_API void *realloc(void *p, size_t s) ISO_FORWARD2(iso_realloc, p, s)

EXTERNAL_API void *__libc_reallocarray(void *p, size_t n, size_t s) ISO_FORWARD3(iso_reallocarray, p, n, s)
EXTERNAL_API void *reallocarray(void *p, size_t n, size_t s) ISO_FORWARD3(iso_reallocarray, p, n, s)

EXTERNAL_API int __posix_memalign(void **r, size_t a, size_t s) {
    if(is_pow2(a) == false) {
        *r = NULL;
        return EINVAL;
    }

    if(s < a) {
        s = a;
    }

    *r = iso_alloc(s);

    if(*r != NULL) {
        return 0;
    } else {
        return ENOMEM;
    }
}

EXTERNAL_API int posix_memalign(void **r, size_t alignment, size_t s) {
    return __posix_memalign(r, alignment, s);
}

EXTERNAL_API void *__libc_memalign(size_t alignment, size_t s) {
    /* All iso_alloc allocations are 8 byte aligned */
    return iso_alloc(s);
}

EXTERNAL_API void *aligned_alloc(size_t alignment, size_t s) {
    /* All iso_alloc allocations are 8 byte aligned */
    return iso_alloc(s);
}

EXTERNAL_API void *memalign(size_t alignment, size_t s) {
    /* All iso_alloc allocations are 8 byte aligned */
    return iso_alloc(s);
}

#if __ANDROID__ || __FreeBSD__
EXTERNAL_API size_t malloc_usable_size(const void *ptr) {
    return iso_chunksz((void *) ptr);
}
#elif __APPLE__
EXTERNAL_API size_t malloc_size(const void *ptr) {
    return iso_chunksz((void *) ptr);
}

EXTERNAL_API size_t malloc_good_size(size_t size) {
    return ALIGN_SZ_UP(size);
}
#else
/* On Linux (non-Android) malloc_usable_size takes void* matching iso_chunksz */
EXTERNAL_API size_t malloc_usable_size(void *ptr) ISO_FORWARD1(iso_chunksz, ptr)
#endif

static void *libc_malloc(size_t s, const void *caller) {
    return iso_alloc(s);
}
static void *libc_realloc(void *ptr, size_t s, const void *caller) {
    return iso_realloc(ptr, s);
}
static void libc_free(void *ptr, const void *caller) {
    iso_free(ptr);
}
static void *libc_memalign(size_t alignment, size_t s, const void *caller) {
    return iso_alloc(s);
}

#if !__ANDROID__
void *(*__malloc_hook)(size_t, const void *) = &libc_malloc;
void *(*__realloc_hook)(void *, size_t, const void *) = &libc_realloc;
void (*__free_hook)(void *, const void *) = &libc_free;
void *(*__memalign_hook)(size_t, size_t, const void *) = &libc_memalign;
#endif
#endif
