/* iso_alloc zero_alloc.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    void *p = iso_alloc(0);
    memcpy(p, "0x41", 1);
    iso_free(p);
    return 0;
}
