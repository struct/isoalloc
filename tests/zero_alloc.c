/* iso_alloc zero_alloc.c
 * Copyright 2021 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    int ret = OK;
    void *p = iso_alloc(0);
    void *q = iso_alloc(0);
    if (p == q) {
        ret = ERR;
    }
    iso_free(p);
    iso_free(q);
    return err;
}
