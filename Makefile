## IsoAlloc Makefile
## Copyright 2023 - chris.rohlf@gmail.com

CC = clang
CXX = clang++

## DISABLE_CANARY - Disables the use of canaries, improves performance
DISABLE_CANARY = -DDISABLE_CANARY=0

## Clear user chunks upon free
SANITIZE_CHUNKS = -DSANITIZE_CHUNKS=0

## Call verify_all_zones upon alloc/free, never reuse private zones
## Adds significant performance over head
FUZZ_MODE = -DFUZZ_MODE=0

## Permanently free any realloc'd chunk
PERM_FREE_REALLOC = -DPERM_FREE_REALLOC=0

## Enable memory tagging support. This will generate a random
## 1 byte tag per addressable chunk of memory. These tags can
## be retrieved and verified. This feature will likely interfere
## with ARM MTE and PAC. See MEMORY_TAGGING.md for more information
MEMORY_TAGGING = -DMEMORY_TAGGING=0

## Enable abort() when isoalloc can't gather enough entropy.
ABORT_NO_ENTROPY = -DABORT_NO_ENTROPY=1

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
THREAD_SUPPORT = -DTHREAD_SUPPORT=1 -pthread

## By default IsoAlloc uses a pthread mutex to synchronize
## thread safe access to the root structure. By enabling this
## IsoAlloc will use a C11 atomic spinlock
USE_SPINLOCK = -DUSE_SPINLOCK=0

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
## for tests that need to verify security properties.
## Only test code should use this.
UNIT_TESTING = -DUNIT_TESTING=1

## Enable the malloc/free and new/delete hooks
MALLOC_HOOK = -DMALLOC_HOOK=1

## Use Huge pages for any allocation that is a multiple
## of 2 megabytes. See PERFORMANCE.md for additional info
HUGE_PAGES = -DHUGE_PAGES=1

## Enable the built-in heap profiler. When this is enabled
## IsoAlloc will write a file to disk upon exit of the
## program. This file encodes the heap usage patterns of
## the target. This file can be consumed by the profiler
## CLI utility. See PROFILER.md for the format of this file
#HEAP_PROFILER = -DHEAP_PROFILER=1 -fno-omit-frame-pointer \
#				-fno-optimize-sibling-calls -ldl

## Enable CPU pinning support on a per-zone basis. This is
## a minor security feature which introduces an allocation
## isolation property that is defined by CPU core. See the
## README for more detailed information. This is Linux only
## and has negative performance implications
CPU_PIN = -DCPU_PIN=0
SCHED_GETCPU =

## Enable the allocation sanity feature. This works a lot
## like GWP-ASAN does. It samples calls to iso_alloc and
## randomly swaps them out for raw page allocations that
## are surrounded by guard pages. These pages are unmapped
## upon free. Much like GWP-ASAN this is designed to be
## used in production builds and should not incur too
## much of a performance penalty
ALLOC_SANITY = -DALLOC_SANITY=0

## Enable hooking of memcpy/memmove/memset to detect out of bounds
## r/w operations on chunks allocated with IsoAlloc. Does
## not require ALLOC_SANITY is enabled. On MacOS you need
## to set FORTIFY_SOURCE to 0. Leave these commented if
## you aren't enabling them.
MEMCPY_SANITY = -DMEMCPY_SANITY=0
MEMSET_SANITY = -DMEMSET_SANITY=0

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

## By default IsoAlloc may select a zone that holds chunks
## that are larger than were requested. This is intended
## to reduce memory consumption and is only done for smaller
## sizes. Enabling this feature configures IsoAlloc to only
## use zones that are a perfect fit for the requested size
## once its been rounded up to the next power of 2
STRONG_SIZE_ISOLATION = -DSTRONG_SIZE_ISOLATION=0

## Enable a sampling mechanism that searches for references
## to a chunk currently being freed. The search only overwrites
## the first reference to that chunk because searching all
## zones is very slow.
UAF_PTR_PAGE = -DUAF_PTR_PAGE=0

## Verifies the free bit slot cache does not contain duplicate
## entries which might lead to IsoAlloc handing out an in-use
## chunk to a caller. This is a slow search that has a small
## performance penalty
VERIFY_FREE_BIT_SLOTS = -DVERIFY_FREE_BIT_SLOTS=0

## Randomizes the free bit slot list upon creation. This can
## impact perf. You can control the minimum size of the list
## to be randomized with MIN_RAND_FREELIST in conf.h
RANDOMIZE_FREELIST = -DRANDOMIZE_FREELIST=1

## Enable experimental features that are not guaranteed to
## compile, or introduce stability and performance bugs
EXPERIMENTAL = -DEXPERIMENTAL=0

