/* iso_alloc verify_tag_ptr_test.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include <stdio.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMORY_TAGGING
#error "This test intended to be run with -DMEMORY_TAGGING=1"
#endif

#define SIZE 256

int main(int argc, char *argv[]) {
    iso_alloc_zone_handle *_zone_handle = iso_alloc_new_zone(SIZE);

    if(_zone_handle == NULL) {
        abort();
    }

    /* Allocate a chunk, and assign a tagged pointer to p */
    void *p = iso_alloc_from_zone_tagged(_zone_handle);

    /* Remove the tag from the pointer */
    void *up = iso_alloc_untag_ptr(p, _zone_handle);

    /* Free the underlying chunk with the untagged pointer */
    iso_free_from_zone(up, _zone_handle);

    /* Flush all caches includes the delayed free list. When
     * the chunk is free'd its tag will be changed */
    iso_flush_caches();

    /* Verify the tag on our stale tagged pointer. This should
     * abort because the tag was changed during free() */
    iso_alloc_verify_ptr_tag(p, _zone_handle);

    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
