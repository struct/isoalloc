/* iso_alloc aligned_alloc_test.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for the aligned allocation interfaces.
 * posix_memalign, memalign and aligned_alloc previously
 * ignored the requested alignment and returned chunks with
 * only SZ_ALIGNMENT bytes of alignment. They also accepted
 * invalid alignments instead of returning EINVAL. This test
 * also covers NULL handling in iso_alloc_new_zone and
 * iso_zone_chunk_count */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if __linux__
#include <malloc.h>
#else
/* macOS does not declare memalign but IsoAlloc exports it */
void *memalign(size_t alignment, size_t size);
#endif

static const size_t alignments[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
                                    8192, 16384, 32768, 65536, (1 << 20)};

static const size_t sizes[] = {1, 24, 100, 1000, 4097, 70000, 300000};

static void check_chunk(void *p, size_t alignment, size_t size, const char *api) {
    if(p == NULL) {
        LOG_AND_ABORT("%s(%d, %d) returned NULL", api, alignment, size);
    }

    if(((uintptr_t) p & (alignment - 1)) != 0) {
        LOG_AND_ABORT("%s(%d, %d) returned %p which is not aligned", api, alignment, size, p);
    }

    size_t cs = iso_chunksz(p);

    if(cs < size) {
        LOG_AND_ABORT("%s(%d, %d) returned a chunk of only %d bytes", api, alignment, size, cs);
    }

    /* The entire requested size must be writable */
    memset(p, 0x41, size);
}

