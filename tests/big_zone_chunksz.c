/* iso_alloc big_zone_chunksz.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for iso_find_big_zone(p, remove=false). A size
 * lookup via iso_chunksz() must not modify the big zone used list.
 * Previously a single lookup unlinked the matched zone (and severed
 * the list tail on a mid-list match) which made the subsequent
 * iso_free() of those chunks abort */
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    const size_t big_sz = SMALL_SIZE_MAX * 2;

    /* Used list after these allocations: c -> b -> a */
    void *a = iso_alloc(big_sz);
    void *b = iso_alloc(big_sz);
    void *c = iso_alloc(big_sz);

    if(a == NULL || b == NULL || c == NULL) {
        return ERR;
    }

    /* Lookup the head of the used list */
    if(iso_chunksz(c) < big_sz) {
        return ERR;
    }

    /* Lookup a mid-list entry */
    if(iso_chunksz(b) < big_sz) {
        return ERR;
    }

    /* Repeated lookups should still find every entry */
    if(iso_chunksz(a) < big_sz || iso_chunksz(b) < big_sz || iso_chunksz(c) < big_sz) {
        return ERR;
    }

    /* With the bug these frees abort with "Could not find any zone" */
    iso_free(a);
    iso_free(b);
    iso_free(c);

    return OK;
}
