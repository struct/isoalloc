/* iso_alloc memset_sanity.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !MEMSET_SANITY
#error "This test intended to be run with -DMEMSET_SANITY=1"
#endif

int main(int argc, char *argv[]) {
    iso_free(iso_alloc(-1234));
    iso_free(iso_alloc(-1));
    iso_free(iso_alloc(0));
    iso_free(iso_alloc(1));

    iso_free(iso_calloc(-1234, 1));
    iso_free(iso_calloc(-1, 1));
    iso_free(iso_calloc(0, 1));
    iso_free(iso_calloc(1, 1));

    iso_free(iso_calloc(1, -1234));
    iso_free(iso_calloc(1, -1));
    iso_free(iso_calloc(1, 0));
    iso_free(iso_calloc(1, 1));

    iso_free(iso_calloc(0, 0));
    iso_free(iso_calloc(0, 1));
    iso_free(iso_calloc(1, 0));

    iso_free(iso_strdup(""));
    iso_free(iso_strdup(NULL));

    iso_free(iso_realloc(NULL, -1234));
    iso_free(iso_realloc(NULL, -1));
    iso_free(iso_realloc(NULL, 0));
    iso_free(iso_realloc(NULL, 1));

    iso_free(iso_realloc(iso_alloc(1), 0));

    iso_verify_zones();

    return OK;
}