## These control log, memory leak, and memory usage code
## In a release build they are all set to 0
DEBUG_LOG_FLAGS = -DDEBUG=1 -DLEAK_DETECTOR=1 -DMEM_USAGE=1

## On Android we use prctl to name mappings so they are
## visible in /proc/pid/maps - But the Android build does
## not use this Makefile. You want to modify Android.mk
NAMED_MAPPINGS = -DNAMED_MAPPINGS=0

## Abort when the allocator cannot return a valid chunk
ABORT_ON_NULL = -DABORT_ON_NULL=0

## Enable protection against misusing 0 sized allocations
NO_ZERO_ALLOCATIONS = -DNO_ZERO_ALLOCATIONS=1

## Enables mlock() on performance sensitive pages. For lower
## end devices with less memory you may want to disable this
USE_MLOCK = -DUSE_MLOCK=1

## When enabled the default library constructor and destructor
## will be used to initialize and tear down the library. If
## you disable this you can call iso_alloc_initialize and
## iso_alloc_destroy to perform the same functions. Note if
## you disable this you will want to enable ISO_DTOR_CLEANUP
AUTO_CTOR_DTOR = -DAUTO_CTOR_DTOR=1

## Unmap user and bitmap in the destructor. You probably
## don't want this as theres no guarantee the IsoAlloc
## destructor will be called last and other destructors
## may reference objects within these pages
ISO_DTOR_CLEANUP = -DISO_DTOR_CLEANUP=0

## Register a signal handler for SIGSEGV that inspects si_addr
## for an address managed by IsoAlloc. Optionally used when
## UAF_PTR_PAGE is enabled for better crash handling
SIGNAL_HANDLER = -DSIGNAL_HANDLER=0

## If you know your target will have an ARMv8.1-A or newer and
## supports Top Byte Ignore (TBI) then you want to enable this
ARM_TBI = 0

## Enables guard pages around big zone meta data. Big zones
## are zones with a size greater than SMALL_SIZE_MAX as defined
## in conf.h. Disabling this will save 2 pages per big zone
## allocation. On systems with pages > 4k this results in
## substantial memory usage
BIG_ZONE_META_DATA_GUARD = -DBIG_ZONE_META_DATA_GUARD=0

## Enables guard pages around big zone user pages. On systems
## with pages > 4k this results in substantial memory usage
BIG_ZONE_GUARD = -DBIG_ZONE_GUARD=0

## Ensures all big zone user data pages are marked PROT_NONE
## while on the free list to catch UAF. This is disabled because
## it incurs a syscall (mprotect) per call to free and again
## upon reuse to mark them R/W again.
PROTECT_FREE_BIG_ZONES = -DPROTECT_FREE_BIG_ZONES=0

## By default IsoAlloc will mask pointers to protect against
## certain memory disclosures. This can be beneficial but also
## incurs a small performance cost
MASK_PTRS = -DMASK_PTRS=1

## We start with the standard C++ specifics but giving
## the liberty to choose the gnu++* variants and/or
## higher than C++17
STDCXX=c++17

## Ditto but with C, now modern compilers support -std=c2x
STDC=c11

LTO = -flto

LIBNAME = libisoalloc.so

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
OS_FLAGS = -framework Security
LIBNAME = libisoalloc.dylib
ifeq ($(MEMCPY_SANITY), -DMEMCPY_SANITY=1)
CFLAGS += -D_FORTIFY_SOURCE=0
endif
ifeq ($(MEMSET_SANITY), -DMEMSET_SANITY=1)
CFLAGS += -D_FORTIFY_SOURCE=0
endif
endif

ifeq ($(UNAME), Linux)
STRIP = strip -s $(BUILD_DIR)/$(LIBNAME)
SCHED_GETCPU = -DSCHED_GETCPU
endif

ifeq ($(UNAME), FreeBSD)
STRIP = strip -s $(BUILD_DIR)/$(LIBNAME)
## Using spinlocks to avoid recursive locks contentions with calloc
USE_SPINLOCK = -DUSE_SPINLOCK=1
## Once FreeBSD 13.1 becomes the minimal non EOL version
## it can be enabled
## SCHED_GETCPU = -DSCHED_GETCPU
endif

ifeq ($(UNAME), DragonFly)
STRIP = strip -s $(BUILD_DIR)/$(LIBNAME)
USE_SPINLOCK = -DUSE_SPINLOCK=1
HUGE_PAGES =
OS_FLAGS = -I/usr/local/cxx_atomics
endif

ifeq ($(UNAME), NetBSD)
STRIP = strip -s $(BUILD_DIR)/$(LIBNAME)
HUGE_PAGES =
endif

ifeq ($(UNAME), SunOS)
STRIP = strip -s $(BUILD_DIR)/$(LIBNAME)
# this platform is both 32 and 64 bits
# we have to be explicit
CFLAGS += -m64
CXXFLAGS += -m64
LTO =
HUGE_PAGES =
endif

