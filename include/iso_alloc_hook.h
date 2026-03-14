/* iso_alloc_hook.h - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#pragma once

/* Use direct function aliasing on GCC >= 9 and Clang >= 10.
 * This makes malloc/free/calloc/realloc etc. linker-level aliases
 * for their iso_ counterparts rather than thin wrapper functions.
 *
 * Benefits over wrapper functions:
 *  - copy(fun) propagates all function attributes (malloc, alloc_size,
 *    nothrow, etc.) from the iso_ target to the exported public symbol
 *  - No wrapper call overhead; same code, two symbol names
 *
 * Falls back to inline wrapper bodies on older compilers.
 *
 * Note: alias() targets must live in the same final DSO. Both
 * malloc_hook.c and iso_alloc_interfaces.c are compiled into
 * libisoalloc.so, so the linker resolves the alias within the DSO. */

#if !defined(__APPLE__) && ((defined(__GNUC__) && __GNUC__ >= 9) || (defined(__clang__) && __clang_major__ >= 10))
#pragma GCC diagnostic ignored "-Wattributes"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wattribute-alias"
#endif
#define ISO_FORWARD(fun) __attribute__((alias(#fun), used, visibility("default"), copy(fun)));
#define ISO_FORWARD0(fun, x)       ISO_FORWARD(fun)
#define ISO_FORWARD1(fun, x)       ISO_FORWARD(fun)
#define ISO_FORWARD2(fun, x, y)    ISO_FORWARD(fun)
#define ISO_FORWARD3(fun, x, y, z) ISO_FORWARD(fun)
#else
#define ISO_FORWARD0(fun, x)       { fun(x); }
#define ISO_FORWARD1(fun, x)       { return fun(x); }
#define ISO_FORWARD2(fun, x, y)    { return fun(x, y); }
#define ISO_FORWARD3(fun, x, y, z) { return fun(x, y, z); }
#endif
