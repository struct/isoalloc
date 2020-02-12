/* iso_alloc_interfaces.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

EXTERNAL_API void *iso_alloc(size_t size) {
    return _iso_alloc(size, NULL);
}

EXTERNAL_API void *iso_calloc(size_t nmemb, size_t size) {
    return _iso_calloc(nmemb, size);
}

EXTERNAL_API void iso_free(void *p) {
    _iso_free(p, false);
    return;
}

EXTERNAL_API void iso_free_permanently(void *p) {
    _iso_free(p, true);
    return;
}

EXTERNAL_API void *iso_realloc(void *p, size_t size) {
    if(p != NULL && size == 0) {
        _iso_free(p, false);
        return NULL;
    }

    void *r = _iso_alloc(size, NULL);

    if(p == NULL) {
        return r;
    }

    size_t chunk_size = _iso_chunk_size(p);

    if(size > chunk_size) {
        size = chunk_size;
    }

    memcpy(r, p, size);
    _iso_free(p, false);
    return r;
}

/* Returns the size of the chunk for an associated pointer */
EXTERNAL_API size_t iso_chunksz(void *p) {
    return _iso_chunk_size(p);
}

EXTERNAL_API iso_alloc_zone_handle *iso_realloc_from_zone(void *p, size_t size, iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return NULL;
    }

    if(p != NULL && size == 0) {
        iso_free(p);
        return NULL;
    }

    void *r = _iso_alloc(size, zone);

    if(p == NULL) {
        return r;
    }

    size_t chunk_size = _iso_chunk_size(p);

    if(size > chunk_size) {
        size = chunk_size;
    }

    memcpy(r, p, size);
    iso_free(p);
    return r;
}

EXTERNAL_API iso_alloc_zone_handle *iso_alloc_from_zone(size_t size, iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return NULL;
    }

    return _iso_alloc(size, zone);
}

EXTERNAL_API void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone) {
    _iso_alloc_destroy_zone(zone);
    return;
}

/* Allocate a zone for a custom object / size */
EXTERNAL_API iso_alloc_zone_handle *iso_alloc_new_zone(size_t size) {
    iso_alloc_zone_handle *zone = (iso_alloc_zone_handle *) iso_new_zone(size, false);
    return zone;
}

/* Disable all use of isoalloc by protecting the _root */
EXTERNAL_API void iso_alloc_protect_root() {
    _iso_alloc_protect_root();
}

/* Unprotect all use of isoalloc by allowing R/W of the _root */
EXTERNAL_API void iso_alloc_unprotect_root() {
    _iso_alloc_unprotect_root();
}

/* Checks for leaks in a specific zone.
 * Returns count of leaks found in that zone */
EXTERNAL_API int32_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone) {
    return _iso_alloc_zone_leak_detector(zone);
}

/* Check for leaks in all zones
 * Returns total count of leaks found */
EXTERNAL_API int32_t iso_alloc_detect_leaks() {
    return _iso_alloc_detect_leaks();
}

EXTERNAL_API int32_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone) {
    return _iso_alloc_zone_mem_usage(zone);
}

EXTERNAL_API int32_t iso_alloc_mem_usage() {
    return _iso_alloc_mem_usage();
}
