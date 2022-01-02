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
alloc_sampled=566

# Total free's
freed=5766464

# Number of free's sampled
free_sampled=586

# Sampled unique backtraces to malloc/free
# backtrace id, backtrace hash, number of calls, smallest size requested, largest size requested, backtrace
allocated=5766465
alloc_sampled=561
freed=5766464
free_sampled=570
alloc_backtrace=0,backtrace_hash=0xbabc,calls=134,lower_bound_size=16,upper_bound_size=8192,0xffffbd060ce8,0x400f34,0x401124,0xffffbceda090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=1,backtrace_hash=0xbae8,calls=11,lower_bound_size=45,upper_bound_size=538,0xffffbd060ce8,0x400f34,0x401170,0xffffbceda090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=2,backtrace_hash=0x63e4,calls=113,lower_bound_size=16,upper_bound_size=8192,0xffffbd05d860,0xffffbd060d14,0x400cf4,0x401220,0xffffbceda090,0x4008d4,0x0,0x0
alloc_backtrace=3,backtrace_hash=0x63a8,calls=6,lower_bound_size=134,upper_bound_size=4114,0xffffbd05d860,0xffffbd060d14,0x400cf4,0x40126c,0xffffbceda090,0x4008d4,0x0,0x0
alloc_backtrace=4,backtrace_hash=0xb0f4,calls=146,lower_bound_size=16,upper_bound_size=8192,0xffffbd060ce8,0xffffbd060df4,0x400ab0,0x40131c,0xffffbceda090,0x4008d4,0x0,0x0
alloc_backtrace=5,backtrace_hash=0xbd34,calls=122,lower_bound_size=8,upper_bound_size=4096,0xffffbd060ce8,0x400a84,0x40131c,0xffffbceda090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=6,backtrace_hash=0xb080,calls=10,lower_bound_size=151,upper_bound_size=4124,0xffffbd060ce8,0xffffbd060df4,0x400ab0,0x401368,0xffffbceda090,0x4008d4,0x0,0x0
alloc_backtrace=7,backtrace_hash=0xbd40,calls=11,lower_bound_size=19,upper_bound_size=2062,0xffffbd060ce8,0x400a84,0x401368,0xffffbceda090,0x4008d4,0x0,0x0,0x0

free_backtrace=0,backtrace_hash=0xbbcc,calls=61,0xffffbd060d40,0x400fec,0x401124,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=1,backtrace_hash=0xa444,calls=59,0xffffbd060d40,0x401064,0x401124,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=2,backtrace_hash=0xbb98,calls=7,0xffffbd060d40,0x400fec,0x401170,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=3,backtrace_hash=0xa410,calls=8,0xffffbd060d40,0x401064,0x401170,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=4,backtrace_hash=0xba88,calls=70,0xffffbd060d40,0x400dac,0x401220,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=5,backtrace_hash=0xb900,calls=65,0xffffbd060d40,0x400e24,0x401220,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=6,backtrace_hash=0xbac4,calls=6,0xffffbd060d40,0x400dac,0x40126c,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=7,backtrace_hash=0xb94c,calls=6,0xffffbd060d40,0x400e24,0x40126c,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=8,backtrace_hash=0xb2f8,calls=127,0xffffbd060d40,0xffffbd060e50,0x400ab0,0x40131c,0xffffbceda090,0x4008d4,0x0,0x0
free_backtrace=9,backtrace_hash=0xbd70,calls=67,0xffffbd060d40,0x400b68,0x40131c,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=10,backtrace_hash=0xbdf8,calls=64,0xffffbd060d40,0x400be0,0x40131c,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=11,backtrace_hash=0xb28c,calls=7,0xffffbd060d40,0xffffbd060e50,0x400ab0,0x401368,0xffffbceda090,0x4008d4,0x0,0x0
free_backtrace=12,backtrace_hash=0xbd04,calls=6,0xffffbd060d40,0x400b68,0x401368,0xffffbceda090,0x4008d4,0x0,0x0,0x0
free_backtrace=13,backtrace_hash=0xbd8c,calls=3,0xffffbd060d40,0x400be0,0x401368,0xffffbceda090,0x4008d4,0x0,0x0,0x0

# Chunk size, number of zones holding that size,
# number of times that zone was 75% full
256,1,8
512,1,23
1024,1,51
2048,1,82
4096,14,575
8192,71,2592
16384,15,546

```

The profiler will collect backtraces in order to produce a report about callers into IsoAlloc. This data is helpful for understanding memory allocation patterns in a program. These hashes are not immediately usable, the profiler uses them internally to track unique call stacks. If the profiler data shows a large number of backtraces then its unlikely using just a handful of memory allocation abstractions (e.g. its frequently calling malloc/new). Due to their size (16 bits) calculated backtraces may not be entirely unique. To get the most accurate results from this feature please compile IsoAlloc and your program with the `-fno-omit-frame-pointer` option.

The 'Zone data' shown above is a simple CSV format that is displaying the size of chunks, the number of zones holding chunks of that size, and the number of times the zone was more than `CHUNK_USAGE_THRESHOLD` % (default=75%) full when being sampled. In the example above this program was making a high number of 16384, and 4096 byte allocations.

## Profiler Tool

TODO - A CLI utility that reads the profiler output and produces an IsoAlloc configuration suited for that runtime. This tool isn't written yet.

## Allocator Based Program Profiling

The profiler built into IsoAlloc is pretty simple. We sample calls to `malloc()` and `free()` and record some basic information such as the size of the allocation requested, and record unique backtraces that made the call. This information is useful for various security research (e.g. fuzzing instrumentation) and analysis of code paths that perform memory allocation and manipulation. The public API now defines a structure, `iso_alloc_traces_t`, that can be requested from the allocator that contains this information. The profiler is still experimental so please expect the API to be unstable and change frequently.
