# Heap Allocator Security Feature Comparison

Heap allocators hide incredible complexity behind `malloc` and `free`. They must maintain an acceptable level of performance for various environments with wildly different memory and CPU constraints. All the security checks in the world don't matter if they require performance regressions that slow down your program by orders of magnitude. Striking a balance between performance and security is a requirement if you want people to use your library. Most allocators have some security checks, even if they're poorly implemented and easily bypassed. It's impossible to capture the nuances of how those checks work, the corner cases of when they do and don't apply, or the environment changes that affect their efficacy etc. This table is incomplete at the moment so I welcome pull requests that improve its accuracy.

:heavy_check_mark: = Yes by default

:heavy_plus_sign: = Yes but requires configuration

:heavy_minus_sign: = Yes but via non-standard api

:x: = Not available

:grey_question: = Todo


| Security Feature                    | isoalloc         | scudo            | mimalloc         | tcmalloc        | ptmalloc3        | jemalloc         | musl's malloc-ng | hardened_malloc  | PartitionAlloc   | snmalloc |
|:-----------------------------------:|:----------------:|:----------------:|:----------------:|:---------------:|:----------------:|:----------------:|:----------------:|:----------------:|:----------------:|:----------------:|
|Memory Isolation                     |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:x:              |:x:               |:grey_question:   |:x:               |:heavy_check_mark:|:heavy_check_mark:|:grey_question:
|Canaries                             |:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:heavy_plus_sign: |:x:               |:heavy_check_mark:|:heavy_check_mark:|:grey_question:   |:heavy_check_mark:
|Non-global canary                    |:heavy_check_mark:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|:grey_question:   |:heavy_check_mark:
|Guard Pages                          |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:x:              |:x:               |:x:               |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:
|Randomized Allocations               |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:grey_question:  |:x:               |:x:               |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:
|Pointer Obfuscation                  |:heavy_check_mark:|:x:               |:heavy_plus_sign: |:grey_question:  |:x:               |:grey_question:   |:x:               |:grey_question:   |:heavy_check_mark:|:heavy_check_mark:
|Double Free Detection                |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:x:              |:heavy_check_mark:|:heavy_plus_sign: |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:heavy_check_mark:
|Chunk Alignment Check                |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:x:              |:heavy_check_mark:|:heavy_plus_sign: |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:
|Out Of Band Metadata                 |:heavy_check_mark:|:x:               |:x:               |:x:              |:x:               |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:x:               |:heavy_check_mark:
|Permanent Frees                      |:heavy_minus_sign:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |:x:               |:x:
|Freed Chunk Sanitization             |:heavy_plus_sign: |:heavy_check_mark:|:x:               |:x:              |:x:               |:heavy_plus_sign: |:x:               |:heavy_check_mark:|:heavy_plus_sign: |:x:
|Adjacent Chunk Verification          |:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:x:               |:x:               |:x:               |:grey_question:   |:x:               |:x:
|Delayed Free                         |:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:x:               |:x:               |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:heavy_check_mark:
|Dangling Pointer Detection           |:heavy_plus_sign: |:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|:heavy_plus_sign: |:x:
|GWP-ASAN Like Sampling               |:heavy_plus_sign: |:heavy_plus_sign: |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|:grey_question:   |:x:
|Size Mismatch Detection              |:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:heavy_plus_sign: |:x:               |:x:               |:heavy_check_mark:|:heavy_plus_sign: |:grey_question:
|ARM Memory Tagging                   |:x:               |:heavy_check_mark:|:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |:x:               |:grey_question:
|Zone/Chunk CPU/Thread Pinning        |:heavy_plus_sign: |:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|:x:               |:x:
|Chunk Race Error Detection           |:x:               |:heavy_check_mark:|:grey_question:   |:x:              |:x:               |:x:               |:grey_question:   |:grey_question:   |:grey_question:   |:heavy_plus_sign:
|Zero Size Allocation Special Handling|:heavy_check_mark:|:x:               |:grey_question:   |:grey_question:  |:x:               |:x:               |:x:               |:heavy_check_mark:|:x:               |:x:
|Read-only global structure           |:heavy_minus_sign:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|:x:               |:x:
|SW Memory Tagging                    |:heavy_minus_sign:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |:heavy_check_mark:|:x:
|Guarded memcpy/memmove               |:heavy_check_mark:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_minus_sign:|:x:               |:heavy_check_mark:
|Automatic initialization             |:x:               |:heavy_plus_sign: |:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|:x:               |:x:

**Lexicon**

- Non-global canary: There isn't a single global secret where all bets are off when leaked, but a different canary per slab/arena.
- *Double Free Detection*: A pointer can't be freed twice.
- *Chunk Alignment Check*: Pointers passed to `free` must be aligned, and multiples of the zone chunk size.
- *Out Of Band Metadata*: The chunk metadata are not stored next to/in the chunk, but out of band, making them harder to corrupt.
- *Freed Chunk Sanitization*: Freed memory is overwritten by some value, preventing info-leaks.
- *Adjacent Chunk Verification*: When freeing a chunk the canaries in adjacent chunks above/below are verified.
- *Delayed Free*: Chunks aren't immediately freed, making use-after-free exploitation without a heap-spray harder.
- *Size Mismatch Detection*: In C++, type-confusing at free time of objects with *sufficiently different sizes* is detected.
- *Arm Memory Tagging*: See https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/enhancing-memory-safety
- *Zone/Chunk CPU Pinning*: Allocations from a given zone are restricted to the CPU core that created that zone.
- *Zero Size Allocation Special Handling*: Zero size allocations are treated like a dedicated size class,
  and point to a non-readable and non-writable region, to ensure that the application can't use them in any way.
- *Read-only global structure*: The global state structure is entirely read-only after initialization.
- *SW Memory Tagging*: The ability to tag pointers with a unique tag that is later verified before pointer dereference
- *Guarded Memcpy*: Ability to use allocator meta-data to protect destination of memcpy from overflow.
- *Automatic initialization*: The allocator memory is always initialized to a specific value, usually `0`. Note that while this is cheaper than zero'ing on free, it is also less powerful.

**Notes**

Not every security feature is needed in every allocator. A missing feature in the table above may not mean the allocator needs it but may instead may not require it. The table needs a N/A qualifier. This section attempts to capture some of this nuance.

- SNMalloc stores free lists in band but protects it

**Sources**

https://github.com/struct/IsoAlloc

https://source.android.com/devices/tech/debug/scudo

https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/scudo/standalone

https://www.microsoft.com/en-us/research/uploads/prod/2019/06/mimalloc-tr-v1.pdf

Thanks to [Kostya Kortchinsky](https://twitter.com/@crypt0ad) for reviewing the Scudo column

https://dustri.org/b/security-features-of-musl.html

https://github.com/GrapheneOS/hardened_malloc#security-properties

https://downloads.immunityinc.com/infiltrate-archives/webkit_heap.pdf

https://census-labs.com/media/shadow-infiltrate-2017.pdf

https://blog.nsogroup.com/a-tale-of-two-mallocs-on-android-libc-allocators-part-3-exploitation/

https://sourceware.org/glibc/wiki/MallocInternals

https://source.chromium.org/chromium/chromium/src/+/master:base/allocator/partition_allocator/PartitionAlloc.md

https://source.chromium.org/chromium/chromium/src/+/master:base/allocator/partition_allocator/starscan/README.md
