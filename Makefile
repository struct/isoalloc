## Isolation Alloc Makefile
## Copyright 2021 - chris.rohlf@gmail.com

CC = clang
CXX = clang++

## Security flags can affect performance
## SANITIZE_CHUNKS - Clear user chunks upon free
## FUZZ_MODE - Call verify_all_zones upon alloc/free, never reuse custom zones
## PERM_FREE_REALLOC - Permanently free any realloc'd chunk
## DISABLE_CANARY - Disables the use of canaries, improves performance
SECURITY_FLAGS = -DSANITIZE_CHUNKS=0 -DFUZZ_MODE=0 -DPERM_FREE_REALLOC=0 -DDISABLE_CANARY=0

## This enables Address Sanitizer support for manually
## poisoning and unpoisoning zones. It adds significant
## performance and memory overhead
## This is slow, and it's incompatible with other sanitizers
#ENABLE_ASAN = -fsanitize=address -DENABLE_ASAN=1

## Enable memory sanitizer to catch uninitialized reads.
## This is slow, and it's incompatible with other sanitizers
#ENABLE_MSAN = -fsanitize=memory -fsanitize-memory-track-origins

## Enable undefined behavior sanitizer to catch undefined behavior.
## This is slow, and it's incompatible with other sanitizers
#ENABLE_UBSAN = -fsanitize=undefined

## Enable thread sanitizer. The slowest sanitizer of them
## all. But useful for finding thread related data race issues
## in the allocator in code paths that use atomic_flag
#ENABLE_TSAN = -fsanitize=thread

SANITIZER_SUPPORT = $(ENABLE_ASAN) $(ENABLE_MSAN) $(ENABLE_UBSAN) $(ENABLE_TSAN)

## Support for threads adds a performance overhead
## You can safely disable it here if you know your
## program does not require concurrent access
## to the IsoAlloc APIs
## THREAD_ZONE_CACHE - Enables thread zone cache
THREAD_SUPPORT = -DTHREAD_SUPPORT=1 -pthread -DTHREAD_ZONE_CACHE=1

## This tells IsoAlloc to only start with 4 default zones.
## If you set it to 0 IsoAlloc will startup with 10. The
## performance penalty for setting it to 0 is a one time
## startup cost but more memory may be wasted. See the
## comments in iso_alloc_internal.h for modifying this
STARTUP_MEM_USAGE = -DSMALL_MEM_STARTUP=0

## Instructs the kernel (via mmap) to prepopulate
## page tables which will reduce page faults and
## sometimes improve performance. If you're using
## IsoAlloc for small short lived programs you probably
## want to disable this. This is ignored on MacOS
PRE_POPULATE_PAGES = -DPRE_POPULATE_PAGES=0

## Enable some functionality that like IsoAlloc internals
## for tests that need to verify security properties
UNIT_TESTING = -DUNIT_TESTING=1

## Enable the malloc/free and new/delete hooks
MALLOC_HOOK = -DMALLOC_HOOK=1

## Enable the built-in heap profiler. When this is enabled
## IsoAlloc will write a file to disk upon exit of the
## program. This file encodes the heap usage patterns of
## the target. This file can be consumed by the profiler
## CLI utility. See PROFILER.md for the format of this file
#HEAP_PROFILER = -DHEAP_PROFILER=1 -fno-omit-frame-pointer

## Enable CPU pinning support on a per-zone basis. This is
## a minor security feature which introduces an allocation
## isolation property that is defined by CPU core. See the
## README for more detailed information. (Linux only)
CPU_PIN = -DCPU_PIN=0

## Enable the allocation sanity feature. This works a lot
## like GWP-ASAN does. It samples calls to iso_alloc and
## randomly swaps them out for raw page allocations that
## are surrounded by guard pages. These pages are unmapped
## upon free. Much like GWP-ASAN this is designed to be
## used in production builds and should not incur too
## much of a performance penalty
ALLOC_SANITY = -DALLOC_SANITY=0

## Enable the userfaultfd based uninitialized read detection
## feature. This samples calls to malloc, and allocates raw
## pages of memory with mmap which are registered with the
## userfaultfd subsystem. We detect uninitialized reads by
## looking for the first read access of that page before a
## previous call to write. Think of it as GWP-ASAN but for
## uninitialized reads. Enabling this feature does incur a
## performance penalty. This requires that both ALLOC_SANITY
## and THREAD_SUPPORT are enabled. Linux only
UNINIT_READ_SANITY = -DUNINIT_READ_SANITY=0

