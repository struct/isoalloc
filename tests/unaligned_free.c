/* iso_alloc unaligned_free.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    void *p = iso_alloc(128);
    p += 1;
    iso_free(p);
    return OK;
}
