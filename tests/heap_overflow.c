/* iso_alloc heap_overflow.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    int64_t *p = (int64_t *) iso_alloc(32);
    memset(p, 0x42, 32768);
    iso_verify_zones();
    iso_free(p);
    return OK;
}
