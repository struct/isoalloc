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

The meta data for all default zones will be locked with `mlock`. This means this data will never be swapped to disk. We do this because iterating over these data structures is required for both the alloc and free paths. This operation may fail if we are running inside a container with memory limits. Failure to lock the memory will not cause an abort and the error will be silently ignored in the initialization of the root structure. Zones that are created on demand after initialization will not have their memory locked.

If you know your program will not require multi-threaded access to IsoAlloc you can disable threading support by setting the `THREAD_SUPPORT` define to 0 in the Makefile. This will remove all atomic lock/unlock operations from the allocator, which will speed things up substantially in some programs. If you do require thread support then you may want to profile your program to determine if `THREAD_ZONE_CACHE` will benefit your performance or harm it. The assumption this cache makes is that your threads will make similarly sized allocations. If this is unlikely then you can disable it in the Makefile.

`DISABLE_CANARY` can be set to 1 to disable the creation and verification of canary chunks. This removes a useful security feature but will significantly improve performance.

## Tests

I've spent a good amount of time testing IsoAlloc to ensure its reasonably fast compared to glibc/ptmalloc. But it is impossible for me to model or simulate all the different states a program that uses IsoAlloc may be in. This section briefly covers the existing performance related tests for IsoAlloc and the data I have collected so far.

The `malloc_cmp_test` build target will build 2 different versions of the test/tests.c program which runs roughly 1.4 million alloc/calloc/realloc operations. We free every other allocation and then free the remaining ones once the allocation loop is complete. This helps simulate some fragmentation in the heap. On average IsoAlloc is faster than ptmalloc but the difference is so close it will likely not be noticable.

```
iso_alloc/iso_free 1441616 tests completed in 0.252341 seconds
iso_calloc/iso_free 1441616 tests completed in 0.308071 seconds
iso_realloc/iso_free 1441616 tests completed in 0.444568 seconds

malloc/free 1441616 tests completed in 0.325772 seconds
calloc/free 1441616 tests completed in 0.408884 seconds
realloc/free 1441616 tests completed in 0.445580 seconds
```
This same test can be used with the `perf` utility to measure basic stats like page faults and CPU utilization using both heap implementations.

```
$ perf stat build/tests
iso_alloc/iso_free 1441616 tests completed in 0.368204 seconds
iso_calloc/iso_free 1441616 tests completed in 0.533650 seconds
iso_realloc/iso_free 1441616 tests completed in 0.856406 seconds

 Performance counter stats for 'build/tests':

       1823.204720      task-clock:u (msec)       #    0.986 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
               182      page-faults:u             #    0.100 K/sec                  

       1.849822214 seconds time elapsed


$ perf stat build/malloc_tests 
malloc/free 1441616 tests completed in 0.375495 seconds
calloc/free 1441616 tests completed in 0.765150 seconds
realloc/free 1441616 tests completed in 0.832934 seconds

 Performance counter stats for 'build/malloc_tests':

       2016.896435      task-clock:u (msec)       #    0.999 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
           530,386      page-faults:u             #    0.263 M/sec                  

       2.018058123 seconds time elapsed
```

The test above is on a free tier Amazon t2.micro instance with 1 CPU and 1 Gb of RAM. IsoAlloc just barely beats ptmalloc here. Below are the results from an Amazon c5n.large instance with 2 CPUs and 4Gb of memory. The IsoAlloc configuration had `PRE_POPULATE_PAGES=1` which reduces page faults.

```
$ sudo perf stat build/tests
iso_alloc/iso_free 1441616 tests completed in 0.302376 seconds
iso_calloc/iso_free 1441616 tests completed in 0.448824 seconds
iso_realloc/iso_free 1441616 tests completed in 0.455860 seconds

 Performance counter stats for 'build/tests':

       1249.500900      task-clock (msec)         #    0.999 CPUs utilized          
                 2      context-switches          #    0.002 K/sec                  
                 0      cpu-migrations            #    0.000 K/sec                  
               191      page-faults               #    0.153 K/sec                  

       1.251236091 seconds time elapsed

$ sudo perf stat build/malloc_tests 
malloc/free 1441616 tests completed in 0.327699 seconds
calloc/free 1441616 tests completed in 0.455887 seconds
realloc/free 1441616 tests completed in 0.503063 seconds

 Performance counter stats for 'build/malloc_tests':

       1319.741125      task-clock (msec)         #    0.999 CPUs utilized          
                 3      context-switches          #    0.002 K/sec                  
                 0      cpu-migrations            #    0.000 K/sec                  
            416735      page-faults               #    0.316 M/sec                  

       1.320819529 seconds time elapsed
```

