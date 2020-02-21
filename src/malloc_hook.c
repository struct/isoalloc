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
 * Using this requires LD_PRELOAD so it is not recommended. */
#if MALLOC_HOOK
EXTERNAL_API void *malloc(size_t size) {
    return iso_alloc(size);
}

EXTERNAL_API void free(void *p) {
    iso_free(p);
    return;
}

EXTERNAL_API void *calloc(size_t nmemb, size_t size) {
    return iso_calloc(nmemb, size);
}

EXTERNAL_API void *realloc(void *p, size_t size) {
    return iso_realloc(p, size);
}

EXTERNAL_API int posix_memalign(void **memptr, size_t alignment, size_t size) {
    /* All iso_alloc allocations are 8 byte aligned */
    *memptr = iso_alloc(size);

    if(*memptr != NULL) {
        return 0;
    } else {
        return ENOMEM;
    }
}

EXTERNAL_API void *memalign(size_t alignment, size_t size) {
    /* All iso_alloc allocations are 8 byte aligned */
    return iso_alloc(size);
}
#endif
