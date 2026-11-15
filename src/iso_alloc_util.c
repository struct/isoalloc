/* iso_alloc_util.c - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

#if CPU_PIN
/* sched_getcpu's performance depends on the
 * architecture/kernel version, so we lower
 * the cost of feature's abstraction here. */
int _iso_getcpu(void) {
#if defined(SCHED_GETCPU)
    return sched_getcpu();
#elif defined(__x86_64__)
    /* rdtscp is not always available and is pretty slow
     * we instead load from the global descriptor table
     * then "mov" it to 'a' */
    unsigned int a;
    const unsigned int cpunodesegment = 15 * 8 + 3;
    __asm__ volatile("lsl %1, %0"
                     : "=r"(a)
                     : "r"(cpunodesegment));
    return (int) (a & 0xfff);
#elif defined(__aarch64__)
#if __APPLE__
    /* Older versions of macOS stored the CPU number in the low
     * bits of tpidrro_el0. Current versions do not, the low byte
     * is constant across every core, and there is no supported
     * user space interface for the current CPU. Returning -1
     * disables pinning rather than pinning every zone to a
     * number that is never the real core */
    return -1;
#else
    /* TODO most likely different register/making on other platforms */
    return -1;
#endif
#else
    return -1;
#endif
}
#endif

void darwin_reuse(void *p, size_t size) {
#if __APPLE__
    while(madvise(p, size, MADV_FREE_REUSE) && errno == EAGAIN) {
    }
#endif
}

void *create_guard_page(void *p) {
    if(p == NULL) {
        p = mmap_rw_pages(g_page_size, false, NULL);

        if(p == NULL) {
            LOG_AND_ABORT("Could not allocate guard page");
        }
    }

    mprotect_pages(p, g_page_size, PROT_NONE);
    name_mapping(p, g_page_size, GUARD_PAGE_NAME);
    return p;
}

/* Assumes p is page aligned and surrounded by guard pages */
void unmap_guarded_pages(void *p, size_t size) {
    size_t sz = ROUND_UP_PAGE(size);
    munmap(p - g_page_size, sz + (g_page_size << 1));
}

/* Assumes size for guard pages is NOT accounted for.
 * Returns a pointer to a contiguous set of RW pages
 * with guard pages mapped on top and bottom. */
void *mmap_guarded_rw_pages(size_t size, bool populate, const char *name) {
    size_t sz = ROUND_UP_PAGE(size);

    if(sz < size) {
        return NULL;
    }

    void *p = mmap_rw_pages(sz + (g_page_size * 2), populate, name);

    create_guard_page(p);
    create_guard_page(p + (g_page_size + sz));
    return (p + g_page_size);
}

#if ARM_MTE
void *mmap_guarded_rw_mte_pages(size_t size, bool populate, const char *name) {
    size_t sz = ROUND_UP_PAGE(size);

    if(sz < size) {
        return NULL;
    }

    void *p = mmap_rw_mte_pages(sz + (g_page_size * 2), populate, name);

    create_guard_page(p);
    create_guard_page(p + (g_page_size + sz));
    return (p + g_page_size);
}
#endif

static void *mmap_pages_hint(void *p, size_t size, bool populate, const char *name, int32_t prot);

void *mmap_rw_pages(size_t size, bool populate, const char *name) {
    return mmap_pages(size, populate, name, PROT_READ | PROT_WRITE);
}

#if ARM_MTE
void *mmap_rw_mte_pages(size_t size, bool populate, const char *name) {
    return mmap_pages(size, populate, name, PROT_READ | PROT_WRITE | PROT_MTE);
}
#endif

/* A random page aligned address to pass to mmap as a hint so
 * mappings land at unpredictable addresses */
static INLINE uint64_t random_mmap_hint(void) {
#if !ENABLE_ASAN
    uint64_t hint = ROUND_DOWN_PAGE(rand_uint64());
    return (hint & 0x3FFFFFFFF000);
#else
    return 0;
#endif
}

