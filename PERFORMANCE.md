# IsoAlloc Performance

## The Basics

Performance is a top priority for any memory allocator. Balancing those performance priorities with security is difficult. Anytime we introduce heavy handed security checks we usually have to pay a performance penalty. But we can be deliberate about our design choices and use good tooling to find places where we can mitigate those tradeoffs. In the end correct code is better than endless security mitigations.

Note: All performance testing, unless noted otherwise, was performed on Linux.

## Configuration and Optimizations

IsoAlloc is only designed and tested for 64 bit, so we don't have to worry about portability hurting our performance. We can assume a large address space will always be present and we can optimize for simple things like always fetching 64 bits of memory as we iterate over an array. This remainder of this section is a basic overview of the performance optimizations in IsoAlloc.

Perhaps the most important optimization in IsoAlloc is the design choice to use a simple bitmap for tracking chunk states. Combining this with zones comprised of contiguous chunks of pages results in good performance at the cost of memory. This is in contrast to typical allocator designs full of linked list code that tends to result far more complex code and slow page faults.

An important optimization is the freelist cache. This cache is just a per-zone array of that threads most recently used zones. The allocation hot path searches this cache first in the hopes that one of those zones has a free chunk available that fits the allocation requests needs. Allocating chunks from this cache can be a lot faster than iterating through every zone for one that might fit. Unfortunately even the thread zone cache requires locking the root. So theres an additional caching layer that at the moment only stores a single free chunk per thread based on the zone it last made an allocation from. This provides a lockless allocation mechanism.

All data fetches from a zone bitmap are 64 bits at a time which takes advantage of fast CPU pipelining. Fetching bits at a different bit width will result in slower performance by an order of magnitude in allocation intensive tests. All user chunks are 8 byte aligned no matter how big each chunk is. Accessing this memory with proper alignment will minimize CPU cache flushes.

All bitmaps pages allocated with `mmap` are passed to the `madvise` syscall with the advice arguments `MADV_WILLNEED` and `MADV_SEQUENTIAL`. All user pages allocated with `mmap` are passed to the `madvise` syscall with the advice arguments `MADV_WILLNEED` and `MADV_RANDOM`. By default both of these mappings are created with `MAP_POPULATE` which instructs the kernel to pre-populate the page tables which reduces page faults and results in better performance. You can disable this with the `PRE_POPULATE_PAGES` Makefile flag. Note that at zone creation time user pages will have canaries written at random aligned offsets. This will cause page faults when the pages are first written to whether those pages are ever used at runtime or not. If we risk wasting memory we mine as well have the kernel pre-populate the page tables for us and increase the performance. The performance of short lived programs will benefit from `PRE_POPULATE_PAGES` being disabled.

Default zones for common sizes are created in the library constructor. This helps speed up allocations for long running programs. New zones are created on demand when needed but this will incur a small performance penalty in the allocation path.

By default user chunks are not sanitized upon free. While this helps mitigate uninitialized memory vulnerabilities it is a very slow operation. You can enable this feature by changing the `SANITIZE_CHUNKS` flag in the Makefile.

The meta data for the IsoAlloc root will be locked with `mlock`. This means this data will never be swapped to disk. We do this because using this data structure is required for both the alloc and free paths. This operation may fail if we are running inside a container with memory limits. Failure to lock the memory will not cause an abort and the error will be silently ignored in the initialization of the root structure.

If you know your program will not require multi-threaded access to IsoAlloc you can disable threading support by setting the `THREAD_SUPPORT` define to 0 in the Makefile. This will remove all atomic lock/unlock operations from the allocator, which will result in significant performance gains in some programs. If you do require thread support then you may want to profile your program to determine if `THREAD_CACHE` will benefit your performance or harm it. The assumption this cache makes is that your threads will make similarly sized allocations. If this is unlikely then you can disable it in the `Makefile`.

`DISABLE_CANARY` can be set to 1 to disable the creation and verification of canary chunks. This removes a useful security feature but will significantly improve performance.

