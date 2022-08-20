/* compiler.h - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#pragma once

#define INTERNAL_HIDDEN __attribute__((visibility("hidden")))
#define ASSUME_ALIGNED __attribute__((assume_aligned(8)))
#define CONST __attribute__((const))

/* This isn't standard in C as [[nodiscard]] until C23 */
#define NO_DISCARD __attribute__((warn_unused_result))

#if UNIT_TESTING
#define EXTERNAL_API __attribute__((visibility("default")))
#endif

#if PERF_TEST_BUILD
#define INLINE
#define FLATTEN
#else
#define INLINE __attribute__((always_inline))
#define FLATTEN __attribute__((flatten))
#endif

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
