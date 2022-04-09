![](/misc/iso_alloc_logo.png?raw=true)

![unit tests](https://github.com/struct/isoalloc/actions/workflows/testsuite.yml/badge.svg)

# Isolation Alloc

Isolation Alloc (or IsoAlloc) is a secure and fast(ish) memory allocator written in C. It is a drop in replacement for `malloc` on Linux / Mac OS using `LD_PRELOAD` or `DYLD_INSERT_LIBRARIES` respectively. Its security strategy is partially inspired by Chrome's [PartitionAlloc](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/base/allocator/partition_allocator/PartitionAlloc.md). A memory allocation isolation security strategy is best summed up as maintaining spatial separation, or isolation between objects of different sizes or types. While IsoAlloc wraps `malloc` and enforces naive isolation by default very strict  isolation of allocations can be achieved using the APIs directly.

Isolation Alloc is designed and [tested](https://github.com/struct/isoalloc/actions) for 64 bit Linux and MacOS. The space afforded by a 64 bit process makes this possible, therefore Isolation Alloc does not support 32 bit targets. The number of bits of entropy provided to `mmap` based page allocations is far too low in a 32 bit process to provide much security value. You can compile and run it on a 32 bit target but it remains untested. It may work on operating systems other than Linux/MacOS but that is also untested at this time.

Additional information about the allocator and some of its design choices can be found [here](http://struct.github.io/iso_alloc.html). However this README and the documentation in this repository will always be more up to date and authoritative.

## Design

You can think of Isolation Alloc as a [region based](https://en.wikipedia.org/wiki/Region-based_memory_management) memory allocator. If you are familiar with the implementation of arenas in other allocators then the concepts here will be familiar to you.

![Design of isoalloc schema](/misc/isoalloc_design.svg)

There is one `iso_alloc_root` structure which contains a pointer to a fixed number of `iso_alloc_zone` structures. These `iso_alloc_zone` structures are referred to as *zones*. Zones point to user chunks and a bitmap that is used to manage those chunks. The translation between bitmap and user chunks is referred to as *bit slots*. The pages that back both the user chunks and the bitmap are allocated separately. The pointers that reference these in the zone meta data are masked in between allocation and free operations. The bitmap contains 2 bits of state per user chunk. The current bit value specification is as follows:

* `00` free chunk
* `10` currently in use
* `01` was used but is now free
* `11` canary chunk

All user chunk pages and bitmap pages are surrounded by guard page allocations with the `PROT_NONE` permission. Zones are created for specific sizes, or manually created through the exposed API for a particular size or object type. Internally managed zones will live for the entire lifetime of the process, but zones created via the API can be destroyed at any time.

If `DEBUG`, `LEAK_DETECTOR`, or `MEM_USAGE` are specified during compilation a memory leak and memory usage routine will be called from the destructor which will print useful information about the state of the heap at that time. These can also be invoked via the API, which is documented further below.

* All allocations are 8 byte aligned.
* The `iso_alloc_root` structure is thread safe and guarded by a mutex or spinlock when `THREAD_SUPPORT` is enabled.
* Each zone bitmap contains 2 bits per chunk.
* All zones are 4 MB in size regardless of the chunk sizes they manage.
* Default zones are created in the constructor for sizes: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 bytes.
* Zones are created on demand for larger allocations or when these default zones are exhausted.
* The free bit slot cache is 255 entries, it helps speed up allocations.
* All allocations larger than 131072 bytes live in specially handled big zones which has a size limitation of 4 GB.

There is support for Address Sanitizer, Memory Sanitizer, and Undefined Behavior Sanitizer. If you want to enable it just uncomment the `ENABLE_ASAN`, `ENABLE_MSAN`, or `ENABLE_UBSAN` flags in the `Makefile`. Like any other usage of Address Sanitizer these are mutually exclusive. IsoAlloc will use Address Sanitizer macros to poison and unpoison user chunks appropriately. IsoAlloc still catches a number of issues Address Sanitizer does not, including double/unaligned/wild free's.

A feature similar to [GWP-ASAN](https://www.chromium.org/Home/chromium-security/articles/gwp-asan) can be enabled with `ALLOC_SANITY` in the Makefile. It samples calls to `iso_alloc/malloc` and allocates a page of memory surrounded by guard pages in order to detect Use-After-Free and linear heap overflows. All sampled sanity allocations are verified with canaries to detect over/underflows into the surrounding bytes of the page. A percentage of sanity allocations are allocated at end of the page to detect linear overflows. This feature works on all supported platforms.

You can also enable `UNINIT_READ_SANITY` for detecting uninitialized read vulnerabilities using the `userfaultfd` syscall. You can read more about that feature [here](https://struct.github.io/isoalloc_uninit_read.html). This feature is only available on Linux and requires `ALLOC_SANITY` and `THREAD_SUPPORT` to be enabled.

See the [PERFORMANCE](PERFORMANCE.md) documentation for more information.

## Thread Safety

IsoAlloc is thread safe by way of protecting the root structure with a global lock built with either a pthread mutex, or a C11 `atomic_flag` when `USE_SPINLOCK` is enabled. This means every thread that wants to allocate or free a chunk needs to wait until it can take ownership of the lock. This design choice has some tradeoffs. It can negatively impact performance of multi threaded programs that perform a lot of allocations. This is because every thread shares the same set of global zones. The benefit of this is that you can allocate and free any chunk from any thread with no additional complexity required. In order to help alleviate contention on this lock each thread has a zone cache built using thread local storage (TLS). This is implemented as a simple FILO cache of the most recently used zones by that thread. It's size is 8 by default but can be increased modifying the `ZONE_CACHE_SZ` define in the internal header file. Making this cache too large can lead to negative performance implications for certain allocation patterns. For example, if a thread allocates multiple 32 byte chunks in a row then the cache may be populated entirely by the same zone that holds 32 byte chunks. Now when the thread goes to allocate a 64 byte chunk it iterates through the entire cache, does not find a usable zone, and then has to take the slow path which iterates through all zones again. This cache is also used when thread support is disabled but it does not live in TLS and is instead allocated on its own set of pages. See the [PERFORMANCE](PERFORMANCE.md) documentation for more information on the various caches in use in IsoAlloc.

When enabled, the `CPU_PIN` feature will restrict allocations from a given zone to the CPU core that created that zone. Free operations are not restricted in this way. This mode is compatible with and without thread support, but is only available on Linux, and will introduce a slight performance hit to the hot path and may increase memory usage. The benefit of this mode is that it introduces an isolation mechanism based on CPU core with no configuration beyond enabling the `CPU_PIN` define in the Makefile.

## Security Properties

* Zones cannot overflow or underflow into one another.
* All user pages are surrounded by guard pages including big zones.
* All bitmap pages are surrounded by guard pages.
* Double free's are checked for on every call to `iso_free`.
* For zones managing allocations 8192 bytes or smaller around %1 of their chunks are permanent canaries.
* All free'd chunks get a canary written to them and verified upon reallocation.
* The state of all zones can be verified at any anytime using `iso_verify_zones` or `iso_verify_zone(zone)`.
* Canaries are unique and are composed of a 64 bit secret value xor'd by the address of the chunk itself.
* A reused chunk will always have its canary checked before its returned by `iso_alloc`.
* The top byte of user chunk canaries is `0x00` to prevent unbounded C string reads from leaking it.
* A chunk can be permanently free'd with a call to `iso_free_permanently`.
* If `SANITIZE_CHUNKS` is set all user chunks are cleared when passed to `iso_free` with the constant 0xDE.
* When freeing a chunk the canary in adjacent chunks above/below are verified.
* Some important zone metadata pointers are masked in-between `iso_alloc` and `iso_free` operations.
* Passing a pointer to `iso_free` that was not allocated with `iso_alloc` will abort.
* Pointers passed to `iso_free` must be 8 byte aligned, and a multiple of the zone chunk size.
* The free bit slot cache provides a chunk quarantine or delayed free mechanism.
* When private zones are destroyed they are overwritten and marked `PROT_NONE` to prevent use-after-free.
* Big zone meta data lives at a random offset from its base page.
* A call to `realloc` will always return a new chunk. Use `PERM_FREE_REALLOC` to make these free's permanent.
* Enable `FUZZ_MODE` in the Makefile to verify all zones upon alloc/free, and never reuse private zones.
* When `CPU_PIN` is enabled allocation from a zone will be restricted to the CPU core that created it.
* When `UAF_PTR_PAGE` is enabled calls to `iso_free` will be sampled to search for dangling references.
* Enable `VERIFY_BIT_SLOT_CACHE` to verify there are no duplicates in the bit slot cache upon free.
* When `ALLOC_SANITY` is enabled a percentage of allocations will be sampled to detect UAF/overflows, see above.
* Randomized hints are passed to `mmap` to ensure contiguous page ranges are not allocated.
* When `ABORT_ON_NULL` is enabled IsoAlloc will abort instead of returning `NULL`.
* By default `NO_ZERO_ALLOCATIONS` will return a pointer to a page marked `PROT_NONE` for all `0` sized allocations.
* When `ABORT_NO_ENTROPY` is enabled IsoAlloc will abort when it can't gather enough entropy.
* When `SHUFFLE_BIT_SLOT_CACHE` is enabled IsoAlloc will shuffle the bit slot cache upon creation (3-4x perf hit)
* When destroying private zones if `NEVER_REUSE_ZONES` is enabled IsoAlloc won't attempt to repurpose the zone
* Zones are retired and replaced after they've allocated and freed a specific number of chunks. This is calculated as `ZONE_ALLOC_RETIRE * max_chunk_count_for_zone`.

## Building

The Makefile targets are very simple:

`make library` - Builds a release version of the library without C++ support

`make library_debug` - Builds a debug version of the library

`make library_debug_no_output` - Builds a debug version of the library with no logging output

`make analyze_library_debug` - Builds the library with clang's scan-build if installed

`make tests` - Builds and runs all tests

`make perf_tests` - Builds and runs a simple performance test that uses gprof. Linux only

`make malloc_cmp_test` - Builds and runs a test that uses both iso_alloc and malloc for comparison

`make c_library_objects` - Builds .o files to be linked in another compilation step

`make c_library_objects_debug` - Builds debug .o files to be linked in another compilation step

`make cpp_library` - Builds the library with a C++ interface that overloads operators `new` and `delete`

`make cpp_library_debug` - Builds a debug version of the library with a C++ interface that overloads operators `new` and `delete`

`make cpp_tests` - Builds and runs the C++ tests

`make format` - Runs clang formatter according to the specification in .clang-format

`make clean` - Cleans up the root directory

## Android

To build Android libraries for x86_64 and ARM64 architectures just `cd` into the `android/jni` directory and run `ndk-build`.

For those of you on an M1 based Mac you can still build IsoAlloc with the following command:
`arch -x86_64 /bin/bash -c $ANDROID_NDK_HOME/build/ndk-build`

## Linking With C++

If you want to use IsoAlloc with a C++ program you can use the `c_library_objects` Makefile target. This will produce .o object files you can pass to your compiler. These targets are used internally to build the library with `new` and `delete` support.

## Debugging

If you try to use Isolation Alloc in an existing program then and you are getting crashes here are some tips to help you get started. If you aren't using `LD_PRELOAD` then first make sure you actually replaced all `malloc, calloc, realloc` and `free` calls to their `iso_alloc` equivalents. Don't forget things like `strdup` that return a pointer from `malloc`.

If you are getting consistent crashes you can build a debug version of the library with `make library_debug` and then catch the crash in GDB with a command similar to this `gdb -q -command=misc/commands.gdb <your_binary>`.

If all else fails please file an issue on the [github project](https://github.com/struct/isoalloc/issues) page.

## API

`void *iso_alloc(size_t size)` - Equivalent to `malloc`. Returns a pointer to a chunk of memory that is size bytes in size. To free this chunk just pass it to `iso_free`.

`void *iso_calloc(size_t nmemb, size_t size)` - Equivalent to `calloc`. Allocates a chunk big enough for an array of nmemb elements of size bytes. The array is zeroized.

`void *iso_realloc(void *p, size_t size)` - Equivalent to `realloc`. Reallocates a new chunk, if necessary, to be size bytes big and copies the contents of p to it.

`void iso_free(void *p)` - Frees any chunk allocated and returned by any API call (e.g. `iso_alloc, iso_calloc, iso_realloc, iso_strdup, iso_strndup`).

`void iso_free_size(void *p, size_t size)` - The same as `iso_free` but requires a size argument so a strict size check can be performed

`void iso_free_permanently(void *p)` - Same as `iso_free` but marks the chunk in such a way that it will not be reallocated

`void iso_free_from_zone(void *p, iso_alloc_zone_handle *zone)` - Free's a chunk from a private zone

`void iso_free_from_zone_permanently(void *p, iso_alloc_zone_handle *zone)` - Permanently free's a chunk from a zone

`size_t iso_chunksz(void *p)` - Returns the size of the chunk returned by `iso_alloc`

`char *iso_strdup(const char *str)` - Equivalent to `strdup`. Returned pointer must be free'd by `iso_free`.

`char *iso_strndup(const char *str, size_t n)` - Equivalent to `strndup`. Returned pointer must be free'd by iso_free.

`iso_alloc_zone_handle *iso_alloc_new_zone(size_t size)` - Allocates a new private zone for allocations up to size bytes. Returns a handle to that zone.

`char *iso_strdup_from_zone(iso_alloc_zone_handle *zone, const char *str)` - Equivalent to `iso_strdup` except string is duplicated in specified zone.

`char *iso_strndup_from_zone(iso_alloc_zone_handle *zone, const char *str, size_t n)` - Equivalent to `iso_strndup` except string is duplicated in specified zone.

`void *iso_alloc_from_zone(iso_alloc_zone_handle *zone, size_t size)` - Equivalent to `iso_alloc` except allocation is done in specified zone.

`void iso_alloc_destroy_zone(iso_alloc_zone_handle *zone)` - Destroy a zone created with `iso_alloc_from_zone`.

`void iso_alloc_protect_root()` - Temporarily protects the `iso_alloc` root structure by marking it unreadable.

`void iso_alloc_unprotect_root()` - Undoes the operation performed by `iso_alloc_protect_root`.

`uint64_t iso_alloc_detect_leaks()` - Returns the total number of leaks detected for all zones. Will print debug logs when compiled with `-DDEBUG`

`uint64_t iso_alloc_detect_zone_leaks(iso_alloc_zone_handle *zone)` - Returns the total number of leaks detected for specified zone. Will print debug logs when compiled with `-DDEBUG`

`uint64_t iso_alloc_mem_usage()` - Returns the total memory usage for all zones. Will print debug logs when compiled with `-DDEBUG`

`uint64_t iso_alloc_zone_mem_usage(iso_alloc_zone_handle *zone)` - Returns the total memory usage for a specified zone. Will print debug logs when compiled with `-DDEBUG`

`void iso_verify_zones()` - Verifies the state of all zones. Will abort if inconsistencies are found.

`void iso_verify_zone(iso_alloc_zone_handle *zone)` - Verifies the state of specified zone. Will abort if inconsistencies are found.

`int32_t iso_alloc_name_zone(iso_alloc_zone_handle *zone, char *name)` - Allows naming of private zones via prctl on Android

`void iso_flush_caches()` - Flushes all thread specific caches. Intended to be used upon thread destruction

### Experimental APIs

These APIs are exposed via the public header `iso_alloc.h` but are subject to backward breaking changes at any time.

`int32_t iso_get_alloc_traces(iso_alloc_traces_t *traces_out)` - Retrieves the current global `iso_alloc_traces_t` structure from the allocator

`int32_t iso_get_free_traces(iso_free_traces_t *traces_out)` - Retrieves the current global `iso_free_traces_t` structure from the allocator

`void iso_alloc_search_stack(void *p)` - Searches from `p` until the current stack frame in `iso_alloc_search_stack` for any pointers into IsoAlloc user pages. Any pointers found are logged to stdout. If `p` is `NULL` then the entire stack is searched.

### Data Structures

```
typedef struct {
    /* The address of the last ALLOC_BTS_DEPTH (8) callers as referenced by stack frames */
    uint64_t callers[ALLOC_BTS_DEPTH];
    /* The smallest allocation size requested by this call path */
    size_t lower_bound_size;
    /* The largest allocation size requested by this call path */
    size_t upper_bound_size;
    /* A 16 bit hash of the back trace */
    uint16_t backtrace_hash;
} iso_alloc_traces_t;

typedef struct {
    /* The address of the last 8 callers as referenced by stack frames */
    uint64_t callers[BACKTRACE_DEPTH];
    /* A 16 bit hash of the back trace */
    uint16_t backtrace_hash;
    /* Call count */
    size_t call_count;
} iso_free_traces_t;
```

When `HEAP_PROFILER` is enabled these structure will contain information collected by the allocator by sampling `malloc` and `free` calls. This data structure is experimental and is subject to change. See `interfaces_test.c` file for an example of how to retrieve and inspect these structures.
