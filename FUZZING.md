# Fuzzing

[Fuzzing](https://en.wikipedia.org/wiki/Fuzzing) is the automated process of discovering realibility bugs and security vulnerabilities in software by creating random or structure aware inputs that a program was not built to handle. Fuzzing is extremely powerful at discovering subtle issues in your code but it requires a comprehensive set of tools and instrumentation in order to be successful. Runtime instrumentation like Address Sanitizer and Memory Sanitizer have enabled even the simplest of fuzzers to uncover these kinds of issues.

## Fuzzing with IsoAlloc

Memory allocation libraries like IsoAlloc can also help uncover security vulnerabilities by enabling the right configurations and calling the right APIs. It's also useful for fuzzing in environments where the sanitizers cannot easily run, or when the target is binary-only. A good example of this is [libdislocator](https://github.com/mirrorer/afl/tree/master/libdislocator) which has shipped with the AFL fuzzer for many years. If you're finding value in libdislocator then switching to IsoAlloc will likely uncover even more issues for you. You can do this today by simply using `LD_PRELOAD=libisoalloc.so` and starting your fuzzer.

## Fuzzing Configuration

Below is a list of configurations which will help you get the most out of fuzzing with IsoAlloc. Each of these can be enabled at compile time by modifying the Makefile. All of these flags should be documented in the [README](README.md#security-properties). 

* FUZZ_MODE - Verifies the internal state of all zones on each alloc/free operation
* SANITIZE_CHUNKS - Over write chunk contents upon free with 0xDE
* PERM_FREE_REALLOC - Any chunk passed to realloc will be permanently free'd
* ALLOC_SANITY - Samples allocations and places them on individual pages to detection UAF and out of bounds r/w
* UAF_PTR_PAGE - Calls to `iso_free` will be sampled to search for dangling references to the chunk being free'd
* ABORT_ON_NULL - Aborts instead of returning NULL whenever malloc() fails
