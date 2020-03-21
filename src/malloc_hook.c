/* malloc_hook.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

/* The MALLOC_HOOK configuration allows us to hook the usual
 * malloc interfaces and redirect them to the iso_alloc API.
 * This may not be desired, especially if you intend to call
 * iso_alloc interfaces directly. These hook points are
 * useful because they allow us to use iso_alloc even in
 * existing and closed source programs that call malloc/free.
 * Using this requires LD_PRELOAD so it is not recommended! */
#if MALLOC_HOOK

EXTERNAL_API void *__libc_malloc(size_t s) {
    return iso_alloc(s);
}

EXTERNAL_API void *malloc(size_t s) {
    return iso_alloc(s);
}

EXTERNAL_API void __libc_free(void *p) {
    iso_free(p);
}

EXTERNAL_API void free(void *p) {
    iso_free(p);
}

EXTERNAL_API void *__libc_calloc(size_t n, size_t s) {
    return iso_calloc(n, s);
}

EXTERNAL_API void *calloc(size_t n, size_t s) {
    return iso_calloc(n, s);
}

EXTERNAL_API void *__libc_realloc(void *p, size_t s) {
    return iso_realloc(p, s);
}

EXTERNAL_API void *realloc(void *p, size_t s) {
    return iso_realloc(p, s);
}

EXTERNAL_API int __posix_memalign(void **r, size_t a, size_t s) {
    /* All iso_alloc allocations are 8 byte aligned */
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

EXTERNAL_API void *__libc_memalign(size_t align, size_t s) {
    /* All iso_alloc allocations are 8 byte aligned */
    return iso_alloc(s);
}

EXTERNAL_API void *memalign(size_t alignment, size_t s) {
    /* All iso_alloc allocations are 8 byte aligned */
    return iso_alloc(s);
}

static void *libc_malloc(size_t s, const void *caller) {
    return iso_alloc(s);
}
static void *libc_realloc(void *ptr, size_t s, const void *caller) {
    return iso_realloc(ptr, s);
}
static void libc_free(void *ptr, const void *caller) {
    iso_free(ptr);
}
static void *libc_memalign(size_t align, size_t s, const void *caller) {
    return iso_alloc(s);
}

void *(*__malloc_hook)(size_t, const void *) = &libc_malloc;
void *(*__realloc_hook)(void *, size_t, const void *) = &libc_realloc;
void (*__free_hook)(void *, const void *) = &libc_free;
void *(*__memalign_hook)(size_t, size_t, const void *) = &libc_memalign;
#endif
