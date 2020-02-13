#!/bin/sh
# This script runs all debug tests including vulnerable
# examples of code that should crash

LD_LIBRARY_PATH=build/ LD_PRELOAD=build/libisoalloc.so build/tests
LD_LIBRARY_PATH=build/ LD_PRELOAD=build/libisoalloc.so build/double_free
LD_LIBRARY_PATH=build/ LD_PRELOAD=build/libisoalloc.so build/heap_overflow
LD_LIBRARY_PATH=build/ LD_PRELOAD=build/libisoalloc.so build/heap_underflow
