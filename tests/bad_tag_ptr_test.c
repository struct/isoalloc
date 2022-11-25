/* iso_alloc bad_tag_ptr_test.c
 * Copyright 2022 - chris.rohlf@gmail.com */

/* This test should successfully fail with or
 * without MEMORY_TAGGING support */

#include <stdio.h>
#include <string.h>
#include "iso_alloc.h"

#define SIZE 256

int main(int argc, char *argv[]) {
    iso_alloc_zone_handle *_zone_handle = iso_alloc_new_zone(SIZE);

    if(_zone_handle == NULL) {
        abort();
    }

    char buffer[1024];
    void *p = &buffer;

    /* This should crash because p is not allocated from this zone */
    uint8_t tag = iso_alloc_get_mem_tag(p, _zone_handle);
    printf("Tag = %x\n", tag);
    iso_alloc_destroy_zone(_zone_handle);

#if !MEMORY_TAGGING
    return -1;
#endif

    return 0;
}
