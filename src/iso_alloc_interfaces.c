/* iso_alloc_interfaces.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

EXTERNAL_API void *iso_alloc(size_t size) {
    void *p = _iso_alloc(NULL, size);
    return p;
}

EXTERNAL_API void *iso_calloc(size_t nmemb, size_t size) {
    void *p = _iso_calloc(nmemb, size);
    return p;
}

EXTERNAL_API void iso_free(void *p) {
    _iso_free(p, false);
    return;
}

EXTERNAL_API void iso_free_permanently(void *p) {
    _iso_free(p, true);
    return;
}

EXTERNAL_API size_t iso_chunksz(void *p) {
    size_t s = _iso_chunk_size(p);
    return s;
}

EXTERNAL_API void *iso_realloc(void *p, size_t size) {
    if(UNLIKELY(size == 0)) {
        iso_free(p);
        return NULL;
    }

    void *r = iso_alloc(size);

    if(r == NULL) {
        return r;
    }

    size_t chunk_size = iso_chunksz(p);

    if(size > chunk_size) {
        size = chunk_size;
    }

    if(p != NULL) {
        memcpy(r, p, size);
    }

#if PERM_FREE_REALLOC
    iso_free_permanently(p);
#else
    iso_free(p);
#endif

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

    if(zone != NULL) {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

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

    if(zone != NULL) {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

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

    zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    void *p = _iso_alloc(zone, size);
    return p;
}

EXTERNAL_API void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return;
    }

    zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    _iso_alloc_destroy_zone(zone);
    return;
}

EXTERNAL_API iso_alloc_zone_handle *iso_alloc_new_zone(size_t size) {
    iso_alloc_zone_handle *zone = (iso_alloc_zone_handle *) iso_new_zone(size, false);
    zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    return zone;
}

EXTERNAL_API void iso_alloc_protect_root() {
    _iso_alloc_protect_root();
}

EXTERNAL_API void iso_alloc_unprotect_root() {
    _iso_alloc_unprotect_root();
}

EXTERNAL_API uint64_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return 0;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    uint64_t r = _iso_alloc_detect_leaks_in_zone(zone);
    return r;
}

EXTERNAL_API uint64_t iso_alloc_detect_leaks() {
    uint64_t r = _iso_alloc_detect_leaks();
    return r;
}

EXTERNAL_API uint64_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return 0;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    uint64_t r = _iso_alloc_zone_mem_usage(zone);
    return r;
}

EXTERNAL_API uint64_t iso_alloc_mem_usage() {
    uint64_t r = _iso_alloc_mem_usage();
    return r;
}

EXTERNAL_API void iso_verify_zones() {
    verify_all_zones();
    return;
}

EXTERNAL_API void iso_verify_zone(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    verify_zone(zone);
    return;
}

#if EXPERIMENTAL
EXTERNAL_API void iso_alloc_search_stack(void *p) {
    _iso_alloc_search_stack(p);
}
#endif
