/* iso_alloc uaf.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if defined(UAF_PTR_PAGE) && !defined(ALLOC_SANITY)
/* This test should be run manually. You need to enable UAF_PTR_PAGE
 * and then disable the sampling logic in iso_alloc. */
typedef struct test {
    char *str;
} test_t;

int main(int argc, char *argv[]) {
    void *str = iso_alloc(32);
    test_t *test = (test_t *) iso_alloc(1024);
    test->str = str;
    memcpy(str, "a string!", 9);
    iso_free(str);

    /* Dereference a pointer that should have been
     * detected and overwritten with UAF_PTR_PAGE */
    LOG("Attempting to dereference test->str.\nWe should fault on %x", UAF_PTR_PAGE_ADDR);
    LOG("%s", test->str);
    iso_free(test);

    return OK;
}
#else
int main(int argc, char *argv[]) {
    return 0;
}
#endif
