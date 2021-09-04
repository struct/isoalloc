# Heap Allocator Security Feature Comparison

Heap allocators hide incredible complexity behind `malloc` and `free`. They must maintain an acceptable level of performance for various environments with wildly different memory and CPU constraints. All the security checks in the world don't matter if they require performance regressions that slow down your program by orders of magnitude. Striking a balance between performance and security is a requirement if you want people to use your library. Most allocators have some security checks, even if they're poorly implemented and easily bypassed. It's impossible to capture the nuances of how those checks work, the corner cases of when they do and don't apply, or the environment changes that affect their efficacy etc. This table is incomplete at the moment so I welcome pull requests that improve it's accuracy.

:heavy_check_mark: = Yes by default

:heavy_plus_sign: = Yes but requires configuration

:x: = Not available

:grey_question: = Todo


| Security Feature      | isoalloc         | scudo            | mimalloc         | tcmalloc        | ptmalloc3        | jemalloc         | musl's malloc-ng | malloc_hardened  |
|:---------------------:|:----------------:|:----------------:|:----------------:|:---------------:|:----------------:|:----------------:|:----------------:|:----------------:|
|Memory Isolation       |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:x:              |:x:               |:grey_question:   |:x:               |:heavy_check_mark:|
|Canaries               |:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:heavy_plus_sign: |:x:               |:heavy_check_mark:|:heavy_check_mark:|
|Guard Pages            |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:x:              |:x:               |:x:               |:heavy_check_mark:|:heavy_check_mark:|
|Randomized Allocations |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:grey_question:  |:x:               |:x:               |:heavy_check_mark:|:heavy_check_mark:|
|Pointer Obfuscation    |:heavy_check_mark:|:x:               |:heavy_plus_sign: |:grey_question:  |:x:               |:grey_question:   |:x:               |:grey_question:   |
|Double Free Detection  |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:x:              |:heavy_check_mark:|:heavy_plus_sign: |:heavy_check_mark:|:heavy_check_mark:|
|Chunk Alignment Check  |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:x:              |:heavy_check_mark:|:heavy_plus_sign: |:heavy_check_mark:|:heavy_check_mark:|
|Out Of Band Metadata   |:heavy_check_mark:|:x:               |:x:               |:x:              |:x:               |:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|
|Permanent Frees        |:heavy_check_mark:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |
|Freed Chunk Sanitization   |:heavy_check_mark:|:heavy_check_mark:|:x:           |:x:              |:x:               |:heavy_plus_sign: |:x:               |:heavy_check_mark:|
|Adjacent Chunk Verification|:heavy_check_mark:|:heavy_check_mark:|:x:           |:x:              |:x:               |:x:               |:x:               |:x:               |
|Delayed Free           |:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:x:               |:x:               |:heavy_check_mark:|:heavy_check_mark:|
|Dangling Pointer Detection |:heavy_plus_sign:|:x:            |:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |
|GWP-ASAN Like Sampling |:heavy_plus_sign: |:heavy_plus_sign: |:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |
|Type Mismatch Detection|:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:x:               |:x:               |:x:               |:heavy_check_mark:|
|Size Mismatch Detection|:heavy_check_mark:|:heavy_check_mark:|:x:               |:x:              |:heavy_plus_sign: |:x:               |:x:               |:heavy_check_mark:|
|ARM Memory Tagging     |:x:               |:heavy_check_mark:|:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |
|Zone/Chunk CPU Pinning |:heavy_check_mark:|:x:               |:x:               |:x:              |:x:               |:x:               |:x:               |:x:               |
|Chunk Race Error Detection |:x:           |:heavy_check_mark:|:grey_question:   |:x:              |:x:               |:x:               |:grey_question:   |:grey_question:   |
|Zero Size Allocation Special Handling|:x: |:x:               |:grey_question:   |:grey_question:  |:x:               |:x:               |:x:               |:heavy_check_mark:|

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