HOOKS = $(MALLOC_HOOK)
OPTIMIZE = -O2 -fstrict-aliasing -Wstrict-aliasing
COMMON_CFLAGS = -Wall -Iinclude/ $(THREAD_SUPPORT) $(PRE_POPULATE_PAGES) $(STARTUP_MEM_USAGE)
BUILD_ERROR_FLAGS = -Wno-pointer-arith -Wno-gnu-zero-variadic-macro-arguments -Wno-format-pedantic
ifneq ($(CC), gcc)
BUILD_ERROR_FLAGS := $(BUILD_ERROR_FLAGS) -Werror -pedantic
else
BUILD_ERROR_FLAGS := $(BUILD_ERROR_FLAGS) -Wno-attributes -Wno-unused-variable
endif
CFLAGS += $(COMMON_CFLAGS) $(DISABLE_CANARY) $(BUILD_ERROR_FLAGS) $(HOOKS) $(HEAP_PROFILER) -fvisibility=hidden \
	-std=$(STDC) $(SANITIZER_SUPPORT) $(ALLOC_SANITY) $(MEMCPY_SANITY) $(UNINIT_READ_SANITY) $(CPU_PIN) $(SCHED_GETCPU) \
	$(EXPERIMENTAL) $(UAF_PTR_PAGE) $(VERIFY_FREE_BIT_SLOTS) $(NAMED_MAPPINGS) $(ABORT_ON_NULL) $(NO_ZERO_ALLOCATIONS) \
	$(ABORT_NO_ENTROPY) $(ISO_DTOR_CLEANUP) $(RANDOMIZE_FREELIST) $(USE_SPINLOCK) $(HUGE_PAGES) $(USE_MLOCK) \
	$(MEMORY_TAGGING) $(STRONG_SIZE_ISOLATION) $(MEMSET_SANITY) $(AUTO_CTOR_DTOR) $(SIGNAL_HANDLER) \
	$(BIG_ZONE_META_DATA_GUARD) $(BIG_ZONE_GUARD) $(PROTECT_UNUSED_BIG_ZONE) $(MASK_PTRS) $(SANITIZE_CHUNKS) $(FUZZ_MODE) \
	$(PERM_FREE_REALLOC)
CXXFLAGS = $(COMMON_CFLAGS) -DCPP_SUPPORT=1 -std=$(STDCXX) $(SANITIZER_SUPPORT) $(HOOKS)