## Tests

I've spent a good amount of time testing IsoAlloc to ensure its reasonably fast compared to glibc/ptmalloc. But it is impossible for me to model or simulate all the different states a program that uses IsoAlloc may be in. This section briefly covers the existing performance related tests for IsoAlloc and the data I have collected so far.

The `malloc_cmp_test` build target will build 2 different versions of the test/tests.c program which runs roughly 1.4 million alloc/calloc/realloc operations. We free every other allocation and then free the remaining ones once the allocation loop is complete. This helps simulate some fragmentation in the heap. On average IsoAlloc is faster than ptmalloc but the difference is so close it will likely not be noticable.

The following test was run in an Ubuntu 20.04 for ARM64 docker container with libc version 2.31-0ubuntu9.2 on a MacOS host. The kernel used was `Linux 8772e39a0c20 5.10.25-linuxkit`.

```
Running IsoAlloc Performance Test

iso_alloc/iso_free 1441616 tests completed in 0.146020 seconds
iso_calloc/iso_free 1441616 tests completed in 0.174673 seconds
iso_realloc/iso_free 1441616 tests completed in 0.249192 seconds

Running glibc/ptmalloc Performance Test

malloc/free 1441616 tests completed in 0.171125 seconds
calloc/free 1441616 tests completed in 0.228953 seconds
realloc/free 1441616 tests completed in 0.317215 seconds

Running jemalloc Performance Test

malloc/free 1441616 tests completed in 0.059686 seconds
calloc/free 1441616 tests completed in 0.188510 seconds
realloc/free 1441616 tests completed in 0.190125 seconds

Running mimalloc Performance Test

malloc/free 1441616 tests completed in 0.091053 seconds
calloc/free 1441616 tests completed in 0.118271 seconds
realloc/free 1441616 tests completed in 0.154681 seconds

Running mimalloc-secure Performance Test

malloc/free 1441616 tests completed in 0.136460 seconds
calloc/free 1441616 tests completed in 0.163846 seconds
realloc/free 1441616 tests completed in 0.193132 seconds

Running tcmalloc Performance Test

malloc/free 1441616 tests completed in 0.108659 seconds
calloc/free 1441616 tests completed in 0.124787 seconds
realloc/free 1441616 tests completed in 0.141458 seconds

```

The same test run on an AWS t2.xlarge Ubuntu 20.04 instance with 4 `Intel(R) Xeon(R) CPU E5-2676 v3 @ 2.40GHz` CPUs and 16 gb of memory:

```
Running IsoAlloc Performance Test

iso_alloc/iso_free 1441616 tests completed in 0.147336 seconds
iso_calloc/iso_free 1441616 tests completed in 0.161482 seconds
iso_realloc/iso_free 1441616 tests completed in 0.244981 seconds

Running glibc malloc Performance Test

malloc/free 1441616 tests completed in 0.182437 seconds
calloc/free 1441616 tests completed in 0.246065 seconds
realloc/free 1441616 tests completed in 0.332292 seconds
```

Here is the same test as above on Mac OS 11.6

```
Running IsoAlloc Performance Test

iso_alloc/iso_free 1441616 tests completed in 0.124150 seconds
iso_calloc/iso_free 1441616 tests completed in 0.182955 seconds
iso_realloc/iso_free 1441616 tests completed in 0.275084 seconds

Running system malloc Performance Test

malloc/free 1441616 tests completed in 0.090845 seconds
calloc/free 1441616 tests completed in 0.200397 seconds
realloc/free 1441616 tests completed in 0.254574 seconds
```

This same test can be used with the `perf` utility to measure basic stats like page faults and CPU utilization using both heap implementations. The output below is on the same AWS t2.xlarge instance as above.

