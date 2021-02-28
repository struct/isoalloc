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
# Total allocations
allocated=5766465

# Sample allocations
sampled=606

# Sampling of callers
backtrace_hash=0x13bb,calls=157
backtrace_hash=0x2408,calls=149
backtrace_hash=0xb2a3,calls=14
backtrace_hash=0xb2f0,calls=138
backtrace_hash=0xb470,calls=11
backtrace_hash=0xb4a3,calls=137

# Zone data
64,1,128
128,1,514
256,1,558
512,1,582
1024,1,592
2048,1,599
4096,14,7217
8192,1,606
16384,63,31755
```

The profiler will collect backtraces in order to produce a report about callers into IsoAlloc. This data is helpful for understanding memory allocation patterns in a program.

The 'Zone data' shown above is a simple CSV format that is displaying the size of chunks, the number of zones holding chunks of that size, and the number of times the zone was more than `CHUNK_USAGE_THRESHOLD` % (currently 75%) full when being sampled. In the example above this program was making a high number of 16384, and 4096 byte allocations.

## Profiler Tool

TODO
