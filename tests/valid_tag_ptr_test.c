/* iso_alloc valid_tag_ptr_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for iso_alloc_verify_ptr_tag. The tag
 * comparison previously compared the 8 bit tag against the
 * unshifted top byte of the pointer so every valid tagged
 * pointer failed verification. The other tagging tests only
 * check that verification aborts on a stale pointer, this
 * one checks that it succeeds on a valid pointer. It also
 * checks that freeing a chunk always changes its tag so a
 * stale tagged pointer can never untag to the same address */
#include <stdio.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMORY_TAGGING
#error "This test intended to be run with -DMEMORY_TAGGING=1"
#endif

#define SIZE 256
#define HELD 64
#define CYCLES 4096

int main(int argc, char *argv[]) {
    iso_alloc_zone_handle *_zone_handle = iso_alloc_new_zone(SIZE);

    if(_zone_handle == NULL) {
        abort();
    }

    void *held[HELD];

    /* Every live tagged pointer must verify */
    for(size_t i = 0; i < HELD; i++) {
        held[i] = iso_alloc_from_zone_tagged(_zone_handle);

        if(held[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate tagged chunk %d", i);
        }

        iso_alloc_verify_ptr_tag(held[i], _zone_handle);

        void *up = iso_alloc_untag_ptr(held[i], _zone_handle);
        memset(up, 0x41, SIZE);
    }

    for(size_t i = 0; i < HELD; i++) {
        iso_alloc_verify_ptr_tag(held[i], _zone_handle);
        iso_free_from_zone(held[i], _zone_handle);
    }

    /* A tag must change on every free. If it did not then
     * untagging the stale pointer yields the original chunk
     * address and a use after free would go undetected */
    for(size_t i = 0; i < CYCLES; i++) {
        void *p = iso_alloc_from_zone_tagged(_zone_handle);

        if(p == NULL) {
            LOG_AND_ABORT("Failed to allocate tagged chunk in cycle %d", i);
        }

        iso_alloc_verify_ptr_tag(p, _zone_handle);

        void *up = iso_alloc_untag_ptr(p, _zone_handle);
        const uint8_t old_tag = iso_alloc_get_mem_tag(up, _zone_handle);

        iso_free_from_zone(up, _zone_handle);

        const uint8_t new_tag = iso_alloc_get_mem_tag(up, _zone_handle);

        if(old_tag == new_tag) {
            LOG_AND_ABORT("Tag %x for chunk %p did not change on free in cycle %d", old_tag, up, i);
        }

        if(iso_alloc_untag_ptr(p, _zone_handle) == up) {
            LOG_AND_ABORT("Stale pointer %p untagged to its original address in cycle %d", p, i);
        }
    }

    iso_flush_caches();
    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
