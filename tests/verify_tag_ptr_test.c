/* iso_alloc verify_tag_ptr_test.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include <stdio.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#define SIZE 256

int main(int argc, char *argv[]) {
    iso_alloc_zone_handle *_zone_handle = iso_alloc_new_zone(SIZE);

    if(_zone_handle == NULL) {
        abort();
    }

    void *p = iso_alloc_from_zone_tagged(_zone_handle);
    void *up = iso_alloc_untag_ptr(p, _zone_handle);
    iso_free_from_zone(up, _zone_handle);
    iso_flush_caches();
    /* The free() path will have changed the tag for this pointer */
    iso_alloc_verify_ptr_tag(p, _zone_handle);
    iso_alloc_destroy_zone(_zone_handle);

#if !MEMORY_TAGGING
    return -1;
#endif

    return 0;
}
