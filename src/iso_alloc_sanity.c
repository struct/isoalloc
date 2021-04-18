/* iso_alloc.c - A secure memory allocator
 * Copyright 2021 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

#if ALLOC_SANITY
atomic_flag sane_cache_flag;
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
                reg.range.len = sane_alloc->size;
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

/* Callers of this function should hold the sanity cache lock */
INTERNAL_HIDDEN _sane_allocation_t *_get_sane_alloc(void *p) {
    if(_sane_cache[SANE_CACHE_IDX(p)] == 0) {
        return NULL;
    }

    for(uint32_t i = 0; i < MAX_SANE_SAMPLES; i++) {
        if(_sane_allocations[i].address == p) {
            return &_sane_allocations[i];
        }
    }

    return NULL;
}

INTERNAL_HIDDEN int32_t _iso_alloc_free_sane_sample(void *p) {
    LOCK_SANITY_CACHE();
    _sane_allocation_t *sane_alloc = _get_sane_alloc(p);

    if(sane_alloc != NULL) {
        munmap(sane_alloc->guard_below, g_page_size);
        munmap(sane_alloc->guard_above, g_page_size);
        munmap(p, sane_alloc->size);
        memset(sane_alloc, 0x0, sizeof(_sane_allocation_t));
        _sane_cache[SANE_CACHE_IDX(p)]--;
        _sane_sampled--;
        UNLOCK_SANITY_CACHE();
        return OK;
    }

    UNLOCK_SANITY_CACHE();
    return ERR;
}

INTERNAL_HIDDEN void *_iso_alloc_sample(size_t size) {
#if UNINIT_READ_SANITY
    if(_page_fault_thread == 0 || LIKELY((rand_uint64() % SANITY_SAMPLE_ODDS) != 1)) {
#else
    if(LIKELY((rand_uint64() % SANITY_SAMPLE_ODDS) != 1)) {
#endif
        return NULL;
    }

    _sane_allocation_t *sane_alloc = NULL;

    LOCK_SANITY_CACHE();

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
    sane_alloc->size = ROUND_UP_PAGE(size);
    void *p = mmap_rw_pages(sane_alloc->size + (g_page_size * 2), false);

    if(p == NULL) {
        LOG_AND_ABORT("Cannot allocate pages for sampled allocation");
    }

    sane_alloc->guard_below = p;
    create_guard_page(sane_alloc->guard_below);
    sane_alloc->guard_above = (void *) ROUND_UP_PAGE((uintptr_t)(p + sane_alloc->size + g_page_size));
    create_guard_page(sane_alloc->guard_above);

    p = (p + g_page_size);

    /* We may right align the mapping to catch overflows */
    if(rand_uint64() % 1 == 1) {
        p = (p + g_page_size) - sane_alloc->orig_size;
    }

#if UNINIT_READ_SANITY
    struct uffdio_register reg = {0};
    reg.range.start = (uint64_t) ROUND_DOWN_PAGE(p);
    reg.range.len = sane_alloc->size;
    reg.mode = UFFDIO_REGISTER_MODE_MISSING;
#endif

    sane_alloc->address = p;

    _sane_cache[SANE_CACHE_IDX(p)]++;
    _sane_sampled++;

#if UNINIT_READ_SANITY
    if((ioctl(_uf_fd, UFFDIO_REGISTER, &reg)) == ERR) {
        LOG_AND_ABORT("Failed to register address %p", p);
    }
#endif

    UNLOCK_SANITY_CACHE();
    return p;
}

#endif
