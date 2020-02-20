![](/misc/iso_alloc_logo.png?raw=true)

# Isolation Alloc

Isolation Alloc is a secure and fast(ish) memory allocator written in C. It's security strategy is partially inspired by Chrome's PartitionAlloc. A memory allocation isolation security strategy is best summed as keeping objects of different sizes or types separate from one another. The space afforded by a 64 bit process makes this possible, therefore Isolation Alloc does not support 32 bit targets.

## Design

Isolation Alloc is designed for 64 bit Linux only, although it does currently compile and run on Mac OS. It may work in a 32 bit address space but it remains untested and the number of bits of entropy provided to `mmap` based page allocations is far too low in a 32 bit process to provide much security value. It may work on operating systems other than Linux but that is also untested at this time.

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

There is one `iso_alloc_root` structure which contains a fixed number of `iso_alloc_zone` structures. These `iso_alloc_zone` structures are referred to as zones. Zones point to user chunks and a bitmap that is used to manage those chunks. Both of these allocations are done separately, the zone only maintains pointers to them. These pointers are masked in between alloc and free operations. The bitmap contains 2 bits per user chunk. The current bit value specification is as follows:

* 00 free chunk
* 10 currently in use
* 01 was used but is now free
* 11 canary chunk

 All user chunk pages and bitmap pages are surrounded by guard page allocations with the `PROT_NONE`permission. Zones are created for specific sizes, or manually created through the exposed API for a particular size or object type. Internally managed zones will live for the entire lifetime of the process, but zones created via the API can be destroyed at any time.

* All allocations are 8 byte aligned
* Zones are thread safe by default, to disable unset `DTHREAD_SUPPORT`
* The bitmap has 2 bits set aside per chunk
* All zones are 8 MB in size regardless of the chunk sizes they manage
* Default zones are created for sizes: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 bytes. Zones are created on demand for larger allocations
* The free bit slot cache is 255 entries, it helps speed up allocations

## Security Properties

* Zones cannot overflow or underflow into one another
* All user pages are surrounded by guard pages
* All bitmap pages are surrounded by guard pages
* Double free's are checked for on every call to `iso_free`
* For zones managing allocations <= 8192 bytes in size around %1 of their chunks are canaries
* The state of all zones can be verified at any anytime using `iso_verify_zones` or `iso_verify_zone(zone)`
* A reused chunk will always have its canary checked before its returned by `iso_alloc`
* A chunk can be permanently free'd with a call to `iso_free_permanently`
* All user chunk contents are cleared when passed to `iso_free` with the constant 0xDE
* When freeing a chunk the canary in adjacent chunks above/below are verified
* Some important zone metadata pointers are masked inbetween `iso_alloc` and `iso_free` operations
* Passing a pointer to `iso_free` that was not allocated with `iso_alloc` will abort

## API

```
void *iso_alloc(size_t size) - Equivalent to malloc. Returns a pointer to a chunk of memory that is size bytes in size. To free this chunk just pass it to iso_free.

void *iso_calloc(size_t nmemb, size_t size) - Equivalent to calloc. Allocates a chunk big enough for an array of nmemb elements of size bytes. The array is zeroized.

void *iso_realloc(void *p, size_t size) - Equivalent to realloc. Reallocates a new chunk, if necessary, to be size bytes big and copies the contents of p to it.

void iso_free(void *p) - Frees any chunk allocated and returned by any iso_alloc API (iso_alloc, iso_calloc, iso_realloc, iso_strdup, iso_strndup).

void iso_free_permanently(void *p) - Same as iso_free but marks the chunk in such a way that it will not be reallocated

size_t iso_chunksz(void *p) - Returns the size of the chunk returned by iso_alloc

char *iso_strdup(const char *str) - Equivalent to strdup. Returned pointer must be free'd by iso_free.

char *iso_strndup(const char *str, size_t n) - Equivalent to strndup. Returned pointer must be free'd by iso_free.

iso_alloc_zone_handle *iso_alloc_new_zone(size_t size) - Allocates a new private zone for allocations up to size bytes. Returns a handle to that zone.

char *iso_strdup_from_zone(iso_alloc_zone_handle *zone, const char *str) - Equivalent to iso_strdup except string is duplicated in specified zone.

char *iso_strndup_from_zone(iso_alloc_zone_handle *zone, const char *str, size_t n) - Equivalent to iso_strndup except string is duplicated in specified zone.

iso_alloc_zone_handle *iso_alloc_from_zone(iso_alloc_zone_handle *zone, size_t size) - Equivalent to iso_alloc except reallocation is done in specified zone.

iso_alloc_zone_handle *iso_realloc_from_zone(iso_alloc_zone_handle *zone, void *p, size_t size) - Equivalent to iso_realloc except reallocation is done in specified zone.

void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone) - Destroy a zone created with iso_alloc_from_zone.

void iso_alloc_protect_root() - Temporarily protects the iso_alloc root structure by marking it unreadable.

void iso_alloc_unprotect_root() - Undoes the operation performed by iso_alloc_protect_root.

int32_t iso_alloc_detect_leaks() - Returns the total number of leaks detected for all zones. Will print debug logs when compiled with `-DDEBUG`

int32_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone) - Returns the total number of leaks detected for specified zone. Will print debug logs when compiled with `-DDEBUG`

int32_t iso_alloc_mem_usage() - Returns the total memory usage for all zones. Will print debug logs when compiled with `-DDEBUG`

int32_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone) - Returns the total memory usage for a specified zone. Will print debug logs when compiled with `-DDEBUG`

void iso_verify_zones() - Verifies the state of all zones. Will abort if inconsistencies are found.

void iso_verify_zone(iso_alloc_zone_handle *zone) - Verifies the state of specified zone. Will abort if inconsistencies are found.
```
