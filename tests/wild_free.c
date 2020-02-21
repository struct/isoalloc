/* iso_alloc wild_free.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    int64_t *p = (int64_t *) 0x7fffffffffff;
    iso_free(p);
    return OK;
}
