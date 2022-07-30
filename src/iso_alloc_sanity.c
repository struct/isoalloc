/* iso_alloc_sanity.c - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

#if ALLOC_SANITY

#if THREAD_SUPPORT
#if USE_SPINLOCK
atomic_flag sane_cache_flag;
#else
pthread_mutex_t sane_cache_mutex;
#endif
#endif

uint64_t _sanity_canary;
int32_t _sane_sampled;
uint8_t _sane_cache[SANE_CACHE_SIZE];
_sane_allocation_t _sane_allocations[MAX_SANE_SAMPLES];

#if UNINIT_READ_SANITY
pthread_t _page_fault_thread;
struct uffdio_api _uffd_api;
int64_t _uf_fd;

INTERNAL_HIDDEN void _iso_alloc_setup_userfaultfd() {
    _uf_fd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);

    if(_uf_fd == ERR) {
        LOG_AND_ABORT("This kernel does not support userfaultfd");
    }

    _uffd_api.api = UFFD_API;
    _uffd_api.features = 0;

    if(ioctl(_uf_fd, UFFDIO_API, &_uffd_api) == ERR) {
        LOG_AND_ABORT("Failed to setup userfaultfd with ioctl");
    }

    if(_page_fault_thread == 0) {
        int32_t s = pthread_create(&_page_fault_thread, NULL, _page_fault_thread_handler, NULL);

        if(s != OK) {
            LOG_AND_ABORT("Cannot create userfaultfd handler thread");
        }
    }
}

INTERNAL_HIDDEN void *_page_fault_thread_handler(void *unused) {
    static struct uffd_msg umsg;
    ssize_t n;

    while(true) {
        struct pollfd pollfd;
        int32_t ret;

        pollfd.fd = _uf_fd;
        pollfd.events = POLLIN;

        ret = poll(&pollfd, 1, -1);

        if(ret == ERR) {
            LOG_AND_ABORT("Failed to poll userfaultfd file descriptor");
        }

        n = read(_uf_fd, &umsg, sizeof(struct uffd_msg));

        if(n == OK) {
            LOG_AND_ABORT("Got EOF on userfaultfd file descriptor")
        }

        if(n == ERR) {
            LOG_AND_ABORT("Failed to read from userfaultfd file descriptor")
        }

        if(umsg.event != UFFD_EVENT_PAGEFAULT) {
            LOG_AND_ABORT("Received non-page-fault event from userfaultfd")
        }

        LOCK_SANITY_CACHE();
        _sane_allocation_t *sane_alloc = _get_sane_alloc((void *) umsg.arg.pagefault.address);

        /* This is where we detect uninitialized reads. Whenever we
         * receive a page fault we check if its a read or a write operation.
         * If its a write then we unregister the page from userfaultfd
         * but if its a read then we assume this chunk was not initialized.
         * It is possible we will receive a read event while we are
         * unregistering a page that was previously written to */
        if((umsg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) == 1) {
            /* Unregister this page but don't remove it from our cache
             * of tracked pages, we still need to unmap it at some point */
            struct uffdio_register reg = {0};

            if(sane_alloc != NULL) {
                reg.range.start = (uint64_t) sane_alloc->address;
                reg.range.len = g_page_size;
            } else {
                /* We received a page fault for an address we are no
                 * longer tracking. We don't know why but it's a write
                 * and we don't care about writes */
                reg.range.start = umsg.arg.pagefault.address;
                reg.range.len = g_page_size;
            }

            if((ioctl(_uf_fd, UFFDIO_UNREGISTER, &reg.range)) == ERR) {
                LOG_AND_ABORT("Failed to unregister address %p", umsg.arg.pagefault.address);
            }

            UNLOCK_SANITY_CACHE();
            continue;
        }

        /* Detects a read of an uninitialized page */
        if((umsg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) == 0) {
            LOG_AND_ABORT("Uninitialized read detected on page %p", umsg.arg.pagefault.address);
        }

        UNLOCK_SANITY_CACHE();
    }

    UNLOCK_SANITY_CACHE();
    return NULL;
}
#endif /* UNINIT_READ_SANITY */

