/* iso_alloc_printf.c - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"
#include <stdarg.h>

#define INTERNAL_HIDDEN __attribute__((visibility("hidden")))

/* This primitive printf implementation is only ever called
 * from the LOG and LOG_AND_ABORT macros. We need to be able
 * to print basic log messages without invoking malloc() or
 * we run the risk of using a corrupted heap.
 *
 * The argument width consumed by each conversion is fixed
 * and callers must match it:
 *
 *   %d        int32_t, printed signed
 *   %u        uint32_t
 *   %x %p     uint64_t, printed as hex. Never pass a narrower
 *             type, it is promoted to int and the upper bytes
 *             of the argument slot are garbage
 *   %ld %zd   int64_t, printed signed
 *   %lu %zu   uint64_t
 *   %s        const char *, NULL prints as (null)
 *   %%        a literal percent sign
 *
 * Output is bounded by the size of a stack buffer and is
 * never written past it. Anything that does not fit is
 * dropped */

#define PRINTF_BUF_SZ 4096

/* Enough for a 64 bit value in base 10 plus a NUL */
#define FMT_BUF_SZ 24

static const char asc_hex[] = "0123456789abcdef";

typedef struct {
    char *out;
    size_t len;
    size_t max;
} printf_buf_t;

static void emit_char(printf_buf_t *b, char c) {
    if(b->len < b->max) {
        b->out[b->len++] = c;
    }
}

static void emit_str(printf_buf_t *b, const char *s) {
    while(*s != '\0' && b->len < b->max) {
        b->out[b->len++] = *s++;
    }
}

/* Formats n into buf and returns a pointer to the first
 * character of the result, which is NUL terminated */
static const char *fmt_uint(uint64_t n, uint32_t base, char *buf) {
    char *ptr = &buf[FMT_BUF_SZ - 1];
    *ptr = '\0';

    if(n == 0) {
        *--ptr = '0';
        return ptr;
    }

    while(n != 0) {
        *--ptr = asc_hex[n % base];
        n /= base;
    }

    return ptr;
}

static void emit_uint(printf_buf_t *b, uint64_t n, uint32_t base) {
    char buf[FMT_BUF_SZ];
    emit_str(b, fmt_uint(n, base, buf));
}

static void emit_int(printf_buf_t *b, int64_t n) {
    uint64_t magnitude = (uint64_t) n;

    if(n < 0) {
        emit_char(b, '-');
        /* Negating in unsigned space is defined for INT64_MIN */
        magnitude = (~magnitude) + 1;
    }

    emit_uint(b, magnitude, 10);
}

INTERNAL_HIDDEN void _iso_alloc_printf(int32_t fd, const char *f, ...) {
    if(UNLIKELY(f == NULL)) {
        return;
    }

    char out[PRINTF_BUF_SZ];
    printf_buf_t b = {.out = out, .len = 0, .max = sizeof(out)};
    va_list arg;
    va_start(arg, f);

    for(const char *idx = f; *idx != '\0'; idx++) {
        if(*idx != '%') {
            emit_char(&b, *idx);
            continue;
        }

        idx++;

        /* A lone trailing percent sign is printed as is */
        if(*idx == '\0') {
            emit_char(&b, '%');
            break;
        }

        switch(*idx) {
        case 'd':
            emit_int(&b, va_arg(arg, int32_t));
            break;
        case 'u':
            emit_uint(&b, va_arg(arg, uint32_t), 10);
            break;
        case 'x':
        case 'p':
            emit_uint(&b, va_arg(arg, uint64_t), 16);
            break;
        case 'l':
        case 'z':
            /* An optional d or u may follow, both are 64 bit */
            if(*(idx + 1) == 'u') {
                idx++;
                emit_uint(&b, va_arg(arg, uint64_t), 10);
            } else {
                if(*(idx + 1) == 'd') {
                    idx++;
                }

                emit_int(&b, va_arg(arg, int64_t));
            }

            break;
        case 's': {
            const char *s = va_arg(arg, const char *);
            emit_str(&b, (s != NULL) ? s : "(null)");
            break;
        }
        case '%':
            emit_char(&b, '%');
            break;
        default:
            /* Unknown conversion, print it verbatim and
             * consume no argument */
            emit_char(&b, '%');
            emit_char(&b, *idx);
            break;
        }
    }

    va_end(arg);
    (void) !write(fd, out, b.len);
}
