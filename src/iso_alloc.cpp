// iso_alloc.cpp - A secure memory allocator
// Copyright 2020 - chris.rohlf@gmail.com

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

#if CPP_SUPPORT
#if MALLOC_HOOK

#include <new>

// These hooks override the basic new/delete
// operators to use the iso_alloc API

EXTERNAL_API void *operator new(size_t size) {
    return iso_alloc(size);
}

EXTERNAL_API void operator delete(void *p) noexcept {
    iso_free(p);
    return;
}

EXTERNAL_API void *operator new[](size_t size) {
    return iso_alloc(size);
}

EXTERNAL_API void operator delete[](void *p) noexcept {
    iso_free(p);
    return;
}

EXTERNAL_API void *operator new(size_t size, const std::nothrow_t &) noexcept {
    return iso_alloc(size);
}

EXTERNAL_API void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    return iso_alloc(size);
}

EXTERNAL_API void operator delete(void *ptr, const std::nothrow_t &) noexcept {
    return iso_free(ptr);
}

EXTERNAL_API void operator delete[](void *ptr, const std::nothrow_t &) noexcept {
    return iso_free(ptr);
}

#endif
#endif
