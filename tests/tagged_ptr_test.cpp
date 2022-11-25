// iso_alloc tagged_ptr_test.cpp
// Copyright 2022 - chris.rohlf@gmail.com

// This test should successfully run with or
// without MEMORY_TAGGING support

#include <memory>
#include <iostream>
#include <ostream>
#include <string.h>
#include "iso_alloc.h"

iso_alloc_zone_handle *_zone_handle;
constexpr uint32_t _str_size = 32;

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
        str = (char *) iso_alloc(_str_size);
        memset(str, 0x41, _str_size);
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
        // Only IsoAlloc private zones have memory tags associated
        // with each chunk in the zone. Thats why we use this interface
        // and not the standard overloaded operator new or malloc
        void *b = iso_alloc_from_zone(_zone_handle);

        if(b == nullptr) {
            return nullptr;
        }

        // Construct an object of type Derived in that chunk
        auto d = new(b) Derived(i);

        // Return a pointer to the new Derived object instance
        return d;
    }

    uint32_t count;
};

// This is a working example of how to construct a C++
// smart pointer that utilizes memory tagging applied
// to each IsoAlloc private zone
template <typename T>
class IsoAllocPtr {
  public:
    IsoAllocPtr(iso_alloc_zone_handle *handle, T *ptr) : eptr(nullptr), zone(handle) {
        eptr = iso_alloc_tag_ptr((void *) ptr, zone);
    }

    T *operator->() {
        T *p = reinterpret_cast<T *>(iso_alloc_untag_ptr(eptr, zone));
        return p;
    }

    void *eptr;
    iso_alloc_zone_handle *zone;
};

int main(int argc, char *argv[]) {
    // Create a private IsoAlloc zone
    _zone_handle = iso_alloc_new_zone(sizeof(Derived));

    for(int32_t i = 0; i < 65535; i++) {
        auto d = Derived::Create(i);

        if(d == nullptr) {
            abort();
        }

        // Wrap the new object in an IsoAllocPtr. The template
        // needs to know about our zone handle and the object
        // we want to manipulate through this pointer
        IsoAllocPtr<Derived> e(_zone_handle, d);

        // Use the IsoAllocPtr operator -> the same way we
        // would a raw pointer to the Derived object
        if(e->GetStr() == nullptr) {
            abort();
        }

#if ENABLE_ASAN
        // If you need to make ASAN happy this ugly code will work
        // because it invokes the destructor manually and then calls
        // the appropriate IsoAlloc free function
        d->~Derived();
        iso_free_from_zone(d, _zone_handle);
#else
        // Tools like AddressSanitizer will throw an error
        // here because it doesn't think the underlying chunk was
        // allocated with malloc() because we used placement new
        // but IsoAlloc properly handles the iso_free() call
        // invoked from operator delete

        delete d;
#endif
    }

    // Destroy the private zone we created
    iso_alloc_destroy_zone(_zone_handle);

    return 0;
}
