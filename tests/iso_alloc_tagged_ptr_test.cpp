/* iso_alloc iso_alloc_tagged_ptr_test.cpp
 * Copyright 2022 - chris.rohlf@gmail.com */

#include <memory>
#include <iostream>
#include <ostream>
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

iso_alloc_zone_handle *_zone_handle;

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
        str = (char *) iso_alloc(32);
        memcpy(str, "AAAAA", 5);
    }

    ~Derived() {
        count = 0;
        type = 0;
        iso_free(str);
    }

    char *GetStr() {
        return str;
    }

    static Derived *Create(int32_t i) {
        // Allocate a chunk of memory from a private zone
        void *b = iso_alloc_from_zone(_zone_handle, sizeof(Derived));

        // Construct an object of type Derived in that chunk
        auto d = new (b) Derived(i);

        // Return the new Derived object instance
        return d;
    }

    uint32_t count;
};

template <typename T>
class IsoAllocPtr {
  public:
    IsoAllocPtr(iso_alloc_zone_handle *handle, T *ptr) : eptr(0), zone(handle) {
        uint64_t tag = GetTag(ptr);
        eptr = (tag << 48) | reinterpret_cast<uintptr_t>(ptr);
    }

    T *operator->() {
        T *p = reinterpret_cast<T *>(eptr & 0x0000ffffffffffff);
        uint64_t tag = GetTag(p);
        return reinterpret_cast<T *>((tag << 48) ^ eptr);
    }

    uint16_t GetTag(T *ptr) {
        uint8_t tag = iso_alloc_get_mem_tag(ptr, zone);
        return tag;
    }

    uintptr_t eptr;
    iso_alloc_zone_handle *zone;
};

int main(int argc, char *argv[]) {
    // Create a private IsoAlloc zone
    _zone_handle = iso_alloc_new_zone(sizeof(Derived));

    for(int32_t i = 0; i < 65535*32;i++) {
        auto d = Derived::Create(256);

        // Wrap the new object in an IsoAllocPtr
        IsoAllocPtr<Derived> e(_zone_handle, d);

        // Use the IsoAllocPtr operator ->
        if(e->GetStr() == nullptr) {
            abort();
        }

        // Tools like AddressSanitizer will throw an error
        // here because it doesn't think the underlying chunk was
        // allocated with malloc() because we used placement new
        // but IsoAlloc properly handles the iso_free() call
        // invoked from operator delete
        delete d;

        // If you need to make ASAN happy this ugly code will work
        // because it invokes the destructor manually and then calls
        // the appropriate iso alloc free function
        //d->~Derived();
        //iso_free_from_zone(b, handle);
    }

    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