## Enable a sampling mechanism that searches for references
## to a chunk currently being freed. The search only overwrites
## the first reference to that chunk because searching all
## zones is very slow.
UAF_PTR_PAGE = -DUAF_PTR_PAGE=0

## Unmap user and bitmap in the destructor. You probably
## don't want this as theres no guarantee the IsoAlloc
## destructor will be called last and other destructors
## that call free will segfault
ISO_DTOR_CLEANUP = -DISO_DTOR_CLEANUP=0

## Verifies the free bit slot cache does not contain duplicate
## entries which might lead to IsoAlloc handing out an in-use
## chunk to a caller. This is a slow search that has a small
## performance penalty
VERIFY_BIT_SLOT_CACHE = -DVERIFY_BIT_SLOT_CACHE=0

## Enable experimental features that are not guaranteed to
## compile, or introduce stability and performance bugs
EXPERIMENTAL = -DEXPERIMENTAL=0

## These control log, memory leak, and memory usage code
## In a release build you probably want them all to be 0
DEBUG_LOG_FLAGS = -DDEBUG=1 -DLEAK_DETECTOR=1 -DMEM_USAGE=1

## On Android we use prctl to name mappings so they are
## visible in /proc/pid/maps - But the Android build does
## not use this Makefile. You want to modify Android.mk
NAMED_MAPPINGS = -DNAMED_MAPPINGS=0

## Abort when the allocator cannot return a valid chunk
ABORT_ON_NULL = -DABORT_ON_NULL=0

## Enable protection against misusing 0 sized allocations
NO_ZERO_ALLOCATIONS = -DNO_ZERO_ALLOCATIONS=1

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
OS_FLAGS = -framework Security
CPU_PIN = ""
endif

ifeq ($(UNAME), Linux)
STRIP = strip -s $(BUILD_DIR)/libisoalloc.so
endif

HOOKS = $(MALLOC_HOOK)
OPTIMIZE = -O2 -fstrict-aliasing -Wstrict-aliasing
COMMON_CFLAGS = -Wall -Iinclude/ $(THREAD_SUPPORT) $(PRE_POPULATE_PAGES) $(STARTUP_MEM_USAGE)
BUILD_ERROR_FLAGS = -Werror -pedantic -Wno-pointer-arith -Wno-gnu-zero-variadic-macro-arguments -Wno-format-pedantic
CFLAGS = $(COMMON_CFLAGS) $(SECURITY_FLAGS) $(BUILD_ERROR_FLAGS) $(HOOKS) $(HEAP_PROFILER) -fvisibility=hidden \
	-std=c11 $(SANITIZER_SUPPORT) $(ALLOC_SANITY) $(UNINIT_READ_SANITY) $(CPU_PIN) $(EXPERIMENTAL) $(UAF_PTR_PAGE) \
	$(VERIFY_BIT_SLOT_CACHE) $(NAMED_MAPPINGS) $(ABORT_ON_NULL) $(NO_ZERO_ALLOCATIONS)
