![](/misc/iso_alloc_logo.png?raw=true)

# Isolation Alloc

Isolation Alloc is a secure and fast(ish) memory allocator written in C. It's security strategy is partially inspired by Chrome's PartitionAlloc. A memory allocation isolation security strategy is best summed as keeping objects of different sizes or types separate from one another. The space afforded by a 64 bit process makes this possible, therefore Isolation Alloc does not support 32 bit targets.

## Design

IsoAlloc is for 64 bit Linux only. It may work in a 32 bit address space but it remains untested and the number of bits of entropy provided to mmap allocations is far too low in a 32 bit process to provide much security value. It may work on operating systems other than Linux but that is also untested at this time.

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

There is one iso_alloc_root structure which contains a fixed number of iso_alloc_zone structures. These iso_alloc_zone structures are referred to as zones. Zones manage user chunks and a bitmap that is used to manage those chunks. Both of these allocations are done separately, the zone only maintains pointers to them. These pointers are masked in between alloc and free operations. The bitmap contains 2 bits per user chunk. The current bit value specification is as follows:

* 00 free chunk
* 10 currently in use
* 01 was used but is now free<
* 11 canary chunk

 All user chunk pages and bitmap pages are surrounded by guard page allocations with the PROT_NONE permission. Zones are created for specific sizes, or manually created through the exposed API for a particular size or object type. Zones managed by isoalloc will live for the entire lifetime of the process, but zones created via the API can be destroyed.
