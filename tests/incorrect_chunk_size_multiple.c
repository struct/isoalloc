/* iso_alloc incorrect_chunk_size_multiple.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    int64_t *p = (int64_t *) iso_alloc(128);
    p += 8;
    iso_free(p);
    return OK;
}
