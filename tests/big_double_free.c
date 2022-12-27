/* iso_alloc big_double_free.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    void *p = iso_alloc(SMALL_SIZE_MAX + 1);
    iso_free(p);
    void *z = iso_alloc(SMALL_SIZE_MAX + 1);
    iso_free(p);
    iso_free(z);
    return OK;
}
