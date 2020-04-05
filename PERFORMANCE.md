# IsoAlloc Performance

## The Basics

Performance is a top priority for any memory allocator. Balancing those performance priorities with security is difficult. Anytime we introduce heavy handed security checks we usually have to pay a performance penalty. But we can be deliberate about our design choices and use good tooling to find places where we can mitigate those tradeoffs. In the end correct code is better than endless security mitigations.

## Optimizations

IsoAlloc is only designed and tested for 64 bit Linux systems, so we don't have to worry about portability hurting our performance. We can assume a large address space will always be present and we can optimize for simple things like always fetching 64 bits of memory as we iterate over an array. This remainder of this section is a basic overview of the performance optimizations in IsoAlloc.

Perhaps the most important optimization in IsoAlloc is the design choice to use a simple bitmap for tracking chunk states. Combining this with zones comprised of contiguous chunks of pages results in good performance at the cost of memory. This is in contrast to typical allocator designs full of linked list code that tends to result far more complex code and slow page faults.

An important optimization is the freelist cache. This cache is just a per-zone array of free bit slots. The allocation hot path searches this cache first in the hopes that the zone has a free chunk available. Allocating chunks from this cache is very fast.

All data fetches from a zone bitmap are 64 bits at a time which takes advantage of fast CPU pipelining. Fetching bits at a different bit width will result in slower performance by an order of magnitude in allocation intensive tests. All user chunks are 8 byte aligned no matter how big each chunk is. Accessing this memory with proper alignment will minimize CPU cache flushes.

All bitmaps pages allocated with `mmap` are passed to the `madvise` syscall with the advice arguments `MADV_WILLNEED` and `MADV_SEQUENTIAL`. All user pages allocated with `mmap` are passed to the `madvise` syscall with the advice arguments `MADV_WILLNEED` and `MADV_RANDOM`. By default both of these mappings are created with `MAP_POPULATE` which instructs the kernel to pre-populate the page tables which reduces page faults and results in better performance. You can disable this with the `PRE_POPULATE_PAGES` Makefile flag. Note that at zone creation time user pages will have canaries written at random aligned offsets. This will cause page faults when the pages are first written to whether those pages are ever used at runtime or not. If we risk wasting memory we mine as well have the kernel pre-populate the page tables for us and increase the performance.

Default zones for common sizes are created in the library constructor. This helps speed up allocations for long running programs. New zones are created on demand when needed but this will incur a small performance penalty in the allocation path.

By default user chunks are not sanitized upon free. While this helps mitigate uninitialized memory vulnerabilities it is a very slow operation. You can enable this feature by changing the `SANITIZE_CHUNKS` flag in the Makefile.

The meta data for all default zones will be locked with `mlock`. This means this data will never be swapped to disk. We do this because iterating over these data structures is required for both the alloc and free paths. This operation may fail if we are running inside a container with memory limits. Failure to lock the memory will not cause an abort and the error will be silently ignored in the initialization of the root structure. Zones that are created on demand after initialization will not have their memory locked.

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

The test above is on a free tier Amazon t2.micro instance with 1 CPU and 1 Gb of RAM. IsoAlloc just barely beats ptmalloc here. Below are the results from an Amazon c5n.large instance with 2 CPUs and 4Gb of memory.

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

IsoAlloc seems to outperform ptmalloc in these artificial tests but this is just one data point.
