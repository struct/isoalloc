/* iso_alloc_interfaces.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

EXTERNAL_API void *iso_alloc(size_t size) {
    return _iso_alloc(NULL, size);
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

    void *r = _iso_alloc(NULL, size);

    if(r == NULL) {
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

EXTERNAL_API iso_alloc_zone_handle *iso_realloc_from_zone(iso_alloc_zone_handle *zone, void *p, size_t size) {
    if(zone == NULL) {
        return NULL;
    }

    if(p != NULL && size == 0) {
        iso_free(p);
        return NULL;
    }

    void *r = _iso_alloc(zone, size);

    if(r == NULL) {
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

EXTERNAL_API char *iso_strdup(const char *str) {
    return iso_strdup_from_zone(NULL, str);
}

EXTERNAL_API char *iso_strdup_from_zone(iso_alloc_zone_handle *zone, const char *str) {
    if(str == NULL) {
        return NULL;
    }

    size_t size = strlen(str);

    char *p = (char *) _iso_alloc(zone, size);

    if(p == NULL) {
        return NULL;
    }

    memcpy(p, str, size);
    return p;
}

EXTERNAL_API char *iso_strndup(const char *str, size_t n) {
    return iso_strndup_from_zone(NULL, str, n);
}

EXTERNAL_API char *iso_strndup_from_zone(iso_alloc_zone_handle *zone, const char *str, size_t n) {
    if(str == NULL) {
        return NULL;
    }

    size_t s_size = strlen(str);

    char *p = (char *) _iso_alloc(zone, n);

    if(p == NULL) {
        return NULL;
    }

    if(s_size > n) {
        memcpy(p, str, n);
        p[n - 1] = '\0';
    } else {
        memcpy(p, str, s_size);
    }

    return p;
}

EXTERNAL_API iso_alloc_zone_handle *iso_alloc_from_zone(iso_alloc_zone_handle *zone, size_t size) {
    if(zone == NULL) {
        return NULL;
    }

    return _iso_alloc(zone, size);
}

EXTERNAL_API void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone) {
    _iso_alloc_destroy_zone(zone);
    return;
}

EXTERNAL_API iso_alloc_zone_handle *iso_alloc_new_zone(size_t size) {
    iso_alloc_zone_handle *zone = (iso_alloc_zone_handle *) iso_new_zone(size, false);
    return zone;
}

EXTERNAL_API void iso_alloc_protect_root() {
    _iso_alloc_protect_root();
}

EXTERNAL_API void iso_alloc_unprotect_root() {
    _iso_alloc_unprotect_root();
}

EXTERNAL_API int32_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone) {
    return _iso_alloc_zone_leak_detector(zone);
}

EXTERNAL_API int32_t iso_alloc_detect_leaks() {
    return _iso_alloc_detect_leaks();
}

EXTERNAL_API int32_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone) {
    return _iso_alloc_zone_mem_usage(zone);
}

EXTERNAL_API int32_t iso_alloc_mem_usage() {
    return _iso_alloc_mem_usage();
}

EXTERNAL_API void iso_verify_zones() {
    verify_all_zones();
    return;
}

EXTERNAL_API void iso_verify_zone(iso_alloc_zone_handle *zone) {
    verify_zone(zone);
    return;
}
