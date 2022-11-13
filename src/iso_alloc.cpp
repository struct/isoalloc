// iso_alloc.cpp - A secure memory allocator
// Copyright 2022 - chris.rohlf@gmail.com

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

extern "C" __attribute__((constructor(FIRST_CTOR+1))) void iso_cpp(void) {
    std::set_new_handler(iso_cpp_handler);
}
#else
#define NEW_EXCEPT
#endif

// These hooks override the basic new/delete
// operators to use the iso_alloc API

EXTERNAL_API void *operator new(size_t size) NEW_EXCEPT {
    return iso_alloc(size);
}

EXTERNAL_API void operator delete(void *p) noexcept {
    iso_free(p);
}

EXTERNAL_API void *operator new[](size_t size) NEW_EXCEPT {
    return iso_alloc(size);
}

EXTERNAL_API void operator delete[](void *p) noexcept {
    iso_free(p);
}

EXTERNAL_API void *operator new(size_t size, const std::nothrow_t &) noexcept {
    return iso_alloc(size);
}

EXTERNAL_API void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    return iso_alloc(size);
}

void operator delete(void *ptr, size_t size) noexcept {
    return iso_free_size(ptr, size);
}

void operator delete[](void *ptr, size_t size) noexcept {
    return iso_free_size(ptr, size);
}

EXTERNAL_API void operator delete(void *ptr, const std::nothrow_t &) noexcept {
    return iso_free(ptr);
}

EXTERNAL_API void operator delete[](void *ptr, const std::nothrow_t &) noexcept {
    return iso_free(ptr);
}

#endif
#endif
