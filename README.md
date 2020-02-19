![](/misc/iso_alloc_logo.png?raw=true)

# Isolation Alloc

Isolation Alloc is a secure and fast(ish) memory allocator written in C. It's security strategy is partially inspired by Chrome's PartitionAlloc. A memory allocation isolation security strategy is best summed as keeping objects of different sizes or types separate from one another. The space afforded by a 64 bit process makes this possible, therefore Isolation Alloc does not support 32 bit targets.

## Design

IsoAlloc is designed for 64 bit Linux only, although it does currently compile and run on Mac OS. It may work in a 32 bit address space but it remains untested and the number of bits of entropy provided to mmap allocations is far too low in a 32 bit process to provide much security value. It may work on operating systems other than Linux but that is also untested at this time.

```
      Contains Root structure
      |
      |      Meta data for Zones
      |      |                       Zone user data               Zone bitmap data
      |      |                       |                            |
      v      v                       v                            v
[GP][root [zone0..zoneN]][GP]..[GP][zone0 user chunks][GP]..[GP][zone0 bitmap][GP]
 ^
 |___ Guard page
```

There is one iso_alloc_root structure which contains a fixed number of iso_alloc_zone structures. These iso_alloc_zone structures are referred to as zones. Zones point to user chunks and a bitmap that is used to manage those chunks. Both of these allocations are done separately, the zone only maintains pointers to them. These pointers are masked in between alloc and free operations. The bitmap contains 2 bits per user chunk. The current bit value specification is as follows:

* 00 free chunk
* 10 currently in use
* 01 was used but is now free
* 11 canary chunk

 All user chunk pages and bitmap pages are surrounded by guard page allocations with the PROT_NONE permission. Zones are created for specific sizes, or manually created through the exposed API for a particular size or object type. Zones managed by isoalloc will live for the entire lifetime of the process, but zones created via the API can be destroyed at any time.

## Security Properties

* Zones cannot overflow or underflow into one another
* All user pages are surrounded by guard pages
* All bitmap pages are surrounded by guard pages
* Double free's are checked for on every call to free()
* All zones are created with around %1 of their chunks set as canaries
* The state of all zones, or a specific zone, can be verified at any anytime
* A reused chunk will always have its canary checked before its returned by iso_alloc()
* A chunk can be permanently free'd with a call to iso_free_permanently()
* All user chunk contents are cleared upon iso_free() with a constant 0xDE
* When freeing a chunk the canary in adjacent chunks above/below are verified

## The Details

 * All allocations are 8 byte aligned

