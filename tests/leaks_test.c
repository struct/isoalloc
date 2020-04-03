/* iso_alloc leaks.c
 * Copyright 2020 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    void *p = NULL;

    for(int32_t i = 0; i < 16; i++) {
        p = iso_alloc(i * i);

        /* Free a single chunk */
        if(i == 1) {
            iso_free(p);
        }
    }

    iso_verify_zones();
    int32_t r = iso_alloc_detect_leaks();

    LOG("Total leaks detected: %d", r);

    return r;
}
