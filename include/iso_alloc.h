/* iso_alloc.h - A secure memory allocator
 * Copyright 2021 - chris.rohlf@gmail.com */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef EXTERNAL_API
#define EXTERNAL_API __attribute__((visibility("default")))
#endif

#define NO_DISCARD __attribute__((warn_unused_result))

typedef void iso_alloc_zone_handle;

#if CPP_SUPPORT
extern "C" {
#endif
EXTERNAL_API NO_DISCARD void *iso_alloc(size_t size);
EXTERNAL_API NO_DISCARD void *iso_calloc(size_t nmemb, size_t size);
EXTERNAL_API void iso_free(void *p);
EXTERNAL_API void iso_free_permanently(void *p);
EXTERNAL_API NO_DISCARD void *iso_realloc(void *p, size_t size);
EXTERNAL_API size_t iso_chunksz(void *p);
EXTERNAL_API NO_DISCARD char *iso_strdup(const char *str);
EXTERNAL_API NO_DISCARD char *iso_strdup_from_zone(iso_alloc_zone_handle *zone, const char *str);
EXTERNAL_API NO_DISCARD char *iso_strndup(const char *str, size_t n);
EXTERNAL_API NO_DISCARD char *iso_strndup_from_zone(iso_alloc_zone_handle *zone, const char *str, size_t n);
EXTERNAL_API NO_DISCARD iso_alloc_zone_handle *iso_alloc_from_zone(iso_alloc_zone_handle *zone, size_t size);
EXTERNAL_API NO_DISCARD iso_alloc_zone_handle *iso_alloc_new_zone(size_t size);
EXTERNAL_API void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone);
EXTERNAL_API void iso_alloc_protect_root();
EXTERNAL_API void iso_alloc_unprotect_root();
EXTERNAL_API uint64_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone);
EXTERNAL_API uint64_t iso_alloc_detect_leaks();
EXTERNAL_API uint64_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone);
EXTERNAL_API uint64_t iso_alloc_mem_usage();
EXTERNAL_API void iso_verify_zones();
EXTERNAL_API void iso_verify_zone(iso_alloc_zone_handle *zone);
EXTERNAL_API int32_t iso_alloc_name_zone(iso_alloc_zone_handle *zone, char *name);

#if EXPERIMENTAL
EXTERNAL_API void iso_alloc_search_stack(void *p);
#endif

#if CPP_SUPPORT
}
#endif