int main(int argc, char *argv[]) {
    const size_t page_size = (size_t) sysconf(_SC_PAGESIZE);
    const size_t na = sizeof(alignments) / sizeof(alignments[0]);
    const size_t ns = sizeof(sizes) / sizeof(sizes[0]);
    void *ptrs[sizeof(alignments) / sizeof(alignments[0]) * sizeof(sizes) / sizeof(sizes[0]) * 3];
    size_t count = 0;

    /* Every combination through each interface. Chunks are
     * held until the end so later allocations can't reuse
     * an earlier aligned chunk by accident */
    for(size_t i = 0; i < na; i++) {
        for(size_t j = 0; j < ns; j++) {
            void *p = NULL;
            int r = posix_memalign(&p, alignments[i], sizes[j]);

            if(r != 0) {
                LOG_AND_ABORT("posix_memalign(%d, %d) returned %d", alignments[i], sizes[j], r);
            }

            check_chunk(p, alignments[i], sizes[j], "posix_memalign");
            ptrs[count++] = p;

            p = memalign(alignments[i], sizes[j]);
            check_chunk(p, alignments[i], sizes[j], "memalign");
            ptrs[count++] = p;

            /* C11 requires size to be a multiple of alignment */
            size_t s = (sizes[j] + (alignments[i] - 1)) & ~(alignments[i] - 1);
            p = aligned_alloc(alignments[i], s);
            check_chunk(p, alignments[i], s, "aligned_alloc");
            ptrs[count++] = p;
        }
    }

    for(size_t i = 0; i < count; i++) {
        free(ptrs[i]);
    }

    /* A big zone with an alignment larger than a page is
     * trimmed from a larger mapping. Reusing it from the
     * free list must preserve that alignment */
    const size_t big_alignment = (page_size << 2);
    void *first = NULL;
    int r = posix_memalign(&first, big_alignment, SMALL_SIZE_MAX * 2);

    if(r != 0 || first == NULL) {
        LOG_AND_ABORT("posix_memalign(%d, %d) failed", big_alignment, SMALL_SIZE_MAX * 2);
    }

    check_chunk(first, big_alignment, SMALL_SIZE_MAX * 2, "posix_memalign");
    free(first);

    for(size_t i = 0; i < 8; i++) {
        void *p = NULL;
        r = posix_memalign(&p, big_alignment, SMALL_SIZE_MAX * 2);

        if(r != 0) {
            LOG_AND_ABORT("posix_memalign(%d, %d) returned %d on reuse", big_alignment, SMALL_SIZE_MAX * 2, r);
        }

        check_chunk(p, big_alignment, SMALL_SIZE_MAX * 2, "posix_memalign");
        free(p);
    }

    /* Invalid alignments must fail with EINVAL and never
     * hand back memory with the wrong alignment. The values
     * are volatile so the compiler can't reject them itself */
    static volatile size_t zero_alignment = 0;
    static volatile size_t odd_alignment = 3;
    static volatile size_t non_pow2_alignment = 24;
    static volatile size_t small_alignment = 4;
    static volatile size_t bad_c11_alignment = 48;
    void *bad = (void *) 0x1;

    if(posix_memalign(&bad, zero_alignment, 64) != EINVAL || bad != NULL) {
        LOG_AND_ABORT("posix_memalign accepted an alignment of 0");
    }

    bad = (void *) 0x1;

    if(posix_memalign(&bad, odd_alignment, 64) != EINVAL || bad != NULL) {
        LOG_AND_ABORT("posix_memalign accepted an alignment of 3");
    }

    bad = (void *) 0x1;

    if(posix_memalign(&bad, non_pow2_alignment, 64) != EINVAL || bad != NULL) {
        LOG_AND_ABORT("posix_memalign accepted an alignment of 24");
    }

    bad = (void *) 0x1;

    if(posix_memalign(&bad, small_alignment, 64) != EINVAL || bad != NULL) {
        LOG_AND_ABORT("posix_memalign accepted an alignment smaller than sizeof(void *)");
    }

    /* The result pointer is volatile so the compiler can't
     * see that it is NULL and reject the call itself */
    void **volatile null_result = NULL;

    if(posix_memalign(null_result, 64, 64) != EINVAL) {
        LOG_AND_ABORT("posix_memalign accepted a NULL result pointer");
    }

    errno = 0;

    if(memalign(zero_alignment, 64) != NULL || errno != EINVAL) {
        LOG_AND_ABORT("memalign accepted an alignment of 0");
    }

    errno = 0;

    if(memalign(non_pow2_alignment, 64) != NULL || errno != EINVAL) {
        LOG_AND_ABORT("memalign accepted an alignment of 24");
    }

    errno = 0;

    if(aligned_alloc(bad_c11_alignment, 96) != NULL || errno != EINVAL) {
        LOG_AND_ABORT("aligned_alloc accepted an alignment of 48");
    }

    /* Overflow in the size rounding must fail cleanly */
    bad = (void *) 0x1;
    r = posix_memalign(&bad, 4096, SIZE_MAX - 100);

    if(r != ENOMEM || bad != NULL) {
        LOG_AND_ABORT("posix_memalign did not fail on size overflow (%d, %p)", r, bad);
    }

    /* A zero size with a valid alignment still returns
     * an aligned chunk that can be freed */
    void *z = NULL;
    r = posix_memalign(&z, 512, 0);

    if(r != 0 || z == NULL || ((uintptr_t) z & 511) != 0) {
        LOG_AND_ABORT("posix_memalign(512, 0) failed (%d, %p)", r, z);
    }

    free(z);

    /* iso_alloc_new_zone must return NULL for a size the
     * zone allocator cannot handle, not a masked NULL */
    iso_alloc_zone_handle *zone = iso_alloc_new_zone(SMALL_SIZE_MAX + 1);

    if(zone != NULL) {
        LOG_AND_ABORT("iso_alloc_new_zone returned %p for an oversized chunk", zone);
    }

    if(iso_zone_chunk_count(NULL) != 0) {
        LOG_AND_ABORT("iso_zone_chunk_count(NULL) is not 0");
    }

    zone = iso_alloc_new_zone(256);

    if(zone == NULL || iso_zone_chunk_count(zone) == 0) {
        LOG_AND_ABORT("iso_alloc_new_zone(256) failed");
    }

    iso_alloc_destroy_zone(zone);

    iso_flush_caches();
    iso_verify_zones();

    return 0;
}
