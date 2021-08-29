# Heap Allocator Security Feature Comparison

Heap allocators hide incredible complexity behind `malloc` and `free`. They must maintain an acceptable level of performance for various environments with wildly different memory and CPU constraints. All the security checks in the world don't matter if they require performance regressions that slow down your program by orders of magnitude. Striking a balance between performance and security is a requirement if you want people to use your library. Most allocators have some security checks, even if they're poorly implemented and easily bypassed. It's impossible to capture the nuances of how those checks work, the corner cases of when they do and don't apply, or the environment changes that affect their efficacy etc. This table is incomplete at the moment so I welcome pull requests that improve it's accuracy.

:heavy_check_mark: = Yes by default

:heavy_plus_sign: = Yes but requires configuration

:x: = Not available

:grey_question: = Todo


| Security Feature		    | isoalloc         | scudo 		      | mimalloc  	     | tcmalloc      | ptmalloc      | jemalloc      | musl's malloc-ng|
|:-------------------------:|:----------------:|:----------------:|:----------------:|:-------------:|:-------------:|:-------------:|:-----------:|
|Memory Isolation			|:heavy_check_mark:|:heavy_check_mark:|:heavy_check_mark:|:grey_question:|:grey_question:|:grey_question:|:x:|
|Canaries  			        |:heavy_check_mark:|:heavy_check_mark:|:x:   			 |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Guard Pages		   	    |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Randomized Allocations	    |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Pointer Obfuscation        |:heavy_check_mark:|:x:				  |:heavy_plus_sign: |:grey_question:|:grey_question:|:grey_question:|:x:|
|Double Free Detection      |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Chunk Alignment Check      |:heavy_check_mark:|:heavy_check_mark:|:heavy_plus_sign: |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Out Of Band Metadata       |:heavy_check_mark:|:x:				  |:x:				 |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Permanent Frees		    |:heavy_check_mark:|:x:				  |:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Freed Chunk Sanitization   |:heavy_check_mark:|:heavy_check_mark:|:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Adjacent Chunk Verification|:heavy_check_mark:|:heavy_check_mark:|:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Delayed Free    	        |:heavy_check_mark:|:heavy_check_mark:|:x:				 |:grey_question:|:grey_question:|:grey_question:|:heavy_check_mark:|
|Dangling Pointer Detection |:heavy_plus_sign: |:x:				  |:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|GWP-ASAN Like Sampling     |:heavy_plus_sign: |:heavy_plus_sign: |:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Type Mismatch Detection	|:heavy_check_mark:|:heavy_check_mark:|:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Size Mismatch Detection	|:heavy_check_mark:|:heavy_check_mark:|:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|ARM Memory Tagging			|:x:			   |:heavy_check_mark:|:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Zone/Chunk CPU Pinning		|:heavy_check_mark:|:x:				  |:x:				 |:grey_question:|:grey_question:|:grey_question:|:x:|
|Chunk Race Error Detection |:x:			   |:heavy_check_mark:|:grey_question:   |:grey_question:|:grey_question:|:grey_question:|:grey_question:|

**Sources**

https://github.com/struct/IsoAlloc

https://source.android.com/devices/tech/debug/scudo

https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/scudo/standalone

https://www.microsoft.com/en-us/research/uploads/prod/2019/06/mimalloc-tr-v1.pdf

Thanks to [Kostya Kortchinsky](https://twitter.com/@crypt0ad) for reviewing the Scudo column

https://dustri.org/b/security-features-of-musl.html