EXE_CFLAGS = -fPIE
GDB_FLAGS = -g -ggdb3 -fno-omit-frame-pointer
PERF_FLAGS = -pg -DPERF_TEST_BUILD=1
LIBRARY = -fPIC -shared
SRC_DIR = src
C_SRCS = $(SRC_DIR)/*.c
CXX_SRCS = $(SRC_DIR)/*.cpp
ISO_ALLOC_PRINTF_SRC = $(SRC_DIR)/iso_alloc_printf.c
BUILD_DIR = build
LDFLAGS = -L$(BUILD_DIR) -lisoalloc $(LTO)

all: tests

## Build a release version of the library
library: clean
	@echo "make library"
	$(CC) $(CFLAGS) $(LIBRARY) $(OPTIMIZE) $(OS_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/$(LIBNAME)
	$(STRIP)

## Build a debug version of the library
library_debug: clean
	@echo "make library debug"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/$(LIBNAME)

## Build a debug version of the library
## specifically for unit tests
library_debug_unit_tests: clean
	@echo "make library_debug_unit_tests"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(UNIT_TESTING) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/$(LIBNAME)

## Builds a debug version of the library with scan-build
## Requires scan-build is in your PATH
analyze_library_debug: clean
	@echo "make analyze_library_debug"
	scan-build $(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/$(LIBNAME)

## Build a debug version of the library
library_debug_no_output: clean
	@echo "make library_debug_no_output"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/$(LIBNAME)

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
	$(CXX) $(CXXFLAGS) $(OPTIMIZE) $(LIBRARY) $(OS_FLAGS) $(CXX_SRCS) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/$(LIBNAME)
	$(STRIP)

cpp_library_debug: clean c_library_objects_debug
	@echo "make cpp_library_debug"
	$(CXX) $(CXXFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(LIBRARY) $(OS_FLAGS) $(CXX_SRCS) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/$(LIBNAME)

## Build a debug version of the unit test
tests: clean library_debug_unit_tests
	@echo "make tests"
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/rand_freelist.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/rand_freelist $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/tests.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) $(UNIT_TESTING) tests/uaf.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/uaf $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/interfaces_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/interfaces_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/thread_tests.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/thread_tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) $(UNIT_TESTING) tests/big_canary_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/big_canary_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/big_tests.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/big_tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/double_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/double_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/big_double_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/big_double_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/heap_overflow.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/heap_overflow $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/heap_underflow.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/heap_underflow $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/leaks_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/leaks_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/wild_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/wild_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/unaligned_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/unaligned_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/incorrect_chunk_size_multiple.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/incorrect_chunk_size_multiple $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/zero_alloc.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/zero_alloc $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/uninit_read.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/uninit_read $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/sized_free.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/sized_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/pool_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/pool_test $(LDFLAGS)
	utils/run_tests.sh

tagging_tests: clean cpp_library_debug
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/tagged_ptr_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/tagged_ptr_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/uaf_tag_ptr_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/uaf_tag_ptr_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/bad_tag_ptr_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/bad_tag_ptr_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/verify_tag_ptr_test.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/verify_tag_ptr_test $(LDFLAGS)
	$(CXX) -DMEMORY_TAGGING=1 $(CXXFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(EXE_CFLAGS) $(OS_FLAGS) tests/tagged_ptr_test.cpp $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/tagged_ptr_test_cpp $(LDFLAGS)
	utils/run_tagging_tests.sh

init_test: clean library_debug_unit_tests
	@echo "make init_test"
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/init_destroy.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/init_destroy $(LDFLAGS)
	build/init_destroy

libc_sanity_tests: clean library_debug_unit_tests
	@echo "make libc_sanity_tests"
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/memset_sanity.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/memset_sanity $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/memcpy_sanity.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/memcpy_sanity $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) tests/memmove_sanity.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/memmove_sanity $(LDFLAGS)
	build/memset_sanity ; build/memcpy_sanity; build/memmove_sanity;

fuzz_test: clean library_debug_unit_tests
	@echo "make fuzz_test"
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) tests/alloc_fuzz.c $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/alloc_fuzz $(LDFLAGS)

	LD_LIBRARY_PATH=build/ build/alloc_fuzz

## Build a non-debug library with performance
## monitoring enabled. Linux only
perf_tests: clean
	@echo "make perf_tests"
	$(CC) $(CFLAGS) $(C_SRCS) $(GDB_FLAGS) $(PERF_FLAGS) $(OS_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests_gprof
	$(CC) $(CFLAGS) $(C_SRCS) $(GDB_FLAGS) $(PERF_FLAGS) $(OS_FLAGS) tests/big_tests.c -o $(BUILD_DIR)/big_tests_gprof
	$(BUILD_DIR)/tests_gprof
	gprof -bl $(BUILD_DIR)/tests_gprof gmon.out > tests_perf_analysis.txt
	$(BUILD_DIR)/big_tests_gprof
	gprof -bl $(BUILD_DIR)/big_tests_gprof gmon.out > big_tests_perf_analysis.txt

## Runs a single test that prints CPU time
## compared to the same malloc/free operations
malloc_cmp_test: clean
	@echo "make malloc_cmp_test"
ifeq ($(MEMCPY_SANITY), -DMEMCPY_SANITY=1)
	$(error "Please unset MEMCPY_SANITY before running this test")
endif
ifeq ($(MEMSET_SANITY), -DMEMSET_SANITY=1)
	$(error "Please unset MEMSET_SANITY before running this test")
endif
	$(CC) $(CFLAGS) $(C_SRCS) $(OPTIMIZE) $(EXE_CFLAGS) $(OS_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(OPTIMIZE) $(EXE_CFLAGS) $(OS_FLAGS) -DMALLOC_PERF_TEST $(ISO_ALLOC_PRINTF_SRC) tests/tests.c -o $(BUILD_DIR)/malloc_tests
	@echo "Running IsoAlloc Performance Test"
	build/tests
	@echo "Running system malloc Performance Test"
	build/malloc_tests

## C++ Support - Build a debug version of the unit test
cpp_tests: clean cpp_library_debug
	@echo "make cpp_tests"
	$(CXX) $(CXXFLAGS) $(DEBUG_LOG_FLAGS) $(GDB_FLAGS) $(EXE_CFLAGS) $(OS_FLAGS) tests/tests.cpp $(ISO_ALLOC_PRINTF_SRC) -o $(BUILD_DIR)/cxx_tests $(LDFLAGS)
	LD_LIBRARY_PATH=$(BUILD_DIR)/ $(BUILD_DIR)/cxx_tests

install:
	cp -pR build/$(LIBNAME) /usr/lib/

format:
	clang-format-12 $(SRC_DIR)/*.* tests/*.* include/*.h -i

format-ci:
	clang-format-12 --Werror --dry-run $(SRC_DIR)/*.* tests/*.* include/*.h -i

clean:
	rm -rf build/* tests_perf_analysis.txt big_tests_perf_analysis.txt gmon.out test_output.txt *.dSYM core* iso_alloc_profiler.data
	rm -rf android/libs android/obj
	mkdir -p build/
