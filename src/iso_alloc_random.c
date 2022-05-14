/* iso_alloc_random.c - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

/* Contributed by Oscar Reparaz (@oreparaz)
 * https://github.com/struct/isoalloc/pull/5 */

#if __linux__
#define GRND_NONBLOCK 0x0001
#include <sys/syscall.h>
#elif __APPLE__
#include <Security/SecRandom.h>
#elif __FreeBSD__
#include <sys/random.h>
#else
#error "unknown OS"
#endif
#include "iso_alloc_internal.h"

INTERNAL_HIDDEN uint64_t rand_uint64(void) {
    uint64_t val = 0;
    int ret = 0;

/* In modern versions of glibc (>=2.25) we can call getrandom(),
 * but older versions of glibc are still in use as of writing this.
 * Use the raw system call as a lower common denominator.
 * We give up on checking the return value. The alternative would be
 * to crash. We prefer here to keep going with degraded randomness. */
#if __linux__
    ret = syscall(SYS_getrandom, &val, sizeof(val), GRND_NONBLOCK) != sizeof(val);
#elif __APPLE__
    ret = SecRandomCopyBytes(kSecRandomDefault, sizeof(val), &val);
#elif __FreeBSD__
    ret = getrandom(&val, sizeof(val), GRND_NONBLOCK) != sizeof(val);
#endif

#if ABORT_NO_ENTROPY
    if(ret != 0) {
        LOG_AND_ABORT("Unable to gather enough entropy");
    }
#endif

    return val;
}
