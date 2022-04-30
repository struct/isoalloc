# IsoAlloc Heap Profiler

Different workloads can affect the performance of IsoAlloc. For example, if your program only ever makes allocations between 32 and 512 bytes then theres no reason to allocate zones for sizes above 512 bytes. Other workloads might make a lot of short lived allocations of one size. By sampling allocations from this target we can produce data that results in a better understanding of memory usage through the programs lifetime. This data can be used to generate more efficient configurations of IsoAlloc.

The heap profiler is designed to sample heap allocations over time across your workload. This profiler emits a machine readable file throughout the lifetime of the sampled target. These files are then passed to the profiler tool where they are merged. After merging the profiler tool will emit a C header file `iso_alloc_target_config.h`. This tooling is still under development.

In order to get the most out of the profiler it is recommended to compile all of your code with `-fno-omit-frame-pointer`. Without this flag IsoAlloc can't properly collect backtrace information and may even be unstable.

## Profiler Tuning

You can control the file profiler data is written to with the `ISO_ALLOC_PROFILER_FILE_PATH` environment variable. The default path is `$CWD/iso_alloc_profiler.data`.

Currently theres only one way to tune the profiler internals and thats by changing `PROFILER_ODDS` or `CHUNK_USAGE_THRESHOLD`. These control the rate at which we sample allocations and the % a zone must be full before being recorded as such.

## Profiler Output Format

The profiler outputs a file (example below) that contains information about the state of the IsoAlloc managed heap. This information is captured by sampling allocations during runtime and when the process is exiting.

```
# Total Allocations
allocated=5766465

# Number of allocations sampled
alloc_sampled=551

# Total free's
freed=4324848

# Number of free's sampled
free_sampled=427

# Sampled unique backtraces to malloc/free
# backtrace id, backtrace hash, number of calls, smallest size requested, largest size requested, backtrace

alloc_backtrace=0,backtrace_hash=0x8614,calls=117,lower_bound_size=16,upper_bound_size=8192
	0xffffab91a010 -> iso_alloc build/libisoalloc.so
	0x400f44 -> [?]
	0x401134 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=1,backtrace_hash=0x86a0,calls=9,lower_bound_size=45,upper_bound_size=538
	0xffffab91a010 -> iso_alloc build/libisoalloc.so
	0x400f44 -> [?]
	0x401180 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=2,backtrace_hash=0xea18,calls=148,lower_bound_size=16,upper_bound_size=8192
	0xffffab916d64 -> [?]
	0xffffab91a03c -> iso_calloc build/libisoalloc.so
	0x400d04 -> [?]
	0x401230 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=3,backtrace_hash=0xea54,calls=16,lower_bound_size=134,upper_bound_size=8212
	0xffffab916d64 -> [?]
	0xffffab91a03c -> iso_calloc build/libisoalloc.so
	0x400d04 -> [?]
	0x40127c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=4,backtrace_hash=0x81dc,calls=127,lower_bound_size=8,upper_bound_size=4096
	0xffffab91a010 -> iso_alloc build/libisoalloc.so
	0x400a94 -> [?]
	0x40132c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=5,backtrace_hash=0x2040,calls=104,lower_bound_size=16,upper_bound_size=8192
	0xffffab91a010 -> iso_alloc build/libisoalloc.so
	0xffffab91a1c8 -> iso_realloc build/libisoalloc.so
	0x400ac0 -> [?]
	0x40132c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=6,backtrace_hash=0x2014,calls=11,lower_bound_size=151,upper_bound_size=4124
	0xffffab91a010 -> iso_alloc build/libisoalloc.so
	0xffffab91a1c8 -> iso_realloc build/libisoalloc.so
	0x400ac0 -> [?]
	0x401378 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
alloc_backtrace=7,backtrace_hash=0x8188,calls=11,lower_bound_size=75,upper_bound_size=2062
	0xffffab91a010 -> iso_alloc build/libisoalloc.so
	0x400a94 -> [?]
	0x401378 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=0,backtrace_hash=0x86d4,calls=66
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400ffc -> [?]
	0x401134 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=1,backtrace_hash=0x995c,calls=60
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x401074 -> [?]
	0x401134 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=2,backtrace_hash=0x99e8,calls=7
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x401074 -> [?]
	0x401180 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=3,backtrace_hash=0x8660,calls=6
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400ffc -> [?]
	0x401180 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=4,backtrace_hash=0x8418,calls=66
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400e34 -> [?]
	0x401230 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=5,backtrace_hash=0x8790,calls=72
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400dbc -> [?]
	0x401230 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=6,backtrace_hash=0x87dc,calls=6
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400dbc -> [?]
	0x40127c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=7,backtrace_hash=0x8454,calls=4
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400e34 -> [?]
	0x40127c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=8,backtrace_hash=0x80c0,calls=64
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400bf0 -> [?]
	0x40132c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=9,backtrace_hash=0x8048,calls=55
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400b78 -> [?]
	0x40132c -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=10,backtrace_hash=0x8094,calls=4
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400bf0 -> [?]
	0x401378 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]
free_backtrace=11,backtrace_hash=0x801c,calls=5
	0xffffab91a068 -> iso_free build/libisoalloc.so
	0x400b78 -> [?]
	0x401378 -> [?]
	0xffffab793090 -> __libc_start_main /lib/aarch64-linux-gnu/libc.so.6
	0x4008e4 -> [?]

# Chunk size, number of zones holding that size,
# number of times that zone was 75% full
128,1,1
256,1,6
512,1,29
1024,1,58
2048,4,116
4096,16,570
8192,65,3195
16384,33,13
```

The profiler will collect backtraces in order to produce a report about callers into IsoAlloc. This data is helpful for understanding memory allocation patterns in a program. These hashes are not immediately usable, the profiler uses them internally to track unique call stacks. If the profiler data shows a large number of backtraces then its unlikely using just a handful of memory allocation abstractions (e.g. its frequently calling malloc/new). Due to their size (16 bits) calculated backtraces may not be entirely unique. To get the most accurate results from this feature please compile IsoAlloc and your program with the `-fno-omit-frame-pointer` option.

The 'Zone data' shown above is a simple CSV format that is displaying the size of chunks, the number of zones holding chunks of that size, and the number of times the zone was more than `CHUNK_USAGE_THRESHOLD` % (default=75%) full when being sampled. In the example above this program was making a high number of 16384, and 4096 byte allocations.

## Profiler Tool

TODO - A CLI utility that reads the profiler output and produces an IsoAlloc configuration suited for that runtime. This tool isn't written yet.

## Allocator Based Program Profiling

The profiler built into IsoAlloc is pretty simple. We sample calls to `malloc()` and `free()` and record some basic information such as the size of the allocation requested, and record unique backtraces that made the call. This information is useful for various security research (e.g. fuzzing instrumentation) and analysis of code paths that perform memory allocation and manipulation. The public API now defines a structure, `iso_alloc_traces_t`, that can be requested from the allocator that contains this information. The profiler is still experimental so please expect the API to be unstable and change frequently.