/* Maps the pages that back a zone such that the user pages,
 * which start user_offset bytes into the mapping, are aligned
 * to ZONE_USER_SIZE. That alignment makes the 4 MB window a
 * pointer lies in name its zone, see iso_find_zone_range. The
 * hint is aligned first so the kernel usually honours it and
 * the mapping costs one call. If it does not, a mapping one
 * zone larger is trimmed down to an aligned one */
void *mmap_zone_pages(size_t size, size_t user_offset, const char *name, int32_t prot) {
    const size_t sz = ROUND_UP_PAGE(size);

    if(UNLIKELY(sz < size || user_offset >= sz)) {
        return NULL;
    }

    uint64_t hint = (random_mmap_hint() & ~((uint64_t) ZONE_USER_SIZE - 1));
    hint = (hint >= user_offset) ? (hint - user_offset) : 0;
    void *p = mmap_pages_hint((void *) hint, sz, false, name, prot);

    if(LIKELY((((uintptr_t) p + user_offset) & (ZONE_USER_SIZE - 1)) == 0)) {
        return p;
    }

    munmap(p, sz);

    const size_t over = (sz + ZONE_USER_SIZE);
    uint8_t *base = (uint8_t *) mmap_pages_hint((void *) random_mmap_hint(), over, false, name, prot);
    const uintptr_t user = ((((uintptr_t) base + user_offset) + (ZONE_USER_SIZE - 1)) & ~((uintptr_t) ZONE_USER_SIZE - 1));
    uint8_t *start = (uint8_t *) (user - user_offset);
    const size_t head = (size_t) (start - base);
    const size_t tail = (over - head - sz);

    if(head != 0) {
        munmap(base, head);
    }

    if(tail != 0) {
        munmap(start + sz, tail);
    }

    return start;
}

void *mmap_pages(size_t size, bool populate, const char *name, int32_t prot) {
    return mmap_pages_hint((void *) random_mmap_hint(), size, populate, name, prot);
}

static void *mmap_pages_hint(void *p, size_t size, bool populate, const char *name, int32_t prot) {
    size_t sz = ROUND_UP_PAGE(size);

    if(sz < size) {
        return NULL;
    }

    int32_t flags = (MAP_PRIVATE | MAP_ANONYMOUS);
    int fd = -1;

#if __linux__
#if PRE_POPULATE_PAGES
    if(populate == true) {
        flags |= MAP_POPULATE;
    }
#endif

#if MAP_HUGETLB && HUGE_PAGES
    /* If we are allocating pages for a user zone
     * then take advantage of the huge TLB */
    if(sz == ZONE_USER_SIZE || sz == (ZONE_USER_SIZE >> 1)) {
        flags |= MAP_HUGETLB;
    }
#endif
#elif __APPLE__
#if VM_FLAGS_SUPERPAGE_SIZE_2MB && HUGE_PAGES
    /* If we are allocating pages for a user zone
     * we are going to use the 2 MB superpage flag */
    if(sz == ZONE_USER_SIZE || sz == (ZONE_USER_SIZE >> 1)) {
        fd = VM_FLAGS_SUPERPAGE_SIZE_2MB;
    }
#endif
#endif

    p = mmap(p, sz, prot, flags, fd, 0);

#if __linux__ && MAP_HUGETLB && HUGE_PAGES
    /* If the huge page allocation failed, retry with regular pages.
     * This can happen when /proc/sys/vm/nr_hugepages is 0 or
     * exhausted, which is common in LD_PRELOAD environments. */
    if(p == MAP_FAILED && (flags & MAP_HUGETLB)) {
        flags &= ~MAP_HUGETLB;
        p = mmap(p, sz, prot, flags, fd, 0);
    }
#elif __APPLE__ && VM_FLAGS_SUPERPAGE_SIZE_2MB && HUGE_PAGES
    /* Same fallback for macOS superpage allocations */
    if(p == MAP_FAILED && fd == VM_FLAGS_SUPERPAGE_SIZE_2MB) {
        fd = -1;
        p = mmap(p, sz, prot, flags, fd, 0);
    }
#endif

    if(p == MAP_FAILED) {
        LOG_AND_ABORT("Failed to mmap rw pages");
    }

#if __linux__ && MAP_HUGETLB && HUGE_PAGES && THP_PAGES && MADV_HUGEPAGE
    if(sz == ZONE_USER_SIZE || sz == (ZONE_USER_SIZE >> 1)) {
        madvise(p, sz, MADV_HUGEPAGE);
    }
#endif

    /* All pages are mapped as if we will never need
     * them. This is to ensure RSS stays managable */
    dont_need_pages(p, sz);

    if(name != NULL) {
        name_mapping(p, sz, name);
    }

    return p;
}

