/* iso_alloc tagged_ptr_test.c
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

    void *p = iso_alloc_from_zone_tagged(_zone_handle);
    void *up = iso_alloc_untag_ptr(p, _zone_handle);

    uint8_t tag = ((uintptr_t) p >> 56);
    uint8_t itag = iso_alloc_get_mem_tag(up, _zone_handle);

    if(tag != itag) {
        LOG_AND_ABORT("Tags %d and %d do not match", tag, itag);
    }

    /* We can pass a tagged or untagged pointer to iso_free_from_zone */
    iso_free_from_zone(p, _zone_handle);

    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
