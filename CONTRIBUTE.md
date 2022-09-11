# Contributing

If you're looking to contribute to IsoAlloc then you will want to start with this guide. It contains some steps you will want to follow before making a pull request, and a basic style guide.

## Testing Your Changes

Contributing to IsoAlloc is a pretty standard process of forking the repo, making a pull request, and optionally linking it to an existing issue. Before you make your pull request please run the following commands on both Linux and MacOS:

`make tests` - Make sure all tests still pass

`make malloc_cmp_test` - Check for major performance regressions

`make cpp_tests` - Make sure all C++ tests still pass

`make library` - Build a release version of the library

`make cpp_library` - Build a release version of the library with C++ support

Repeat the steps above using gcc/g++ as your compiler. e.g. `make tests CC=gcc CXX=g++`

Compile a debug version of the library with `make cpp_library_debug` and then run a basic test using `LD_PRELOAD` and another binary.

If you're making changes that are handled differently between Clang and GCC then please run the tests above but also set the `CC` and `CXX` environment variables approriately.

## Style Guide

Before you make a PR please run the following:

`make format` - Run the clang formatter to ensure your changes conform to the rest of the project style

The clang-format Makefile target should cleanup a lot of your commit but please ensure you conform to the following style guide:

- Open braces on same line as if/function start
- No space between if conditional and parantheses
- Use a define for any int or string constants
- Comments should be C style unless its a .cpp file
- Declare counter local types within for loop declarations if possible

```
    /* Check the value of some flag */
    if(flag == SOME_VALUE) {
        ...
    }

    for(uint64_t i = 0; i < canary_count; i++) {
        ...
    }
```
