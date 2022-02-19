/* iso_alloc double_free.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    void *p = iso_alloc(1024);
    iso_free(p);
    iso_free(p);
    return OK;
}
