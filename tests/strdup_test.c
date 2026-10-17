/* iso_alloc strdup_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for iso_strdup/iso_strndup. These previously
 * allocated strlen(str) bytes with no room for a NUL terminator
 * and never wrote one, iso_strdup("") returned the unreadable
 * zero allocation page, iso_strndup(str, 0) wrote to p[-1], and
 * truncation dropped the last in-bounds character vs POSIX */
#include "iso_alloc.h"
#include "iso_alloc_internal.h"
#include <string.h>

int main(int argc, char *argv[]) {
    /* A 32 character string fills a 32 byte chunk exactly so the
     * NUL byte requires the allocation be sized strlen + 1 */
    const char *s32 = "0123456789abcdef0123456789abcdef";
    char *p = iso_strdup(s32);

    if(p == NULL || iso_chunksz(p) < 33) {
        return ERR;
    }

    if(p[32] != '\0' || strcmp(p, s32) != 0) {
        return ERR;
    }

    iso_free(p);

    /* Duplicating an empty string must return a readable 1 byte
     * string, not the zero allocation page */
    p = iso_strdup("");

    if(p == NULL || p[0] != '\0') {
        return ERR;
    }

    iso_free(p);

    /* POSIX strndup truncation keeps n characters plus a NUL */
    p = iso_strndup("hello", 3);

    if(p == NULL || strcmp(p, "hel") != 0) {
        return ERR;
    }

    iso_free(p);

    /* n == 0 must yield an empty string, previously this wrote
     * out of bounds at p[n - 1] */
    p = iso_strndup("hello", 0);

    if(p == NULL || p[0] != '\0') {
        return ERR;
    }

    iso_free(p);

    /* The source is not required to be NUL terminated within the
     * first n bytes so strndup must never scan past them */
    char unterminated[4] = {'a', 'b', 'c', 'd'};
    p = iso_strndup(unterminated, sizeof(unterminated));

    if(p == NULL || strcmp(p, "abcd") != 0) {
        return ERR;
    }

    iso_free(p);

    /* No truncation: the whole string plus NUL */
    p = iso_strndup("hi", 16);

    if(p == NULL || strcmp(p, "hi") != 0) {
        return ERR;
    }

    iso_free(p);

    return OK;
}