INTERNAL_HIDDEN INLINE void write_sanity_canary(void *p) {
    const uint64_t canary = (_sanity_canary & SANITY_CANARY_VALIDATE_MASK);

    for(int32_t i = 0; i < (g_page_size / sizeof(uint64_t)); i++) {
        *(uint64_t *) p = canary;
        p += sizeof(uint64_t);
    }
}

/* Verify the canary value in an allocation */
INTERNAL_HIDDEN INLINE void check_sanity_canary(_sane_allocation_t *sane_alloc) {
    void *end = NULL;
    void *start = NULL;

    if(sane_alloc->right_aligned == true) {
        end = ((sane_alloc->address + g_page_size) - sane_alloc->orig_size);
        start = sane_alloc->address;
    } else {
        end = sane_alloc->address + g_page_size;
        start = sane_alloc->address + sane_alloc->orig_size;
    }

    while(start < end) {
        uint64_t v = *((uint64_t *) start);
        uint64_t canary = (_sanity_canary & SANITY_CANARY_VALIDATE_MASK);

        if(UNLIKELY(v != canary)) {
            LOG_AND_ABORT("Sanity canary at 0x%p has been corrupted! Value: 0x%x Expected: 0x%x", start, v, canary);
        }

        start += sizeof(uint64_t);
    }
}

/* Callers of this function should hold the sanity cache lock */
INTERNAL_HIDDEN _sane_allocation_t *_get_sane_alloc(void *p) {
    if(_sane_cache[SANE_CACHE_IDX(p)] == 0) {
        return NULL;
    }

    void *pa = NULL;

    if(IS_PAGE_ALIGNED((uintptr_t) p)) {
        pa = (void *) ROUND_DOWN_PAGE((uintptr_t) p);
    } else {
        pa = p;
    }

    for(uint32_t i = 0; i < MAX_SANE_SAMPLES; i++) {
        if(_sane_allocations[i].address == pa) {
            return &_sane_allocations[i];
        }
    }

    return NULL;
}

INTERNAL_HIDDEN int32_t _iso_alloc_free_sane_sample(void *p) {
    LOCK_SANITY_CACHE();
    _sane_allocation_t *sane_alloc = _get_sane_alloc(p);

    if(sane_alloc != NULL) {
        check_sanity_canary(sane_alloc);

        munmap(sane_alloc->guard_below, g_page_size);
        munmap(sane_alloc->guard_above, g_page_size);
        munmap(sane_alloc->address, g_page_size);
        memset(sane_alloc, 0x0, sizeof(_sane_allocation_t));
        _sane_cache[SANE_CACHE_IDX(p)]--;
        _sane_sampled--;
        UNLOCK_SANITY_CACHE();
        return OK;
    }

    UNLOCK_SANITY_CACHE();
    return ERR;
}

