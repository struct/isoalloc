/* iso_alloc calloc_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* iso_calloc only clears memory that has been used before. A
 * chunk the bitmap marks as never handed out, and a big zone
 * that was just mapped, are left to the zeroes the kernel gave
 * them. This test dirties chunks every way the allocator can
 * reuse them and checks that calloc always returns zeroes */
#include <stdint.h>
#include <string.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if !UNIT_TESTING
#error "This test needs the library built with -DUNIT_TESTING=1"
#endif

#define COUNT 600

static void check_zero(const uint8_t *p, size_t size, const char *what) {
    for(size_t i = 0; i < size; i++) {
        if(p[i] != 0) {
            LOG_AND_ABORT("%s: byte %d of %p is 0x%x", what, i, p, (uint64_t) p[i]);
        }
    }
}

static void dirty_and_free(size_t size, void **held) {
    for(size_t i = 0; i < COUNT; i++) {
        held[i] = iso_alloc(size);

        if(held[i] == NULL) {
            LOG_AND_ABORT("iso_alloc(%d) failed", size);
        }

        memset(held[i], 0xff, size);
    }

    for(size_t i = 0; i < COUNT; i++) {
        iso_free(held[i]);
    }

    /* Make the dirty chunks available again */
    iso_flush_caches();
}

int main(int argc, char *argv[]) {
    void *held[COUNT];
    static const size_t sizes[] = {16, 48, 256, 1000, 4096, 8192, 65536};

    for(size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        const size_t size = sizes[s];

        /* Never used chunks of a class */
        for(size_t i = 0; i < COUNT; i++) {
            held[i] = iso_calloc(1, size);

            if(held[i] == NULL) {
                LOG_AND_ABORT("iso_calloc(%d) failed", size);
            }

            check_zero(held[i], size, "fresh chunk");
            memset(held[i], 0xaa, size);
        }

        for(size_t i = 0; i < COUNT; i++) {
            iso_free(held[i]);
        }

        iso_flush_caches();

        /* Chunks that were used and freed, which carry a canary
         * and whatever the previous user left */
        dirty_and_free(size, held);

        for(size_t i = 0; i < COUNT; i++) {
            held[i] = iso_calloc(1, size);

            if(held[i] == NULL) {
                LOG_AND_ABORT("iso_calloc(%d) failed", size);
            }

            check_zero(held[i], size, "reused chunk");
        }

        for(size_t i = 0; i < COUNT; i++) {
            iso_free(held[i]);
        }

        iso_flush_caches();
    }

    /* A zone that was reset in place is zero again by way of the
     * kernel, and its bitmap says every chunk is fresh */
    iso_alloc_zone_handle *handle = iso_alloc_new_zone(512);

    for(size_t i = 0; i < COUNT; i++) {
        held[i] = iso_alloc_from_zone(handle);
        memset(held[i], 0xff, 512);
    }

    for(size_t i = 0; i < COUNT; i++) {
        iso_free_from_zone(held[i], handle);
    }

    _iso_alloc_purge_now();
    _iso_alloc_purge_now();

    for(size_t i = 0; i < COUNT; i++) {
        held[i] = iso_alloc_from_zone(handle);
        check_zero(held[i], 512, "chunk from a reset zone");
    }

    for(size_t i = 0; i < COUNT; i++) {
        iso_free_from_zone(held[i], handle);
    }

    iso_alloc_destroy_zone(handle);

    /* Big zones, new and reused from the free list */
    const size_t big = (SMALL_SIZE_MAX * 2);
    uint8_t *b = iso_calloc(1, big);
    check_zero(b, big, "new big zone");
    memset(b, 0xff, big);
    iso_free(b);
    iso_flush_caches();

    b = iso_calloc(1, big);
    check_zero(b, big, "reused big zone");
    iso_free(b);

    /* The multiply overflow check still stands */
    if(iso_calloc(SIZE_MAX / 2, 4) != NULL) {
        LOG_AND_ABORT("iso_calloc accepted an overflowing size");
    }

    iso_flush_caches();
    iso_verify_zones();
    return 0;
}
