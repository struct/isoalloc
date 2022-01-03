/* iso_alloc sized_free.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    size_t size = 1024;
    uint8_t *p = iso_alloc(size);
    iso_free_size(p, size * 2);
    return 0;
}
