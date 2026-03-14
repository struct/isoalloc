# IsoAlloc Performance

## The Basics

Performance is a top priority for any memory allocator. Balancing those performance priorities with security is difficult. Anytime we introduce heavy handed security checks we usually have to pay a performance penalty. But we can be deliberate about our design choices and use good tooling to find places where we can mitigate those tradeoffs. In the end correct code is better than endless security mitigations.

## Configuration and Optimizations

IsoAlloc is only designed and tested for 64 bit, so we don't have to worry about portability hurting our performance. We can assume a large address space will always be present and we can optimize for simple things like always fetching 64 bits of memory as we iterate over an array. This remainder of this section is a basic overview of the performance optimizations in IsoAlloc.

Perhaps the most important optimization in IsoAlloc is the design choice to use a simple bitmap for tracking chunk states. Combining this with zones comprised of contiguous chunks of pages results in good performance at the cost of memory. This is in contrast to typical allocator designs full of linked list code that tends to result far more complex code, slow page faults, and exploitable designs.

All data fetches from a zone bitmap are 64 bits at a time which takes advantage of fast CPU pipelining. Fetching bits at a different bit width will result in slower performance by an order of magnitude in allocation intensive tests. All user chunks are 8 byte aligned no matter how big each chunk is. Accessing this memory with proper alignment will minimize CPU cache flushes.

When `PRE_POPULATE_PAGES` is enabled in the Makefile global caches, the root, and zone bitmaps (but not pages that hold user data) are created with `MAP_POPULATE` which instructs the kernel to pre-populate the page tables which reduces page faults and results in better performance. Note that by default at zone creation time user pages will have canaries written at random aligned offsets. This will cause page faults and populate those PTE's when the pages are first written to whether those pages are ever used at runtime or not. If you disable canaries it will result in lower RSS and a faster runtime performance.

The `MAX_ZONES` value in `conf.h` limits the total number of zones that can be allocated at runtime. If your program is being killed with OOM errors you can safely increase this value, however its max value is 65535. However it will result in a larger allocation for the `root->zones` array which holds meta data for each zone whether that zone is currently mapped and in use or not. To calculate the total number of bytes available for allocations you can do (`MAX_ZONES * ZONE_USER_SIZE`). Note that `ZONE_USER_SIZE` is not configurable in `conf.h`.

Default zones for common sizes are created in the library constructor. This helps speed up allocations for long running programs. New zones are created on demand when needed but this will incur a small performance penalty in the allocation path.

All chunk sizes are multiples of 32 with a minimum value of `SMALLEST_CHUNK_SZ` (32 by default, alignment and smallest chunk size should be in sync) and a maximum value of `SMALL_SIZE_MAX` up to 65536 by default. In a configuration with `SMALL_SIZE_MAX` set to 65536 zones will only be created for 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, and 65536. You can increase `SMALL_SIZE_MAX` up to 131072. If you choose to change this value be mindful of the pages that it can waste (e.g. allocating a chunk of 16385 bytes will result in returning a chunk of 32768 bytes).

Everything above `SMALL_SIZE_MAX` is allocated by the big zone path which has a limitation of 4 GB and a size granularity that is only limited by page size alignment. Big zones have meta data allocated separately. Guard pages for this meta data can be enabled or disabled using `BIG_ZONE_META_DATA_GUARD`. Likewise `BIG_ZONE_GUARD` can be used to enable or disable guard pages for big zone user data pages.

By default user chunks are not sanitized upon free. While this helps mitigate uninitialized memory vulnerabilities it is a very slow operation. You can enable this feature by changing the `SANITIZE_CHUNKS` flag in the Makefile.

When `USE_MLOCK` is enabled in the Makefile the meta data for the IsoAlloc root and other caches will be locked with `mlock`. This means this data will never be swapped to disk. We do this because using these data structures is required for both the alloc and free hot paths. This operation may fail if we are running inside a container with memory limits. Failure to lock the memory will not cause an abort and all failures will be silently ignored.

By default IsoAlloc randomizes the freelist per zone which results in a small performance hit. You can disable this by setting `RANDOMIZE_FREELIST` to 0 in the Makefile.

When enabled `USE_SPINLOCK` will use spinlocks via `atomic_flag` instead of a pthread mutex. Performance and load testing of IsoAlloc has shown spinlocks are slightly slower than a mutex so it is not the preferred default option.

If you know your program will not require multi-threaded access to IsoAlloc you can disable threading support by setting the `THREAD_SUPPORT` define to 0 in the Makefile. This will remove all atomic/mutex lock/unlock operations from the allocator, which will result in significant performance gains in some programs. If you do require thread support then you may want to profile your program to determine what default zone sizes will benefit performance.

