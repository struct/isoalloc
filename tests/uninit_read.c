/* iso_alloc uninit_read.c
 * Copyright 2021 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    while(1) {
        uint8_t *p = iso_alloc(1024);
        uint8_t drf = p[128];
        p[256] = drf;
        iso_free(p);
    }

    return OK;
}
