/* iso_alloc_interfaces.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

EXTERNAL_API void *iso_alloc(size_t size) {
    LOCK_ROOT_MUTEX();
    void *p = _iso_alloc(NULL, size);
    UNLOCK_ROOT_MUTEX();
    return p;
}

EXTERNAL_API void *iso_calloc(size_t nmemb, size_t size) {
    LOCK_ROOT_MUTEX();
    void *p = _iso_calloc(nmemb, size);
    UNLOCK_ROOT_MUTEX();
    return p;
}

EXTERNAL_API void iso_free(void *p) {
    LOCK_ROOT_MUTEX();
    _iso_free(p, false);
    UNLOCK_ROOT_MUTEX();
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

    _iso_free(p, false);
    return r;
}

/* Returns the size of the chunk for an associated pointer */
EXTERNAL_API size_t iso_chunksz(void *p) {
    LOCK_ROOT_MUTEX();
    size_t s = _iso_chunk_size(p);
    UNLOCK_ROOT_MUTEX();
    return s;
}

EXTERNAL_API iso_alloc_zone_handle *iso_realloc_from_zone(iso_alloc_zone_handle *zone, void *p, size_t size) {
    if(zone == NULL) {
        return NULL;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    if(p != NULL && size == 0) {
        iso_free(p);
        return NULL;
    }

    void *r = _iso_alloc(zone, size);

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

    if(zone != NULL) {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    LOCK_ROOT_MUTEX();
    char *p = (char *) _iso_alloc(zone, size);
    UNLOCK_ROOT_MUTEX();

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
    LOCK_ROOT_MUTEX();
    void *p = _iso_alloc(zone, size);
    UNLOCK_ROOT_MUTEX();
    return p;
}

EXTERNAL_API void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone) {
    zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    LOCK_ROOT_MUTEX();
    _iso_alloc_destroy_zone(zone);
    UNLOCK_ROOT_MUTEX();
    return;
}

EXTERNAL_API iso_alloc_zone_handle *iso_alloc_new_zone(size_t size) {
    LOCK_ROOT_MUTEX();
    iso_alloc_zone_handle *zone = (iso_alloc_zone_handle *) iso_new_zone(size, false);
    UNLOCK_ROOT_MUTEX();
    zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    return zone;
}

EXTERNAL_API void iso_alloc_protect_root() {
    LOCK_ROOT_MUTEX();
    _iso_alloc_protect_root();
}

EXTERNAL_API void iso_alloc_unprotect_root() {
    UNLOCK_ROOT_MUTEX();
    _iso_alloc_unprotect_root();
}

EXTERNAL_API uint64_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return 0;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    LOCK_ROOT_MUTEX();
    uint64_t r = _iso_alloc_zone_leak_detector(zone);
    UNLOCK_ROOT_MUTEX();
    return r;
}

EXTERNAL_API uint64_t iso_alloc_detect_leaks() {
    LOCK_ROOT_MUTEX();
    uint64_t r = _iso_alloc_detect_leaks();
    UNLOCK_ROOT_MUTEX();
    return r;
}

EXTERNAL_API uint64_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return 0;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    LOCK_ROOT_MUTEX();
    uint64_t r = _iso_alloc_zone_mem_usage(zone);
    UNLOCK_ROOT_MUTEX();
    return r;
}

EXTERNAL_API uint64_t iso_alloc_mem_usage() {
    LOCK_ROOT_MUTEX();
    uint64_t r = _iso_alloc_mem_usage();
    UNLOCK_ROOT_MUTEX();
    return r;
}

EXTERNAL_API void iso_verify_zones() {
    LOCK_ROOT_MUTEX();
    verify_all_zones();
    UNLOCK_ROOT_MUTEX();
    return;
}

EXTERNAL_API void iso_verify_zone(iso_alloc_zone_handle *zone) {
    if(zone == NULL) {
        return;
    } else {
        zone = (iso_alloc_zone_handle *) ((uintptr_t) zone ^ (uintptr_t) _root->zone_handle_mask);
    }

    LOCK_ROOT_MUTEX();
    verify_zone(zone);
    UNLOCK_ROOT_MUTEX();
    return;
}
