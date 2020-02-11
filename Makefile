## Isolation Alloc Makefile
## Copyright 2020 - chris.rohlf@gmail.com

CC = clang
CFLAGS = -Wall -Werror -std=c11 -fvisibility=hidden -O2 -Iinclude/
HOOK_MALLOC = -DMALLOC_HOOK=1
EXE_CFLAGS = -fPIE
DEBUG_FLAGS = -DDEBUG -DLEAK_DETECTOR -DMEM_USAGE
GDB_FLAGS = -g -ggdb3
PERF_FLAGS = -pg -DPERF_BUILD
THREAD_FLAGS = -DTHREAD_SUPPORT -lpthread
LIBRARY = -fPIC -shared
SRC_DIR = src
SRCS = $(SRC_DIR)/*.c
TEST_DIR = tests
TEST_SRCS = $(TEST_DIR)/*.c
BUILD_DIR = build

## Build the library and tests
all: library tests

## Build the library
library: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(LIBRARY) $(SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build the library and hook malloc
library_hook_malloc: clean
	$(CC) $(CFLAGS) $(HOOK_MALLOC) $(THREAD_FLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) $(LIBRARY) $(SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(LIBRARY) $(DEBUG_FLAGS) $(GDB_FLAGS) $(SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug_no_output: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(LIBRARY) $(GDB_FLAGS) $(SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the unit test
tests: clean library_debug
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(TEST_SRCS) -o $(BUILD_DIR)/tests -L$(BUILD_DIR) -lisoalloc
	LD_LIBRARY_PATH=$(BUILD_DIR)/ LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/tests

## Build a debug version of the unit test
static_tests: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) -o $(BUILD_DIR)/static_tests $(SRCS) $(TEST_SRCS)
	$(BUILD_DIR)/static_tests

malloc_tests: clean
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(TEST_SRCS) -DUSE_MALLOC=1 -o $(BUILD_DIR)/malloc_tests
	$(BUILD_DIR)/malloc_tests

## Build both tests and library with debug
## symbols but no log output
tests_no_output: clean library_debug_no_output
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(GDB_FLAGS) $(TEST_SRCS) -o tests -L$(BUILD_DIR) -lisoalloc
	LD_LIBRARY_PATH=$(BUILD_DIR) LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/tests

## Build a non-debug version with the allocator
## built in and record performance information
perf_tests: clean
	$(CC) $(CFLAGS) $(SRCS) $(TEST_SRCS) $(PERF_FLAGS) -o $(BUILD_DIR)/tests_gprof
	$(BUILD_DIR)/tests_gprof
	gprof -b $(BUILD_DIR)/tests_gprof gmon.out > perf_analysis.txt

format:
	clang-format $(SRC_DIR)/*.c $(TEST_DIR)/*.c include/*.h -i

clean:
	rm -rf build/* perf_analysis.txt gmon.out *.dSYM