void dont_need_pages(void *p, size_t size) {
    madvise(p, size, FREE_OR_DONTNEED);

#if __APPLE__
    while(madvise(p, size, MADV_FREE_REUSE) == -1 && errno == EAGAIN) {
    }
#endif
}

/* Returns the pages of a zone to the OS so they read as zero
 * the next time they are touched, while the mapping and its
 * guard pages stay in place. The zone keeps its address and
 * every reference to it, which is what makes resetting a zone
 * cheap compared to unmapping and mapping another. The zero
 * guarantee matters: a reset zone has an all zero bitmap and no
 * canaries written, and calloc trusts that a never used chunk
 * is zero */
void decommit_zone_pages(void *p, size_t size, int32_t prot) {
#if __linux__
    /* Private anonymous pages dropped this way are zero filled
     * on the next fault. Huge page mappings may refuse it */
    if(madvise(p, size, MADV_DONTNEED) == 0) {
        return;
    }
#endif

    /* Replace the range with a fresh mapping. MAP_FIXED over an
     * existing mapping is atomic so the range is never unmapped.
     * On macOS MADV_FREE_REUSABLE would not guarantee zeroes */
    void *r = mmap(p, size, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if(UNLIKELY(r != p)) {
        LOG_AND_ABORT("Failed to decommit zone pages @ 0x%p (%s)", p, strerror(errno));
    }

#if __linux__ && !HUGE_PAGES && defined(MADV_NOHUGEPAGE)
    madvise(p, size, MADV_NOHUGEPAGE);
#endif
}

/* A cheap, coarse monotonic clock in milliseconds for the idle
 * zone purge. Precision of a few milliseconds is plenty */
uint64_t coarse_now_ms(void) {
    struct timespec ts;
#if __APPLE__
    clock_gettime(CLOCK_MONOTONIC_RAW_APPROX, &ts);
#elif defined(CLOCK_MONOTONIC_COARSE)
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return ((uint64_t) ts.tv_sec * 1000) + ((uint64_t) ts.tv_nsec / 1000000);
}

void mprotect_pages(void *p, size_t size, int32_t protection) {
    if((mprotect(p, size, protection)) == ERR) {
        LOG_AND_ABORT("Failed to mprotect pages @ 0x%p (%s)", p, strerror(errno));
    }
}

int32_t name_mapping(void *p, size_t sz, const char *name) {
#if NAMED_MAPPINGS && (__ANDROID__ || KERNEL_VERSION_SEQ_5_17)
    return prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, p, sz, name);
#else
    return 0;
#endif
}

bool is_pow2(uint64_t sz) {
    return sz != 0 && (sz & (sz - 1)) == 0;
}

size_t next_pow2(size_t sz) {
    sz |= sz >> 1;
    sz |= sz >> 2;
    sz |= sz >> 4;
    sz |= sz >> 8;
    sz |= sz >> 16;
    sz |= sz >> 32;
    return sz + 1;
}

const uint32_t _log_table[32] = {
    0, 9, 1, 10, 13, 21, 2, 29,
    11, 14, 16, 18, 22, 25, 3, 30,
    8, 12, 20, 28, 15, 17, 24, 7,
    19, 27, 23, 6, 26, 5, 4, 31};

/* Fast log2() implementation for 32 bit integers */
uint32_t _log2(uint32_t v) {
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return _log_table[(uint32_t) (v * 0x07C4ACDD) >> 27];
}