INTERNAL_HIDDEN void *_iso_alloc_sample(const size_t size) {
#if UNINIT_READ_SANITY
    if(_page_fault_thread == 0 || LIKELY((rand_uint64() % SANITY_SAMPLE_ODDS) != 1)) {
#else
    if(LIKELY((rand_uint64() % SANITY_SAMPLE_ODDS) != 1)) {
#endif
        return NULL;
    }

    _sane_allocation_t *sane_alloc = NULL;

    LOCK_SANITY_CACHE();
    UNLOCK_ROOT();

    /* Find the first free slot in our sampled storage */
    for(uint32_t i = 0; i < MAX_SANE_SAMPLES; i++) {
        if(_sane_allocations[i].address == 0) {
            sane_alloc = &_sane_allocations[i];
            break;
        }
    }

    /* There are no available slots in the cache */
    if(sane_alloc == NULL) {
        LOG_AND_ABORT("There are no free slots in the cache, there should be %d", _sane_sampled);
    }

    sane_alloc->orig_size = size;
    void *p = mmap_rw_pages(g_page_size * 3, false, SAMPLED_ALLOC_NAME);

    if(p == NULL) {
        LOG_AND_ABORT("Cannot allocate pages for sampled allocation");
    }

    sane_alloc->guard_below = p;
    create_guard_page(sane_alloc->guard_below);
    sane_alloc->guard_above = (void *) ROUND_UP_PAGE((uintptr_t) (p + (g_page_size * 2)));
    create_guard_page(sane_alloc->guard_above);

    p = (p + g_page_size);

    /* We may right align the mapping to catch overflows */
    if(rand_uint64() % 1 == 1) {
        p = (p + g_page_size) - sane_alloc->orig_size;
        sane_alloc->right_aligned = true;
        sane_alloc->address = (void *) ROUND_DOWN_PAGE((uintptr_t) p);
    } else {
        sane_alloc->address = p;
    }

#if UNINIT_READ_SANITY
    struct uffdio_register reg = {0};
    reg.range.start = (uint64_t) ROUND_DOWN_PAGE(p);
    reg.range.len = g_page_size;
    reg.mode = UFFDIO_REGISTER_MODE_MISSING;
#endif

    _sane_cache[SANE_CACHE_IDX(p)]++;
    _sane_sampled++;

#if UNINIT_READ_SANITY
    if((ioctl(_uf_fd, UFFDIO_REGISTER, &reg)) == ERR) {
        LOG_AND_ABORT("Failed to register address %p", p);
    }
#endif

#if !UNINIT_READ_SANITY
    write_sanity_canary(sane_alloc->address);
#endif

    UNLOCK_SANITY_CACHE();
    return p;
}
#endif

#if MEMCPY_SANITY

#define MEMCPY_SANITY_CHK(p) (user_pages_start <= p && (user_pages_start + ZONE_USER_SIZE) - n > p && n > zone->chunk_size)

INTERNAL_HIDDEN void *__iso_memcpy(void *restrict dest, const void *restrict src, size_t n) {
    char *p_dest = (char *) dest;
    char const *p_src = (char const *) src;

    while(n--) {
        *p_dest++ = *p_src++;
    }

    return dest;
}

INTERNAL_HIDDEN void *_iso_alloc_memcpy(void *restrict dest, const void *restrict src, size_t n) {
    if(n > SMALLEST_CHUNK_SZ) {
        /* We don't want to add too much overhead here so we only
         * check the chunk-to-zone cache for zone data and we don't
         * need to lock the root for that. Its possible for a cache
         * miss to mean a security check doesn't happen here but
         * this feature is more for catching bugs than it is for
         * mitigating them */
        iso_alloc_zone_t *zone = search_chunk_lookup_table(dest);
        void *user_pages_start = UNMASK_USER_PTR(zone);

        if(MEMCPY_SANITY_CHK(dest)) {
            LOG_AND_ABORT("Detected an out of bounds write memcpy: dest=0x%p (%d bytes) src=0x%p size=%d", dest, zone->chunk_size, src, n);
        }

        zone = search_chunk_lookup_table(src);
        user_pages_start = UNMASK_USER_PTR(zone);

        if(MEMCPY_SANITY_CHK(src)) {
            LOG_AND_ABORT("Detected an out of bounds read memcpy: dest=0x%p src=0x%p (%d bytes) size=%d", dest, src, zone->chunk_size, n);
        }
    }

    return __iso_memcpy(dest, src, n);
}
#endif

#if MEMSET_SANITY

#define MEMSET_SANITY_CHK(p) (user_pages_start <= p && (user_pages_start + ZONE_USER_SIZE) - n > p && n > zone->chunk_size)

INTERNAL_HIDDEN void *__iso_memset(void *dest, int b, size_t n) {
    char *p_dest = (char *) dest;

    while(n--) {
        *p_dest++ = b;
    }

    return dest;
}

INTERNAL_HIDDEN void *_iso_alloc_memset(void *dest, int b, size_t n) {
    if(n > SMALLEST_CHUNK_SZ) {
        iso_alloc_zone_t *zone = search_chunk_lookup_table(dest);
        void *user_pages_start = UNMASK_USER_PTR(zone);

        if(MEMSET_SANITY_CHK(dest)) {
            LOG_AND_ABORT("Detected an out of bounds write memset: dest=0x%p (%d bytes) size=%d", dest, zone->chunk_size, n);
        }
    }

    return __iso_memset(dest, b, n);
}
#endif