IsoAlloc seems to outperform ptmalloc in these artificial benchmarks.


The following benchmarks were collected from [mimalloc-bench](https://github.com/daanx/mimalloc-bench) with the default configuration of IsoAlloc. As you can see from the data IsoAlloc is competitive with jemalloc, tcmalloc, and glibc/ptmalloc for most benchmarks but clearly falls behind in the Redis and cfrac benchmarks. For any benchmark that IsoAlloc scores poorly on I was able to tweak its build to improve the CPU time and memory consumption. Its worth noting that IsoAlloc was able to stay competitive even with performing numerous security checks not present in other allocators.

```
espresso tcmalloc 04.94 8668 4.91 0.02 0 1524
espresso jemalloc 05.19 5040 5.18 0.01 0 678
espresso isoalloc 06.80 91344 6.65 0.14 0 23240
espresso ptmalloc 05.63 2328 5.62 0.01 0 460

barnes tcmalloc 03.10 63672 3.07 0.02 0 16603
barnes jemalloc 03.17 60360 3.15 0.02 0 15738
barnes isoalloc 03.14 74804 3.09 0.05 0 19507
barnes ptmalloc 03.09 58492 3.07 0.02 0 15605

alloc-test1 tcmalloc 03.81 16212 3.79 0.01 1 3359
alloc-test1 jemalloc 04.12 12180 4.10 0.01 0 2500
alloc-test1 isoalloc 07.65 64460 7.46 0.19 0 15727
alloc-test1 ptmalloc 04.74 13336 4.73 0.00 0 2932

cache-thrash1 tcmalloc 01.67 7604 1.66 0.00 0 1171
cache-thrash1 jemalloc 01.67 4116 1.67 0.00 0 288
cache-thrash1 isoalloc 01.68 19272 1.68 0.00 0 4094
cache-thrash1 ptmalloc 01.67 3476 1.66 0.00 0 217

cache-thrashN tcmalloc 04.03 7588 28.27 0.00 0 1190
cache-thrashN jemalloc 00.42 4928 3.34 0.00 0 420
cache-thrashN isoalloc 02.14 19336 16.85 0.01 0 4106
cache-thrashN ptmalloc 00.42 3876 3.31 0.00 0 241

cache-scratch1 tcmalloc 01.67 7532 1.67 0.00 0 1167
cache-scratch1 jemalloc 01.67 4236 1.67 0.00 0 287
cache-scratch1 isoalloc 01.68 19256 1.67 0.01 0 4079
cache-scratch1 ptmalloc 01.67 3628 1.67 0.00 0 217

cache-scratchN tcmalloc 04.51 7716 30.21 0.00 0 1194
cache-scratchN jemalloc 04.40 4504 31.06 0.00 0 391
cache-scratchN isoalloc 02.16 19296 17.00 0.00 0 4096
cache-scratchN ptmalloc 02.06 3784 9.59 0.00 0 241

cfrac tcmalloc 06.40 8192 6.39 0.00 2 1370
cfrac jemalloc 06.65 4708 6.65 0.00 0 476
cfrac isoalloc 10.89 25400 10.67 0.21 0 5744
cfrac ptmalloc 06.82 3136 6.82 0.00 0 426

redis tcmalloc 4.409 37896 1.93 0.27 0 8479
redis jemalloc 5.097 31772 2.33 0.22 0 7082
redis isoalloc 13.020 104688 6.11 0.43 0 25329
redis ptmalloc 4.866 30076 2.16 0.27 0 6807
```