```
$ perf stat build/tests

iso_alloc/iso_free 1441616 tests completed in 0.416603 seconds
iso_calloc/iso_free 1441616 tests completed in 0.575822 seconds
iso_realloc/iso_free 1441616 tests completed in 0.679546 seconds

 Performance counter stats for 'build/tests':

           1709.07 msec task-clock                #    1.000 CPUs utilized
                 7      context-switches          #    0.004 K/sec
                 0      cpu-migrations            #    0.000 K/sec
            145562      page-faults               #    0.085 M/sec

       1.709414837 seconds time elapsed

       1.405068000 seconds user
       0.304239000 seconds sys

$ perf stat build/malloc_tests

malloc/free 1441616 tests completed in 0.359380 seconds
calloc/free 1441616 tests completed in 0.569044 seconds
realloc/free 1441616 tests completed in 0.597936 seconds

 Performance counter stats for 'build/malloc_tests':

           1528.51 msec task-clock                #    1.000 CPUs utilized
                 5      context-switches          #    0.003 K/sec
                 0      cpu-migrations            #    0.000 K/sec
            433055      page-faults               #    0.283 M/sec

       1.528795324 seconds time elapsed

       0.724352000 seconds user
       0.804371000 seconds sys

```

The following benchmarks were collected from [mimalloc-bench](https://github.com/daanx/mimalloc-bench) with the default configuration of IsoAlloc. As you can see from the data IsoAlloc is competitive with jemalloc, tcmalloc, and glibc/ptmalloc for some benchmarks but clearly falls behind in the Redis benchmark. For any benchmark that IsoAlloc scores poorly on I was able to tweak its build to improve the CPU time and memory consumption. It's worth noting that IsoAlloc was able to stay competitive even with performing many security checks not present in other allocators.

```
# benchmark allocator elapsed rss user sys page-faults page-reclaims

barnes isoalloc 02.00 65476 1.97 0.02 3 18495
barnes mimalloc 01.95 57836 1.93 0.01 0 16552
barnes tcmalloc 01.95 62612 1.93 0.01 0 17516
barnes jemalloc 01.94 59480 1.92 0.01 0 16649

cache-scratch1 isoalloc 01.28 10736 1.27 0.00 1 2158
cache-scratch1 mimalloc 01.27 3180 1.27 0.00 0 200
cache-scratch1 tcmalloc 01.27 6876 1.27 0.00 0 1129
cache-scratch1 jemalloc 01.27 3460 1.27 0.00 0 236

cache-scratchN isoalloc 00.37 11136 1.45 0.00 0 2265
cache-scratchN mimalloc 00.36 3468 1.44 0.00 0 230
cache-scratchN tcmalloc 01.87 6976 7.44 0.00 0 1142
cache-scratchN jemalloc 01.86 3748 7.41 0.00 0 283

cache-thrash1 isoalloc 01.34 10856 1.31 0.01 1 2162
cache-thrash1 mimalloc 01.27 3380 1.27 0.00 0 206
cache-thrash1 tcmalloc 01.27 6868 1.26 0.00 0 1127
cache-thrash1 jemalloc 01.27 3648 1.27 0.00 0 240

cache-thrashN isoalloc 01.00 10784 3.82 0.00 0 2174
cache-thrashN mimalloc 00.36 3356 1.44 0.00 0 229
cache-thrashN tcmalloc 01.87 6880 7.42 0.00 0 1138
cache-thrashN jemalloc 00.37 3760 1.46 0.00 0 296

redis isoalloc 8.669 76240 4.07 0.30 1 21473 ops/sec: 230702.66, relative time: 8.669s
redis mimalloc 4.555 28968 2.13 0.17 4 6655 ops/sec: 439023.69, relative time: 4.555s
redis tcmalloc 4.715 37120 2.21 0.17 3 8446 ops/sec: 424108.56, relative time: 4.715s
redis jemalloc 5.125 30836 2.41 0.17 0 7034 ops/sec: 390174.03, relative time: 5.125s
```

IsoAlloc isn't quite ready for performance sensitive server workloads but it's more than fast enough for client side mobile/desktop applications with risky C/C++ attack surface.
