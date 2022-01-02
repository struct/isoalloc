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

# Unique backtrace hashes to malloc()
alloc_backtrace_hash=0xc080,calls=12
alloc_backtrace_hash=0xc0f4,calls=125
alloc_backtrace_hash=0xf224,calls=143
alloc_backtrace_hash=0xf268,calls=11
alloc_backtrace_hash=0xfa88,calls=11
alloc_backtrace_hash=0xfadc,calls=113
alloc_backtrace_hash=0xfd20,calls=11
alloc_backtrace_hash=0xfd54,calls=140

# Unique backtrace hashes to free()
free_backtrace_hash=0xc08c,calls=7
free_backtrace_hash=0xc0f8,calls=129
free_backtrace_hash=0xe5b0,calls=9
free_backtrace_hash=0xe5e4,calls=89
free_backtrace_hash=0xf8a0,calls=80
free_backtrace_hash=0xf8ec,calls=8
free_backtrace_hash=0xfa38,calls=6
free_backtrace_hash=0xfa6c,calls=57
free_backtrace_hash=0xfb28,calls=67
free_backtrace_hash=0xfb64,calls=3
free_backtrace_hash=0xfc2c,calls=8
free_backtrace_hash=0xfc58,calls=58
free_backtrace_hash=0xfca4,calls=3
free_backtrace_hash=0xfcd0,calls=62

# Sampled unique backtraces to malloc
# backtrace id, backtrace hash, number of calls, smallest size requested, largest size requested, backtrace
alloc_backtrace=0,backtrace_hash=0xfadc,calls=113,lower_bound_size=16,upper_bound_size=8192,0xffff94ac3c88,0x400f34,0x401124,0xffff9493d090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=1,backtrace_hash=0xfa88,calls=11,lower_bound_size=45,upper_bound_size=538,0xffff94ac3c88,0x400f34,0x401170,0xffff9493d090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=2,backtrace_hash=0xf224,calls=143,lower_bound_size=16,upper_bound_size=8192,0xffff94ac0800,0xffff94ac3cb4,0x400cf4,0x401220,0xffff9493d090,0x4008d4,0x0,0x0
alloc_backtrace=3,backtrace_hash=0xf268,calls=11,lower_bound_size=134,upper_bound_size=4114,0xffff94ac0800,0xffff94ac3cb4,0x400cf4,0x40126c,0xffff9493d090,0x4008d4,0x0,0x0
alloc_backtrace=4,backtrace_hash=0xfd54,calls=140,lower_bound_size=8,upper_bound_size=4096,0xffff94ac3c88,0x400a84,0x40131c,0xffff9493d090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=5,backtrace_hash=0xc0f4,calls=125,lower_bound_size=16,upper_bound_size=8192,0xffff94ac3c88,0xffff94ac3d94,0x400ab0,0x40131c,0xffff9493d090,0x4008d4,0x0,0x0
alloc_backtrace=6,backtrace_hash=0xfd20,calls=11,lower_bound_size=16,upper_bound_size=2062,0xffff94ac3c88,0x400a84,0x401368,0xffff9493d090,0x4008d4,0x0,0x0,0x0
alloc_backtrace=7,backtrace_hash=0xc080,calls=12,lower_bound_size=151,upper_bound_size=4124,0xffff94ac3c88,0xffff94ac3d94,0x400ab0,0x401368,0xffff9493d090,0x4008d4,0x0,0x0

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
