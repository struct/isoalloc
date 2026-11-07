// iso_alloc.cpp - A secure memory allocator
// Copyright 2023 - chris.rohlf@gmail.com

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if CPP_SUPPORT
#if MALLOC_HOOK

#include <new>

#if !ABORT_ON_NULL
#define NEW_EXCEPT noexcept(false)

void iso_cpp_handler() {
    throw std::bad_alloc();
}

extern "C" __attribute__((constructor(FIRST_CTOR + 1))) void iso_cpp(void) {
    std::set_new_handler(iso_cpp_handler);
}
#else
#define NEW_EXCEPT
#endif

// A throwing operator new may never return NULL. The
// standard requires that we retry through the installed
// new handler and throw bad_alloc if there is none
static inline void *iso_cpp_new(size_t size) {
    void *p = iso_alloc(size);

    while(UNLIKELY(p == NULL)) {
        std::new_handler h = std::get_new_handler();

        if(h == nullptr) {
            throw std::bad_alloc();
        }

        h();
        p = iso_alloc(size);
    }

    return p;
}

static inline void *iso_cpp_new_aligned(size_t size, std::align_val_t val) {
    const size_t alignment = static_cast<size_t>(val);

    if(is_pow2(alignment) == false) {
        throw std::bad_alloc();
    }

    void *p = _iso_alloc_aligned(size, alignment);

    while(UNLIKELY(p == NULL)) {
        std::new_handler h = std::get_new_handler();

        if(h == nullptr) {
            throw std::bad_alloc();
        }

        h();
        p = _iso_alloc_aligned(size, alignment);
    }

    return p;
}

// These hooks override the basic new/delete
// operators to use the iso_alloc API

EXTERNAL_API FLATTEN void *operator new(size_t size) NEW_EXCEPT {
    return iso_cpp_new(size);
}

EXTERNAL_API FLATTEN void operator delete(void *p) noexcept {
    iso_free(p);
}

EXTERNAL_API FLATTEN void *operator new[](size_t size) NEW_EXCEPT {
    return iso_cpp_new(size);
}

EXTERNAL_API FLATTEN void operator delete[](void *p) noexcept {
    iso_free(p);
}

EXTERNAL_API FLATTEN void *operator new(size_t size, const std::nothrow_t &) noexcept {
    return iso_alloc(size);
}

EXTERNAL_API FLATTEN void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    return iso_alloc(size);
}

FLATTEN void operator delete(void *ptr, size_t size) noexcept {
    return iso_free_size(ptr, size);
}

FLATTEN void operator delete[](void *ptr, size_t size) noexcept {
    return iso_free_size(ptr, size);
}

EXTERNAL_API FLATTEN void operator delete(void *ptr, const std::nothrow_t &) noexcept {
    return iso_free(ptr);
}

EXTERNAL_API FLATTEN void operator delete[](void *ptr, const std::nothrow_t &) noexcept {
    return iso_free(ptr);
}

EXTERNAL_API FLATTEN void *operator new(size_t size, std::align_val_t val) NEW_EXCEPT {
    return iso_cpp_new_aligned(size, val);
}

EXTERNAL_API FLATTEN void *operator new[](size_t size, std::align_val_t val) NEW_EXCEPT {
    return iso_cpp_new_aligned(size, val);
}

EXTERNAL_API FLATTEN void operator delete(void *p, std::align_val_t val) noexcept {
    iso_free(p);
}

EXTERNAL_API FLATTEN void operator delete[](void *p, std::align_val_t val) noexcept {
    iso_free(p);
}

EXTERNAL_API FLATTEN void *operator new(size_t size, std::align_val_t val, std::nothrow_t &) noexcept {
    return _iso_alloc_aligned(size, static_cast<size_t>(val));
}

EXTERNAL_API FLATTEN void *operator new[](size_t size, std::align_val_t val, std::nothrow_t &) noexcept {
    return _iso_alloc_aligned(size, static_cast<size_t>(val));
}

EXTERNAL_API FLATTEN void operator delete(void *p, std::align_val_t val, std::nothrow_t &) noexcept {
    iso_free(p);
}

EXTERNAL_API FLATTEN void operator delete[](void *p, std::align_val_t val, std::nothrow_t &) noexcept {
    iso_free(p);
}

#endif
#endif
