/* iso_alloc leaks.c
 * Copyright 2021 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    void *p[16];
    int32_t leak = 0;

    for(int32_t i = 0; i < 16; i++) {
        p[i] = iso_alloc(i * i);

        /* Free a single chunk */
        if(i == 1) {
            iso_free(p[i]);
        } else {
            leak++;
        }
    }

    for(int32_t i = 0; i < 16; i++) {
        LOG("p[%d] (%p) = %p", i, &p[i], p[i]);
    }

    iso_verify_zones();
    int32_t r = iso_alloc_detect_leaks();

    LOG("Total leaks detected: %d %p of %d", r, p, leak);

    return r;
}
