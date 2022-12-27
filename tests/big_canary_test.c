/* iso_alloc big_canary_test.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {

    void *r = iso_alloc(ZONE_USER_SIZE + (ZONE_USER_SIZE / 4));

    if(r == NULL) {
        LOG_AND_ABORT("Failed to allocate a big zone of %d bytes", ZONE_USER_SIZE + (ZONE_USER_SIZE / 4));
    }

    iso_alloc_root *root = _get_root();
    void *p = ((iso_alloc_big_zone_t *) ((uintptr_t) root->big_zone_next_mask ^ (uintptr_t) root->big_zone_used));

    if(p == NULL) {
        LOG_AND_ABORT("Big zone list is empty, %p must not be a big zone!", r);
    }

    memset(p, 0x41, sizeof(iso_alloc_big_zone_t));

    iso_free_permanently(r);

    return 0;
}
