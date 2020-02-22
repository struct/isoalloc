/* iso_alloc interfaces_test.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#include <assert.h>

int main(int argc, char *argv[]) {
    /* Test iso_calloc() */
    void *p = iso_calloc(10, 2);

    if(p == NULL) {
        LOG_AND_ABORT("iso_calloc failed")
    }

    iso_free(p);

    /* Test iso_alloc() */
    p = iso_alloc(128);

    if(p == NULL) {
        LOG_AND_ABORT("iso_alloc failed")
    }

    memset(p, 0x41, 128);

    uint8_t *pv = p;

    if(pv[10] != 0x41 || pv[100] != 0x41) {
        LOG_AND_ABORT("Chunk allocated at %p does not contain expected data! %x %x", p, pv[10], pv[100]);
    }

    /* Test iso_realloc */
    p = iso_realloc(p, 1024);

    if(p == NULL) {
        LOG_AND_ABORT("iso_realloc failed")
    }

    iso_free(p);

    p = iso_alloc(1024);

    size_t size = iso_chunksz(p);
    assert(size == 1024);

    iso_free_permanently(p);

    iso_alloc_zone_handle *zone = iso_alloc_new_zone(256);

    if(zone == NULL) {
        LOG_AND_ABORT("Could not create a zone");
    }

    p = iso_alloc_from_zone(zone, 256);

    if(p == NULL) {
        LOG_AND_ABORT("Could not allocate from custom zone");
    }

    p = iso_realloc_from_zone(zone, p, 64);

    if(p == NULL) {
        LOG_AND_ABORT("Could not allocate from custom zone");
    }

    iso_free(p);

    p = iso_alloc(1024);

    if(p == NULL) {
        LOG_AND_ABORT("iso_alloc failed");
    }

    memset(p, 0x41, 1024);

    void *r = iso_strdup(p);

    if(r == NULL) {
        LOG_AND_ABORT("iso_strdup failed");
    }

    iso_free(p);
    iso_free(r);

    iso_verify_zones();

    return 0;
}
