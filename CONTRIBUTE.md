# Hacking

If you're looking to contribute to IsoAlloc then you will want to start with this guide. It contains some steps you will want to follow before making a pull request, and a basic style guide.

## The Process

Contributing to IsoAlloc is a pretty standard process of forking the repo, making a pull request, and optionally linking it to an existing issue. Before you make your pull request please run the following commands on both Linux and MacOS:

`clang format` - Run the clang formatter to ensure your code conforms to the rest of the project

`make tests` - Make sure all tests still pass

`make malloc_cmp_test` - Check for major performance regressions

`make library` - Make sure you can still build a release version of the library

If you're making changes to the C++ wrapper you will want to run those build targets and test with `make cpp_tests` as well.

## Style Guide

The clang-format makefile target should cleanup a lot of your commit but please ensure you conform to the following style guide:

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
