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

`MASK_PTRS` is enabled by default and causes the `user_pages_start` and `bitmap_start` pointers stored in every zone's metadata to be XOR'd with a per-zone random secret between alloc and free operations. This protects against attackers who can read or corrupt zone metadata. Each alloc and free pays a small cost for these XOR operations. Setting `MASK_PTRS=0` removes this overhead at the cost of this security property.

`CANARY_COUNT_DIV` in `conf.h` controls what fraction of chunks in a zone are reserved as canaries. It is used as a right-shift on the total chunk count: `chunk_count >> CANARY_COUNT_DIV`. The default value of 7 reserves less than 1% of chunks. Increasing this value reduces canary density and frees more chunks for user allocations; decreasing it increases security coverage at the cost of usable memory.

`ZONE_ALLOC_RETIRE` in `conf.h` controls how frequently zones are retired and replaced. A zone is retired once it has completed `ZONE_ALLOC_RETIRE * max_chunk_count_for_zone` total alloc/free cycles. Lowering this value causes zones to be replaced more often, reducing the window for use-after-free exploitation but increasing the frequency of zone creation. `BIG_ZONE_ALLOC_RETIRE` is the equivalent for big zones.

`SMALL_MEM_STARTUP` reduces the number and size of default zones created at startup. This decreases initial RSS at the cost of more frequent zone creation for programs with diverse allocation sizes.

`STRONG_SIZE_ISOLATION` enforces stricter isolation by size class. When enabled, chunk sizes are rounded up to a smaller set of buckets which increases isolation between differently-sized allocations. This may increase per-allocation waste but reduces cross-size heap exploitation primitives.

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
cfrac       je    03.07 4560 3.06 0.00 0 455
cfrac       mi    02.92 2676 2.92 0.00 0 348
cfrac       iso   05.16 30764 5.08 0.08 0 7497

espresso    je    02.49 5032 2.48 0.00 0 550
espresso    mi    02.47 3004 2.45 0.01 0 3636
espresso    iso   03.25 69124 3.16 0.09 0 30105

barnes      je    01.71 59916 1.68 0.02 0 16684
barnes      mi    01.64 57864 1.61 0.02 0 16550
barnes      iso   01.65 74968 1.61 0.03 0 20851

gs          je    00.15 37756 0.13 0.01 0 5812
gs          mi    00.15 33668 0.14 0.01 0 5110
gs          iso   00.23 67960 0.16 0.06 0 18846

larsonN     je    1.153 269184 98.81 1.00 0 419378
larsonN     mi    1.037 301044 99.34 0.41 0 83267
larsonN     iso   1304.061 121072 6.10 70.16 0 30031

rocksdb     je    02.49 162976 2.09 0.60 0 38215
rocksdb     mi    02.22 160392 1.86 0.54 0 37563
rocksdb     iso   02.87 197548 2.58 0.59 0 46899

redis       je    3.319 9484 0.14 0.02 0 1540
redis       mi    2.840 7124 0.12 0.02 0 1254
redis       iso   7.340 49712 0.34 0.04 0 14959
```

IsoAlloc isn't quite ready for performance sensitive server workloads. However it's more than fast enough for client side mobile/desktop applications with risky C/C++ attack surfaces. These environments have threat models similar to what IsoAlloc was designed for.
