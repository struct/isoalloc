## Isolation Alloc Makefile
## Copyright 2020 - chris.rohlf@gmail.com

CC = clang
CXX = clang++
COMMON_CFLAGS = -Wall -O2 -Iinclude/ -pthread
CFLAGS =  $(COMMON_CFLAGS) -fvisibility=hidden -std=c11
CXXFLAGS = $(COMMON_CFLAGS) -DCPP_SUPPORT -std=c++11
EXE_CFLAGS = -fPIE
DEBUG_FLAGS = -DDEBUG -DLEAK_DETECTOR -DMEM_USAGE
GDB_FLAGS = -g -ggdb3
PERF_FLAGS = -pg -DPERF_BUILD
MALLOC_HOOK = -DMALLOC_HOOK
LIBRARY = -fPIC -shared
SRC_DIR = src
C_SRCS = $(SRC_DIR)/*.c
CXX_SRCS = $(SRC_DIR)/*.cpp
BUILD_DIR = build

all: library tests

## Build a release version of the library
library: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so
	strip $(BUILD_DIR)/libisoalloc.so

## Build a release version of the library
## Adds malloc hooks
library_hook_malloc: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(MALLOC_HOOK) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so
	strip $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
## Adds malloc hooks
library_debug_hook_malloc: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(MALLOC_HOOK) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug_no_output: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the unit test
tests: clean library_debug
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/double_free.c -o $(BUILD_DIR)/double_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/heap_overflow.c -o $(BUILD_DIR)/heap_overflow -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/heap_underflow.c -o $(BUILD_DIR)/heap_underflow -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/interfaces_test.c -o $(BUILD_DIR)/interfaces_test -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/thread_tests.c -o $(BUILD_DIR)/thread_tests -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/leaks_test.c -o $(BUILD_DIR)/leaks_test -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/wild_free.c -o $(BUILD_DIR)/wild_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/unaligned_free.c -o $(BUILD_DIR)/unaligned_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/incorrect_chunk_size_multiple.c -o $(BUILD_DIR)/incorrect_chunk_size_multiple -L$(BUILD_DIR) -lisoalloc
	utils/run_tests.sh

## Build a non-debug library with performance
## monitoring enabled
perf_tests: clean
	$(CC) $(CFLAGS) $(C_SRCS) $(PERF_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests_gprof
	$(BUILD_DIR)/tests_gprof
	gprof -b $(BUILD_DIR)/tests_gprof gmon.out > perf_analysis.txt

## C++ Support - Build object files for C code
c_library_object:
	$(CC) $(CFLAGS) $(C_SRCS) $(DEBUG_FLAGS) -fPIC -c
	mv *.o $(BUILD_DIR)

## C++ Support - Build the library with C++ support
## including overloaded new/delete operators
cpp_library: clean c_library_object
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(LIBRARY) $(CXX_SRCS) $(MALLOC_HOOK) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so

## C++ Support - Build a debug version of the unit test
cpp_tests: clean cpp_library
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(EXE_CFLAGS) tests/tests.cpp -o $(BUILD_DIR)/cxx_tests -L$(BUILD_DIR) -lisoalloc
	LD_LIBRARY_PATH=$(BUILD_DIR)/ LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/cxx_tests

install:
	cp -pR build/libisoalloc.so /usr/lib/

format:
	clang-format $(SRC_DIR)/*.* tests/*.* include/*.h -i

clean:
	rm -rf build/* perf_analysis.txt gmon.out test_output.txt *.dSYM
	mkdir -p build/
