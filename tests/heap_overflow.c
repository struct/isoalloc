/* iso_alloc heap_overflow.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    uint8_t *p = (uint8_t *) iso_alloc(32);
    memset(p, 0x42, 32768);
    iso_verify_zones();
    iso_free(p);
    return OK;
}
