## Isolation Alloc Makefile
## Copyright 2020 - chris.rohlf@gmail.com

CC = clang
CXX = clang++

## Security flags can affect performance
## SANITIZE_CHUNKS - Clear user chunks upon free
SECURITY_FLAGS = -DSANITIZE_CHUNKS=0

## Support for threads adds a performance overhead
## You can safely disable it here if you know your
## program does not require concurrent access
## to the IsoAlloc APIs
THREAD_SUPPORT = -DTHREAD_SUPPORT=1 -pthread

## Instructs the kernel (via mmap) to prepopulate
## page tables which will reduce page faults and
## improve performance. If you're using IsoAlloc
## for small short lived programs you probably
## want to disable this
PRE_POPULATE_PAGES = -DPRE_POPULATE_PAGES=1

COMMON_CFLAGS = -Wall -Iinclude/ $(THREAD_SUPPORT) $(PRE_POPULATE_PAGES)
CFLAGS = $(COMMON_CFLAGS) $(SECURITY_FLAGS) -fvisibility=hidden -std=c11
CXXFLAGS = $(COMMON_CFLAGS) -DCPP_SUPPORT -std=c++11
EXE_CFLAGS = -fPIE
OPTIMIZE = -O2
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
	$(CC) $(CFLAGS) $(LIBRARY) $(C_SRCS) $(OPTIMIZE) -o $(BUILD_DIR)/libisoalloc.so
	strip $(BUILD_DIR)/libisoalloc.so

## Build a release version of the library
## Adds malloc hooks
library_hook_malloc: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(MALLOC_HOOK) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so
	strip $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Builds a debug version of the library with scan-build
analyze_library_debug: clean
	scan-build $(CC) $(CFLAGS) $(LIBRARY) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
## Adds malloc hooks
library_debug_hook_malloc: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(MALLOC_HOOK) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug_no_output: clean
	$(CC) $(CFLAGS) $(LIBRARY) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## C++ Support - Build object files for C code
c_library_object:
	$(CC) $(CFLAGS) $(C_SRCS) $(DEBUG_FLAGS) -fPIC -c
	mv *.o $(BUILD_DIR)

## C++ Support - Build the library with C++ support
## including overloaded new/delete operators
cpp_library: clean c_library_object
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(LIBRARY) $(CXX_SRCS) $(MALLOC_HOOK) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the unit test
tests: clean library_debug
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/big_tests.c -o $(BUILD_DIR)/big_tests -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/double_free.c -o $(BUILD_DIR)/double_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/heap_overflow.c -o $(BUILD_DIR)/heap_overflow -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/heap_underflow.c -o $(BUILD_DIR)/heap_underflow -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/interfaces_test.c -o $(BUILD_DIR)/interfaces_test -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/thread_tests.c -o $(BUILD_DIR)/thread_tests -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/leaks_test.c -o $(BUILD_DIR)/leaks_test -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/wild_free.c -o $(BUILD_DIR)/wild_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/unaligned_free.c -o $(BUILD_DIR)/unaligned_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/incorrect_chunk_size_multiple.c -o $(BUILD_DIR)/incorrect_chunk_size_multiple -L$(BUILD_DIR) -lisoalloc
	utils/run_tests.sh

## Build a non-debug library with performance
## monitoring enabled
perf_tests: clean
	$(CC) $(CFLAGS) $(C_SRCS) $(PERF_FLAGS) $(OPTIMIZE) tests/tests.c -o $(BUILD_DIR)/tests_gprof
	$(CC) $(CFLAGS) $(C_SRCS) $(PERF_FLAGS) $(OPTIMIZE) tests/big_tests.c -o $(BUILD_DIR)/big_tests_gprof
	$(BUILD_DIR)/tests_gprof
	gprof -b $(BUILD_DIR)/tests_gprof gmon.out > tests_perf_analysis.txt
	$(BUILD_DIR)/big_tests_gprof
	gprof -b $(BUILD_DIR)/big_tests_gprof gmon.out > big_tests_perf_analysis.txt

## Runs a single test that prints CPU time
## compared to the same malloc/free operations
malloc_cmp_test: clean
	$(CC) $(CFLAGS) $(C_SRCS) $(EXE_CFLAGS) $(OPTIMIZE) tests/tests.c -o $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(C_SRCS) $(EXE_CFLAGS) $(OPTIMIZE) -DMALLOC_PERF_TEST tests/tests.c -o $(BUILD_DIR)/malloc_tests
	echo "Running IsoAlloc Performance Test"
	build/tests
	echo "Running glibc malloc Performance Test"
	build/malloc_tests

## C++ Support - Build a debug version of the unit test
cpp_tests: clean cpp_library
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(EXE_CFLAGS) tests/tests.cpp -o $(BUILD_DIR)/cxx_tests -L$(BUILD_DIR) -lisoalloc
	LD_LIBRARY_PATH=$(BUILD_DIR)/ LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/cxx_tests

install:
	cp -pR build/libisoalloc.so /usr/lib/

format:
	clang-format $(SRC_DIR)/*.* tests/*.* include/*.h -i

clean:
	rm -rf build/* tests_perf_analysis.txt big_tests_perf_analysis.txt gmon.out test_output.txt *.dSYM
	mkdir -p build/
