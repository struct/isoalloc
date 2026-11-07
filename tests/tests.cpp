/* iso_alloc tests.cpp
 * Copyright 2023 - chris.rohlf@gmail.com */

#include <memory>
#include <array>
#include <vector>
#include <new>
#include <cstdint>
#include <cstring>
#if THREAD_SUPPORT
#include <thread>
#endif
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

using namespace std;

static const uint32_t allocation_sizes[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128,
                                            ZONE_256, ZONE_512, ZONE_1024,
                                            ZONE_2048, ZONE_4096, ZONE_8192};

static const uint32_t array_sizes[] = {16, 32, 64, 128, 256, 512, 1024,
                                       2048, 4096, 8192, 16384, 32768, 65536};

int32_t alloc_count;
int32_t new_handler_calls;

struct alignas(256) Aligned256 {
    uint8_t data[300];
};

struct alignas(4096) AlignedPage {
    uint8_t data[100];
};

static void check_alignment(void *p, size_t alignment) {
    if(p == NULL) {
        LOG_AND_ABORT("Aligned new returned NULL for alignment %d", alignment);
    }

    if((reinterpret_cast<uintptr_t>(p) & (alignment - 1)) != 0) {
        LOG_AND_ABORT("Pointer %p is not %d byte aligned", p, alignment);
    }
}

/* The aligned new operators previously ignored the requested
 * alignment, and the throwing operators could return NULL
 * instead of calling the new handler and throwing bad_alloc */
static void aligned_new_tests(void) {
    auto *a = new Aligned256();
    check_alignment(a, alignof(Aligned256));
    memset(a->data, 0x41, sizeof(a->data));
    delete a;

    auto *aa = new Aligned256[8];
    check_alignment(aa, alignof(Aligned256));

    for(size_t i = 0; i < 8; i++) {
        check_alignment(&aa[i], alignof(Aligned256));
        memset(aa[i].data, 0x41, sizeof(aa[i].data));
    }

    delete[] aa;

    auto *pg = new AlignedPage();
    check_alignment(pg, alignof(AlignedPage));
    delete pg;

    /* Alignments larger than a page go through the big zone path */
    const size_t big_alignment = (1 << 20);
    void *b = ::operator new(1000, std::align_val_t(big_alignment));
    check_alignment(b, big_alignment);
    memset(b, 0x41, 1000);
    ::operator delete(b, std::align_val_t(big_alignment));

    void *n = ::operator new(100, std::align_val_t(512), std::nothrow);
    check_alignment(n, 512);
    ::operator delete(n, std::align_val_t(512), std::nothrow);

    /* A throwing new must throw bad_alloc rather than return NULL.
     * These impossible sizes make the allocator log a failed big
     * zone allocation for each attempt, which is expected */
    LOG("The following big zone allocation failures are expected");
    static volatile size_t huge = (SIZE_MAX >> 1);
    bool caught = false;

    try {
        auto *h = new uint8_t[huge];
        delete[] h;
    } catch(const std::bad_alloc &) {
        caught = true;
    }

    if(caught == false) {
        LOG_AND_ABORT("new did not throw bad_alloc for an impossible size");
    }

    caught = false;

    try {
        void *h = ::operator new(huge, std::align_val_t(4096));
        ::operator delete(h, std::align_val_t(4096));
    } catch(const std::bad_alloc &) {
        caught = true;
    }

    if(caught == false) {
        LOG_AND_ABORT("aligned new did not throw bad_alloc for an impossible size");
    }

    /* The nothrow variants return NULL instead */
    if(new(std::nothrow) uint8_t[huge] != NULL) {
        LOG_AND_ABORT("nothrow new returned memory for an impossible size");
    }

    if(::operator new(huge, std::align_val_t(4096), std::nothrow) != NULL) {
        LOG_AND_ABORT("nothrow aligned new returned memory for an impossible size");
    }

    /* An installed new handler must be called once before
     * bad_alloc is thrown. The handler uninstalls itself so
     * the retry fails and the exception follows */
    new_handler_calls = 0;
    std::set_new_handler([]() {
        new_handler_calls++;
        std::set_new_handler(nullptr);
    });

    caught = false;

    try {
        auto *h = new uint8_t[huge];
        delete[] h;
    } catch(const std::bad_alloc &) {
        caught = true;
    }

    std::set_new_handler(nullptr);

    if(caught == false || new_handler_calls != 1) {
        LOG_AND_ABORT("new handler was called %d times, bad_alloc caught = %d", new_handler_calls, caught);
    }
}

class Base {
  public:
    int32_t type;
    char *str;
};

class Derived : Base {
  public:
    Derived(int32_t i) {
        count = i;
        type = count * count;
        str = (char *) iso_alloc(1024);
        memcpy(str, "AAAAA", 5);
    }

    ~Derived() {
        count = 0;
        type = 0;
        iso_free(str);
    }

    void operator delete(void *p) {}

    void operator delete(void *p, void *h) {
        Derived *d = static_cast<Derived *>(p);
        d->~Derived();
        uint8_t *plc = static_cast<uint8_t *>(h);
        delete[] plc;
    }

#if __cplusplus >= 202002L
    /* Note this need to be class member */
    void operator delete(Derived *ptr, std::destroying_delete_t) {
        ptr->~Derived();
        ::operator delete(ptr);
    }
#endif

    uint32_t count;
};

int allocate(size_t array_size, size_t allocation_size) {
    std::vector<void *> p(array_size);
    memset(p.data(), 0x0, p.size());

    for(size_t i = 0; i < array_size; i++) {
        if(allocation_size == 0) {
            allocation_size = allocation_sizes[(rand() % sizeof(allocation_sizes) / sizeof(uint32_t))] + (rand() % 32);
        }

        p[i] = new uint8_t[allocation_size];

        if(p[i] == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations", allocation_size, alloc_count);
        }

        alloc_count++;

        /* Randomly free some allocations */
        if((rand() % 2) == 1) {
            delete[](uint8_t *) p[i];
            p[i] = NULL;
        }
    }

    /* Free the remaining allocations */
    for(size_t i = 0; i < array_size; i++) {
        if(p[i] != NULL) {
            delete[](uint8_t *) p[i];
        }
    }

    return OK;
}

int main(int argc, char *argv[]) {
    char *a = (char *) iso_alloc(100);
    iso_free(a);
    auto d = std::make_unique<Derived>(100);
    constexpr size_t array_sizeslen = sizeof(array_sizes) / sizeof(uint32_t);

    for(size_t i = 0; i < array_sizeslen; i++) {
        for(size_t z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
            allocate(array_sizes[i], allocation_sizes[z]);
        }
    }

    for(size_t i = 0; i < array_sizeslen; i++) {
        allocate(array_sizes[i], 0);
        Base *b = new Base();
        delete b;
        auto d = std::make_unique<Derived>(i);
    }

    for(size_t i = 0; i < array_sizeslen; i++) {
        allocate(array_sizes[i], 0);
        auto *ptr = new uint8_t[sizeof(Derived)];
        auto *d = new(ptr) Derived(i);
        Derived::operator delete(d, ptr);
    }

#if THREAD_SUPPORT && __linux__
    for(size_t i = 0; i < 4; i++) {
        std::array<std::thread, 4> t;
        for(size_t z = 0; z < 4; z++) {
            t[i] = std::thread(allocate, array_sizes[i], allocation_sizes[z]);
            t[i].join();
        }
    }
#endif

    aligned_new_tests();

    iso_verify_zones();

    return 0;
}