`DISABLE_CANARY` can be set to 1 to disable the creation and verification of canary chunks. This removes a useful security feature but will significantly improve performance and RSS.

By default IsoAlloc will attempt to use Huge Pages (for both Linux and Mac OS) for any allocations that are a multiple of 2 mb in size. This is the default huge page size on most systems but it might not be on yours. On Linux you can check the value for your system by running the following command:

```
cat /proc/meminfo | grep Hugepagesize
Hugepagesize:       2048 kB
```

If necessary you can adjust the value of `HUGE_PAGE_SZ` in `conf.h` to reflect the size on your system.

## Caches and Memoization

There are a few important caches and memoization techniques used in IsoAlloc. These significantly improve the performance of alloc/free hot paths and keep the design as simple as possible.

### Zone Free List

Each zone contains an array of bitslots that represent free chunks in that zone. The allocation hot path searches this list first for a free chunk that can satisfy the allocation request. Allocating chunks from this list is a lot faster than iterating through a zones bitmap for a free bitslot. This cache is refilled whenever it is low. Free'd chunks are added to this list after they've been in the quarantine.

### Chunk to Zone Lookup

The chunk-to-zone lookup table is a high hit rate cache for finding which zone owns a user chunk. It works by mapping the MSB of the chunk address to a zone index. Misses are gracefully handled and more common with a higher RSS and more mappings.

### MRU Zone Cache

It is not uncommon to write a program that uses multiple threads for different purposes. Some threads will never make an allocation request above or below a certain size. This thread local cache optimizes for this by storing a TLS array of the threads most recently used zones. These zones are checked in the `iso_find_zone_range` free path if the chunk-to-zone lookup fails.

### Thread Chunk Quarantine

This thread local cache speeds up the free hot path by quarantining chunks until a threshold has been met. Until that threshold is reached free's are very cheap. When the cache is emptied and the chunks free'd its still faster because we take advantage of keeping the zone meta data in a CPU cache line.

### Zone Lookup Table

Zones are linked by their `next_sz_index` member which tells the allocator where in the `_root->zones` array it can find the next zone that holds the same size chunks. This lookup table helps us find the first zone that holds a specific size in O(1) time. This is achieved by placing a zone's index value at that zones size index in the table, e.g. `zone_lookup_table[zone->size] = zone->index`, from there we just need to use the next zone's index member and walk it like a singly linked list to find other zones of that size. Zones are added to the front of the list as they are created.

## ARM NEON

IsoAlloc tries to use ARM Neon instructions to make loops in the hot path faster. If you want to disable any of this code at compile time and rely instead on more portable, but potentially slower code just set `DONT_USE_NEON` to 1 in the Makefile.

## Tests

I've spent a good amount of time testing IsoAlloc to ensure its reasonably fast compared to glibc/ptmalloc. But it is impossible for me to model or simulate all the different states a program that uses IsoAlloc may be in. This section briefly covers the existing performance related tests for IsoAlloc and the data I have collected so far.

The `malloc_cmp_test` build target will build 2 different versions of the test/tests.c program which runs roughly 1.4 million alloc/calloc/realloc operations. We free every other allocation and then free the remaining ones once the allocation loop is complete. This helps simulate some fragmentation in the heap. On average IsoAlloc is faster than ptmalloc but the difference is so close it will likely not be noticable.

The following test was run in an Ubuntu 20.04.3 LTS (Focal Fossa) for ARM64 docker container with libc version 2.31-0ubuntu9.2 on a MacOS host. The kernel used was `Linux f7f23ca7dc44 5.10.76-linuxkit`.

```
IsoAlloc
build/tests
iso_alloc/iso_free 1834784 tests completed in 0.081202 seconds
iso_calloc/iso_free 1834784 tests completed in 1.041517 seconds
iso_realloc/iso_free 1834784 tests completed in 0.828665 seconds

jemalloc
LD_PRELOAD=./libjemalloc.so build/tests
iso_alloc/iso_free 1834784 tests completed in 0.084586 seconds
iso_calloc/iso_free 1834784 tests completed in 1.461562 seconds
iso_realloc/iso_free 1834784 tests completed in 0.779396 seconds

scudo
LD_PRELOAD=./libscudo.so build/malloc_tests
malloc/free 1834784 tests completed in 0.717936 seconds
calloc/free 1834784 tests completed in 2.706141 seconds
realloc/free 1834784 tests completed in 1.840283 seconds

system malloc
malloc/free 1834784 tests completed in 0.662565 seconds
calloc/free 1834784 tests completed in 2.728955 seconds
realloc/free 1834784 tests completed in 1.943556 seconds

```

