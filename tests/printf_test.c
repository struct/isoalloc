/* iso_alloc printf_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Tests the internal printf used by LOG and LOG_AND_ABORT.
 * It previously read past the end of a format string, dropped
 * the character after a newline, copied literals and strings
 * without a bound, and could never print a negative number
 * because it stored the value in an unsigned type. Every
 * test binary compiles iso_alloc_printf.c directly so this
 * calls the implementation through a pipe and compares */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

static int fds[2];
static char result[8192];

/* The read end is non-blocking so a format that produces
 * no output at all reads back as an empty string instead
 * of blocking forever */
static const char *capture(void) {
    memset(result, 0x0, sizeof(result));
    ssize_t n = read(fds[0], result, sizeof(result) - 1);

    if(n < 0 && errno != EAGAIN) {
        LOG_AND_ABORT("read from pipe failed");
    }

    return result;
}

#define CHECK(expected, ...)                                                                       \
    do {                                                                                           \
        _iso_alloc_printf(fds[1], __VA_ARGS__);                                                    \
        const char *got = capture();                                                               \
        if(strcmp(got, expected) != 0) {                                                           \
            fprintf(stderr, "format \"%s\"\n  expected \"%s\"\n  got      \"%s\"\n", #__VA_ARGS__, \
                    expected, got);                                                                \
            abort();                                                                               \
        }                                                                                          \
    } while(0)

int main(int argc, char *argv[]) {
    if(pipe(fds) != 0 || fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0) {
        LOG_AND_ABORT("pipe failed");
    }

    /* Plain text, including text after a newline which was
     * previously dropped */
    CHECK("hello\n", "hello\n");
    CHECK("a\nb\n", "a\nb\n");

    /* Signed and unsigned 32 bit */
    CHECK("0 1 -1 2147483647 -2147483648", "%d %d %d %d %d", 0, 1, -1, INT32_MAX, INT32_MIN);
    CHECK("4294967295", "%u", UINT32_MAX);

    /* 64 bit conversions, signed and unsigned */
    CHECK("-9223372036854775808 9223372036854775807", "%ld %ld", INT64_MIN, INT64_MAX);
    CHECK("18446744073709551615", "%lu", UINT64_MAX);
    CHECK("-5 6", "%zd %zu", (int64_t) -5, (size_t) 6);
    CHECK("7", "%l", (int64_t) 7);

    /* Hex always reads 64 bits */
    CHECK("0 ff deadbeefcafebabe", "%x %x %x", (uint64_t) 0, (uint64_t) 0xff, (uint64_t) 0xdeadbeefcafebabe);
    CHECK("1000", "%p", (void *) 0x1000);

    /* Strings, including NULL */
    CHECK("abc (null) ", "%s %s ", "abc", (const char *) NULL);
    CHECK("", "%s", "");

    /* Literal percent, trailing percent, unknown conversion */
    CHECK("100%", "100%%");
    CHECK("50%", "50%");
    CHECK("%q 1", "%q %d", 1);

    /* The same shape as the LOG macro */
    CHECK("[LOG][12](f.c:34 fn()) chunk 0x40 in zone[2]\n", "[LOG][%d](%s:%d %s()) chunk 0x%x in zone[%d]\n", 12, "f.c", 34, "fn", (uint64_t) 0x40, 2);

    /* Output is bounded. A string longer than the buffer is
     * truncated instead of overflowing the stack */
    char big[16384];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    _iso_alloc_printf(fds[1], "%s", big);
    const char *got = capture();
    size_t len = strlen(got);

    if(len == 0 || len >= sizeof(big) - 1) {
        LOG_AND_ABORT("Overlong string was not bounded, got %d bytes", (int32_t) len);
    }

    for(size_t i = 0; i < len; i++) {
        if(got[i] != 'A') {
            LOG_AND_ABORT("Truncated output was corrupted at %d", (int32_t) i);
        }
    }

    /* A long literal is bounded too */
    _iso_alloc_printf(fds[1], "%s%s%s%s", big, big, big, big);
    got = capture();

    if(strlen(got) >= sizeof(big)) {
        LOG_AND_ABORT("Repeated strings were not bounded");
    }

    close(fds[0]);
    close(fds[1]);

    return 0;
}
