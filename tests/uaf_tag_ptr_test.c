/* iso_alloc uaf_tag_ptr_test.c
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
    iso_free_from_zone(up, _zone_handle);
    iso_flush_caches();
    p = iso_alloc_untag_ptr(p, _zone_handle);

    /* This should crash on systems without TBI as p is
     * already free and the untagging operation should
     * result in a bad pointer */
    memset(p, 0x41, SIZE);

#if __aarch64__
    /* aarch64 systems with TBI enabled will succeed in
     * using the tagged pointer p. If p is still tagged
     * we abort here */
    if((uintptr_t) p & IS_TAGGED_PTR_MASK) {
        LOG_AND_ABORT("Write to tagged ptr %p succeeded. TBI may be enabled", p);
    }
#else
    if((uintptr_t) p & IS_TAGGED_PTR_MASK) {
        LOG_AND_ABORT("Write to tagged ptr %p succeeded on x86_64 ?!", p);
    }
#endif

    iso_alloc_destroy_zone(_zone_handle);
    return 0;
}
