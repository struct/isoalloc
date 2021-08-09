# IsoAlloc Performance

## The Basics

Performance is a top priority for any memory allocator. Balancing those performance priorities with security is difficult. Anytime we introduce heavy handed security checks we usually have to pay a performance penalty. But we can be deliberate about our design choices and use good tooling to find places where we can mitigate those tradeoffs. In the end correct code is better than endless security mitigations.

Note: All performance testing, unless noted otherwise, was performed on Linux.

## Configuration and Optimizations

IsoAlloc is only designed and tested for 64 bit, so we don't have to worry about portability hurting our performance. We can assume a large address space will always be present and we can optimize for simple things like always fetching 64 bits of memory as we iterate over an array. This remainder of this section is a basic overview of the performance optimizations in IsoAlloc.

Perhaps the most important optimization in IsoAlloc is the design choice to use a simple bitmap for tracking chunk states. Combining this with zones comprised of contiguous chunks of pages results in good performance at the cost of memory. This is in contrast to typical allocator designs full of linked list code that tends to result far more complex code and slow page faults.

An important optimization is the freelist cache. This cache is just a per-zone array of free bit slots. The allocation hot path searches this cache first in the hopes that the zone has a free chunk available. Allocating chunks from this cache is very fast.

All data fetches from a zone bitmap are 64 bits at a time which takes advantage of fast CPU pipelining. Fetching bits at a different bit width will result in slower performance by an order of magnitude in allocation intensive tests. All user chunks are 8 byte aligned no matter how big each chunk is. Accessing this memory with proper alignment will minimize CPU cache flushes.

All bitmaps pages allocated with `mmap` are passed to the `madvise` syscall with the advice arguments `MADV_WILLNEED` and `MADV_SEQUENTIAL`. All user pages allocated with `mmap` are passed to the `madvise` syscall with the advice arguments `MADV_WILLNEED` and `MADV_RANDOM`. By default both of these mappings are created with `MAP_POPULATE` which instructs the kernel to pre-populate the page tables which reduces page faults and results in better performance. You can disable this with the `PRE_POPULATE_PAGES` Makefile flag. Note that at zone creation time user pages will have canaries written at random aligned offsets. This will cause page faults when the pages are first written to whether those pages are ever used at runtime or not. If we risk wasting memory we mine as well have the kernel pre-populate the page tables for us and increase the performance. The performance of short lived programs will benefit from `PRE_POPULATE_PAGES` being disabled.

Default zones for common sizes are created in the library constructor. This helps speed up allocations for long running programs. New zones are created on demand when needed but this will incur a small performance penalty in the allocation path.

By default user chunks are not sanitized upon free. While this helps mitigate uninitialized memory vulnerabilities it is a very slow operation. You can enable this feature by changing the `SANITIZE_CHUNKS` flag in the Makefile.

The meta data for the IsoAlloc root will be locked with `mlock`. This means this data will never be swapped to disk. We do this because using this data structure is required for both the alloc and free paths. This operation may fail if we are running inside a container with memory limits. Failure to lock the memory will not cause an abort and the error will be silently ignored in the initialization of the root structure.

If you know your program will not require multi-threaded access to IsoAlloc you can disable threading support by setting the `THREAD_SUPPORT` define to 0 in the Makefile. This will remove all atomic lock/unlock operations from the allocator, which will result in significant performance gains in some programs. If you do require thread support then you may want to profile your program to determine if `THREAD_ZONE_CACHE` will benefit your performance or harm it. The assumption this cache makes is that your threads will make similarly sized allocations. If this is unlikely then you can disable it in the `Makefile`.

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

iso_alloc/iso_free 1441616 tests completed in 0.418426 seconds
iso_calloc/iso_free 1441616 tests completed in 0.578068 seconds
iso_realloc/iso_free 1441616 tests completed in 0.681393 seconds

Running glibc malloc Performance Test

malloc/free 1441616 tests completed in 0.352161 seconds
calloc/free 1441616 tests completed in 0.562425 seconds
realloc/free 1441616 tests completed in 0.590622 seconds

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

barnes mimalloc 04.81 58660 4.77 0.03 0 15631
barnes smimalloc 04.80 58880 4.78 0.01 0 15649
barnes isoalloc 04.81 67144 4.77 0.04 0 17579
barnes tcmalloc 04.81 63636 4.78 0.03 0 16605
barnes jemalloc 04.85 60480 4.84 0.01 0 15741

mstressN mimalloc 04.24 645672 4.42 0.37 0 161319
mstressN smimalloc 04.28 649116 4.51 0.39 0 162183
mstressN isoalloc 04.23 673724 6.20 0.54 0 294751
mstressN tcmalloc 04.00 624892 4.33 0.26 0 158983
mstressN jemalloc 11.27 624880 5.47 6.87 0 4045738

espresso mimalloc 06.66 4640 6.63 0.02 0 800
espresso smimalloc 07.18 6732 7.15 0.02 0 1319
espresso isoalloc 10.13 48968 10.00 0.12 0 11976
espresso tcmalloc 06.55 8720 6.51 0.03 0 1528
espresso jemalloc 06.94 5348 6.93 0.01 0 652

redis mimalloc 8.666 29940 3.61 0.73 0 6709
redis smimalloc 9.317 31712 3.90 0.76 0 715
redis isoalloc 19.794 99904 9.01 0.92 0 24082
redis tcmalloc 8.661 38308 3.52 0.82 0 8502
redis jemalloc 9.470 32176 4.04 0.70 0 7100
```

IsoAlloc isn't quite ready for performance sensitive server workloads but it's more than fast enough for client side mobile/desktop applications with risky C/C++ attack surface.
