# Isolation Alloc Memory Tagging

IsoAlloc supports a software only memory tagging model that is conceptually very similar to Chromes [MTECheckedPtr](https://docs.google.com/document/d/1ph7iOorkGqTuETFZp-xvHV4L2rYootuz1ThzAAoGe30/edit?usp=sharing). This technique for pointer protection is inspired by ARM's upcoming Memory Tagging Extension (MTE) due in ARM v8.5-A. ARM MTE is a comprehensive hardware based solution for detecting memory safety issues in release builds of software with very little overhead. ARM MTE uses the Top Byte Ignore (TBI) feature to transparently tag pointers with metadata or a 'tag'. With ARM MTE this tag is mostly transparently checked and removed in hardware. The feature implemented here is conceptually very similar.

Note that this feature is experimental, off by default, and the APIs are subject to change!

## Overview

We can't achieve the granularity provided by ARM MTE in software alone but we can implement a pointer protection mechanism by generating 1 byte of meta data per chunk managed by a private IsoAlloc zone, adding that tag to the pointer, and verifying it before derefencing it. This feature is enabled or disabled with `MEMORY_TAGGING` in the `Makefile`.

This 1 byte tag will be added to the LSB of the pointer returned by calling `iso_alloc_from_zone_tagged`. IsoAlloc also provides a C API for tagging and untagging pointers retrieved by `iso_alloc_from_zone`. These functions are `iso_alloc_tag_ptr` and `iso_alloc_untag_ptr`.

Using these primivite operations we can build a simple C++ smart pointer that transparently tags, untags, and dereferences a tagged pointer.

```
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
```

These APIs can also be used in C, but this requires tagging and untagging pointers manually before using them.

## Implementation Details

* Currently only private zones can make use of memory tagging in IsoAlloc
* All tag data is stored below user pages with a guard page allocated in between
* A single 1 byte tag is generated per chunk in a private zone, this means the memory required to hold tags is larger for private zones holding smaller chunk sizes. For zones holding chunks 1024 byte or larger only a single of page of memory is required for tags as there are only 4096 possible 1024 byte chunks in zone user size of 4mb. The maximum amount of memory needed is for 16 byte chunks which requires 64 pages because there are 262144 possible chunks with a zone user size of 4mb.
* Tags are 1 byte in size and randomly chosen, they are added to the LSB of a pointer (e.g. tag value `0xed`, tagged pointer `0xed0b8066c1a000`, untagged pointer `0xb8066c1a000`)
* Tags are refreshed whenever the private zone has reached %25 of 'retirement age' (defined in conf.h as `ZONE_ALLOC_RETIRE`) with 0 current allocations

## Examples

The C API test can be found [tests/tagged_ptr_test.c](here)
The C++ smart pointer test can be found [tests/tagged_ptr_test.cpp](here)
