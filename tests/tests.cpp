/* iso_alloc tests.cpp
 * Copyright 2023 - chris.rohlf@gmail.com */

#include <memory>
#include <array>
#include <vector>
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

#if THREAD_SUPPORT
    for(size_t i = 0; i < 4; i++) {
        std::array<std::thread, 4> t;
        for(size_t z = 0; z < 4; z++) {
            t[i] = std::thread(allocate, array_sizes[i], allocation_sizes[z]);
            t[i].join();
        }
    }
#endif

    iso_verify_zones();

    return 0;
}