CXXFLAGS = $(COMMON_CFLAGS) -DCPP_SUPPORT=1 -std=c++17 $(SANITIZER_SUPPORT) $(HOOKS)
EXE_CFLAGS = -fPIE
GDB_FLAGS = -g -ggdb3 -fno-omit-frame-pointer
PERF_FLAGS = -pg -DPERF_TEST_BUILD=1
LIBRARY = -fPIC -shared
SRC_DIR = src
C_SRCS = $(SRC_DIR)/*.c
CXX_SRCS = $(SRC_DIR)/*.cpp
ISO_ALLOC_PRINTF_SRC = $(SRC_DIR)/iso_alloc_printf.c
BUILD_DIR = build
LDFLAGS = -L$(BUILD_DIR) -lisoalloc

all: library tests

## Build a release version of the library
library: clean
	@echo "make library"
	$(CC) $(CFLAGS) $(LIBRARY) $(OPTIMIZE) $(OS_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so
	$(STRIP)

## Build a debug version of the library
library_debug: clean
	@echo "make library debug"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
## specifically for unit tests
library_debug_unit_tests: clean
	@echo "make library_debug_unit_tests"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(UNIT_TESTING) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Builds a debug version of the library with scan-build
## Requires scan-build is in your PATH
analyze_library_debug: clean
	@echo "make analyze_library_debug"
	scan-build $(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug_no_output: clean
	@echo "make library_debug_no_output"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## C++ Support - Build object files for C code
c_library_objects:
	@echo "make c_library_objects"
	$(CC) $(CFLAGS) $(OPTIMIZE) $(C_SRCS) -fPIC -c
	mv *.o $(BUILD_DIR)

## C++ Support - Build debug object files for C code
c_library_objects_debug:
	@echo "make c_library_objects_debug"
	$(CC) $(CFLAGS) $(C_SRCS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) -fPIC -c
	mv *.o $(BUILD_DIR)

## C++ Support - Build the library with C++ support
cpp_library: clean c_library_objects
	@echo "make cpp_library"
	$(CXX) $(CXXFLAGS) $(OPTIMIZE) $(LIBRARY) $(OS_FLAGS) $(CXX_SRCS) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so
	$(STRIP)

cpp_library_debug: clean c_library_objects_debug
	@echo "make cpp_library_debug"
	$(CXX) $(CXXFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(LIBRARY) $(OS_FLAGS) $(CXX_SRCS) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the unit test
tests: clean library_debug_unit_tests
	@echo "make library_debug_unit_tests"
	#$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/uaf.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/uaf
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/interfaces_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/interfaces_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/thread_tests.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/thread_tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(UNIT_TESTING) tests/big_canary_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/big_canary_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/tests.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/big_tests.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/big_tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/double_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/double_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/big_double_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/big_double_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/heap_overflow.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/heap_overflow $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/heap_underflow.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/heap_underflow $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/leaks_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/leaks_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/wild_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/wild_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/unaligned_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/unaligned_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/incorrect_chunk_size_multiple.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/incorrect_chunk_size_multiple $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/zero_alloc.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/zero_alloc $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/uninit_read.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/uninit_read $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/zero_alloc.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/zero_alloc $(LDFLAGS)
	utils/run_tests.sh

fuzz_test: clean library_debug_unit_tests
	@echo "make fuzz_test"
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) -DNEVER_REUSE_ZONES=1 tests/alloc_fuzz.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/alloc_fuzz $(LDFLAGS)

	LD_LIBRARY_PATH=build/ build/alloc_fuzz

## Build a non-debug library with performance
## monitoring enabled. Linux only
perf_tests: clean
	@echo "make perf_tests"
	$(CC) $(CFLAGS) $(C_SRCS) $(GDB_FLAGS) $(PERF_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests_gprof
	$(CC) $(CFLAGS) $(C_SRCS) $(GDB_FLAGS) $(PERF_FLAGS) tests/big_tests.c -o $(BUILD_DIR)/big_tests_gprof
	$(BUILD_DIR)/tests_gprof
	gprof -b $(BUILD_DIR)/tests_gprof gmon.out > tests_perf_analysis.txt
	$(BUILD_DIR)/big_tests_gprof
	gprof -b $(BUILD_DIR)/big_tests_gprof gmon.out > big_tests_perf_analysis.txt

## Runs a single test that prints CPU time
## compared to the same malloc/free operations
malloc_cmp_test: clean
	@echo "make malloc_cmp_test"
	$(CC) $(CFLAGS) $(C_SRCS) $(OPTIMIZE) $(EXE_CFLAGS) $(OS_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(OPTIMIZE) $(EXE_CFLAGS) $(OS_FLAGS) -DMALLOC_PERF_TEST $(ISO_ALLOC_PRINTF_SRC) tests/tests.c -o $(BUILD_DIR)/malloc_tests
	echo "Running IsoAlloc Performance Test"
	build/tests
	echo "Running glibc malloc Performance Test"
	build/malloc_tests

## C++ Support - Build a debug version of the unit test
cpp_tests: clean cpp_library_debug
	@echo "make cpp_tests"
	$(CXX) $(CXXFLAGS) $(DEBUG_LOG_FLAGS) $(EXE_CFLAGS) tests/tests.cpp $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/cxx_tests $(LDFLAGS)
	LD_LIBRARY_PATH=$(BUILD_DIR)/ LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/cxx_tests

install:
	cp -pR build/libisoalloc.so /usr/lib/

format:
	clang-format $(SRC_DIR)/*.* tests/*.* include/*.h -i

clean:
	rm -rf build/* tests_perf_analysis.txt big_tests_perf_analysis.txt gmon.out test_output.txt *.dSYM core* profiler.data
	mkdir -p build/