This same test can be used with the `perf` utility to measure basic stats like page faults and CPU utilization using both heap implementations. The output below is on the same AWS t2.xlarge instance as above.

```
$ sudo perf stat build/tests

iso_alloc/iso_free 1834784 tests completed in 0.075247 seconds
iso_calloc/iso_free 1834784 tests completed in 1.100221 seconds
iso_realloc/iso_free 1834784 tests completed in 0.901481 seconds

 Performance counter stats for 'build/tests':

     2,082,958,624      task-clock                       #    0.874 CPUs utilized             
                26      context-switches                 #   12.482 /sec                      
                 0      cpu-migrations                   #    0.000 /sec                      
           576,000      page-faults                      #  276.530 K/sec                     
     <not counted>      armv8_pmuv3_0/instructions/                                             (0.00%)
    10,522,826,144      armv8_pmuv3_1/instructions/      #    1.33  insn per cycle            
                                                  #    0.61  stalled cycles per insn   
     <not counted>      armv8_pmuv3_0/cycles/                                                   (0.00%)
     7,910,463,871      armv8_pmuv3_1/cycles/            #    3.798 GHz                       
     <not counted>      armv8_pmuv3_0/stalled-cycles-frontend/                                        (0.00%)
       149,093,549      armv8_pmuv3_1/stalled-cycles-frontend/ #    1.88% frontend cycles idle      
     <not counted>      armv8_pmuv3_0/stalled-cycles-backend/                                        (0.00%)
     6,432,153,136      armv8_pmuv3_1/stalled-cycles-backend/ #   81.31% backend cycles idle       
     <not counted>      armv8_pmuv3_0/branches/                                                 (0.00%)
     1,914,734,216      armv8_pmuv3_1/branches/          #  919.238 M/sec                     
     <not counted>      armv8_pmuv3_0/branch-misses/                                            (0.00%)
         3,870,559      armv8_pmuv3_1/branch-misses/     #    0.20% of all branches           

       2.382450831 seconds time elapsed

       1.325971000 seconds user
       1.055181000 seconds sys
```

The following benchmarks were collected from [mimalloc-bench](https://github.com/daanx/mimalloc-bench) with the default configuration of IsoAlloc. As you can see from the data IsoAlloc is competitive with other allocators for some benchmarks but clearly falls behind on others. For any benchmark that IsoAlloc scores poorly on I was able to tweak its build to improve the CPU time and memory consumption. It's worth noting that IsoAlloc was able to stay competitive even with performing many security checks not present in other allocators. Please note these are 'best case' measurements, not averages.

```
#------------------------------------------------------------------
# test    alloc   time  rss    user  sys  page-faults page-reclaims
cfrac       je    02.99 4912 2.99 0.00 0 454
cfrac       mi    03.01 2484 3.00 0.00 0 346
cfrac       iso   05.84 26616 5.75 0.09 0 6502

espresso    je    02.52 4872 2.50 0.01 0 538
espresso    mi    02.46 3060 2.45 0.01 0 3637
espresso    iso   03.65 69876 3.56 0.09 0 21695

barnes      je    01.62 60268 1.59 0.02 0 16687
barnes      mi    01.71 57672 1.68 0.02 0 16550
barnes      iso   01.66 74628 1.62 0.03 0 20851

gs          je    00.16 37592 0.15 0.01 0 5808
gs          mi    00.16 32588 0.13 0.02 0 5109
gs          iso   00.23 71152 0.16 0.07 0 19698

larsonN     je    1.171 266596 98.81 0.92 0 409842
larsonN     mi    1.016 299768 99.38 0.44 0 83755
larsonN     iso   918.582 126528 99.64 0.37 0 31368

rocksdb     je    02.48 162424 2.05 0.63 0 38384
rocksdb     mi    02.48 159812 2.04 0.66 0 37464
rocksdb     iso   02.74 197220 2.49 0.55 0 46815

redis       je    3.180 9496 0.14 0.02 0 1538
redis       mi    3.080 7088 0.12 0.03 0 1256
redis       iso   6.880 52816 0.31 0.05 0 16317
```

IsoAlloc isn't quite ready for performance sensitive server workloads. However it's more than fast enough for client side mobile/desktop applications with risky C/C++ attack surfaces. These environments have threat models similar to what IsoAlloc was designed for.
