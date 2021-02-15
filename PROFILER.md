# IsoAlloc Heap Profiler

Different workloads can affect the performance of IsoAlloc. For example, if your program only ever makes allocations between 32 and 512 bytes then theres no reason to allocate zones for sizes above 512 bytes. Other workloads might make a lot of short lived allocations of one size. By sampling allocations from this target we can produce data that results in a better understanding of memory usage through the programs lifetime. This data can be used to generate more efficient configurations of IsoAlloc.

The heap profiler is designed to sample heap allocations over time across your workload. This profiler emits a machine readable file throughout the lifetime of the sampled target. These files are then passed to the profiler tool where they are merged. After merging the profiler tool will emit a C header file `iso_alloc_target_config.h`. This tooling is still under development.

## Profiler Tuning

Currently theres only one way to tune the profiler and thats by changing `PROFILER_ODDS` or `CHUNK_USAGE_THRESHOLD`. These control the rate at which we sample allocations and the % a zone must be full before being recorded as such.

## Profiler Output Format

The profiler outputs a file (example below) that contains information about the state of the IsoAlloc managed heap. This information is captured by sampling allocations during runtime and when the process is exiting.

```
# Total allocations
allocated=5766464
# Sample allocations, % of total allocations sampled
sampled=42796,1
# Zone data
64,1,122
128,1,482
256,1,524
512,1,545
1024,1,564
2048,1,572
4096,14,6811
8192,1,573
16384,63,30208
```

The 'Zone data' shown above is a simple CSV format that is displaying the size of chunks, the number of zones holding chunks of that size, and the number of times the zone was more than `CHUNK_USAGE_THRESHOLD` % (currently 75%) full when being sampled. In the example above this program was making a high number of 16384, and 4096 byte allocations.

## Profiler Tool

TODO
