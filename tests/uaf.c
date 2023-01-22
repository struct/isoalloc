/* iso_alloc uaf.c
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if UAF_PTR_PAGE && !ALLOC_SANITY
/* This test should be run manually after enabling UAF_PTR_PAGE
 * and disabling the sampling mechanism before the call to
 * _iso_alloc_ptr_search in _iso_free_internal_unlocked */
typedef struct test {
    char *str;
} test_t;

int main(int argc, char *argv[]) {
    void *str = iso_alloc(32);
    test_t *test = (test_t *) iso_alloc(1024);
    test->str = str;

    const char *s = "a string!";
    memcpy(str, s, strlen(s));

    /* We free the chunk permanently because
     * it bypasses the quarantine */
    iso_free_permanently(str);

    /* Dereference a pointer that should have been
     * detected and overwritten with UAF_PTR_PAGE */
    iso_alloc_root *root = _get_root();
    fprintf(stdout, "Dereferencing test->str @ %p. Fault address will be %p\n", test->str, root->uaf_ptr_page);
    fprintf(stdout, "[%s]\n", test->str);
    iso_free_permanently(test);

    return OK;
}
#else
int main(int argc, char *argv[]) {
    return 0;
}
#endif
