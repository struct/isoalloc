## Isolation Alloc Makefile
## Copyright 2020 - chris.rohlf@gmail.com

CC = clang
CXX = clang++
COMMON_CFLAGS = -Wall -O2 -Iinclude/
CFLAGS =  $(COMMON_CFLAGS) -fvisibility=hidden -std=c11
CXXFLAGS = $(COMMON_CFLAGS) -DCPP_SUPPORT=1 -std=c++11
EXE_CFLAGS = -fPIE
DEBUG_FLAGS = -DDEBUG -DLEAK_DETECTOR -DMEM_USAGE
GDB_FLAGS = -g -ggdb3
PERF_FLAGS = -pg -DPERF_BUILD
THREAD_FLAGS = -DTHREAD_SUPPORT -lpthread
MALLOC_HOOK = -DMALLOC_HOOK=1
LIBRARY = -fPIC -shared
SRC_DIR = src
C_SRCS = $(SRC_DIR)/*.c
CXX_SRCS = $(SRC_DIR)/*.cpp
BUILD_DIR = build

all: library tests

## Build a release version of the library
## Adds malloc hooks
library: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(LIBRARY) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so
	strip $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
## Does not compile malloc hooks
library_debug: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(LIBRARY) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug_no_output: clean
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(LIBRARY) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the unit test
tests: clean library_debug
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/double_free.c -o $(BUILD_DIR)/double_free -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/heap_overflow.c -o $(BUILD_DIR)/heap_overflow -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/heap_underflow.c -o $(BUILD_DIR)/heap_underflow -L$(BUILD_DIR) -lisoalloc
	$(CC) $(CFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/interfaces_test.c -o $(BUILD_DIR)/interfaces_test -L$(BUILD_DIR) -lisoalloc
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
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(THREAD_FLAGS) $(LIBRARY) $(CXX_SRCS) $(MALLOC_HOOK) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so

## C++ Support - Build a debug version of the unit test
cpp_tests: clean cpp_library
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) tests/tests.cpp -o $(BUILD_DIR)/cxx_tests -L$(BUILD_DIR) -lisoalloc
	LD_LIBRARY_PATH=$(BUILD_DIR)/ LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/cxx_tests

format:
	clang-format $(SRC_DIR)/*.* tests/*.* include/*.h -i

clean:
	rm -rf build/* perf_analysis.txt gmon.out test_output.txt *.dSYM
