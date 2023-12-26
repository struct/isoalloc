LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS := -DTHREAD_SUPPORT=1 -pthread						\
	-DPRE_POPULATE_PAGES=0 -DSMALL_MEM_STARTUP=0					\
	-DFUZZ_MODE=0 -DPERM_FREE_REALLOC=0 -DDISABLE_CANARY=0 -Werror			\
	-pedantic -Wno-pointer-arith -Wno-gnu-zero-variadic-macro-arguments		\
	-Wno-format-pedantic -DMALLOC_HOOK=1 -fvisibility=hidden -std=c11		\
	-DUNINIT_READ_SANITY=0 -DCPU_PIN=0 -DEXPERIMENTAL=0				\
	-DUAF_PTR_PAGE=0 -DVERIFY_FREE_BIT_SLOTS=0 -DNAMED_MAPPINGS=1 -fPIC		\
	-shared -DDEBUG=1 -DLEAK_DETECTOR=1 -DMEM_USAGE=1 -DUSE_MLOCK=1 		\
	-DMEMORY_TAGGING=0 -DSCHED_GETCPU -g -ggdb3 -fno-omit-frame-pointer		\
	-DRANDOMIZE_FREELIST=1 -DBIG_ZONE_META_DATA_GUARD=0 -DBIG_ZONE_GUARD=0  	\
	-DPROTECT_FREE_BIG_ZONES=0 -DMASK_PTRS=1 -DSIGNAL_HANDLER=0			\
	-DUSE_MLOCK=1 -DNO_ZERO_ALLOCATIONS=1 -DABORT_ON_NULL=0				\
	-DMEMCPY_SANITY=0 -DMEMSET_SANITY=0						\
	-DSTRONG_SIZE_ISOLATION=0 -DISO_DTOR_CLEANUP=0

LOCAL_SRC_FILES := ../../src/iso_alloc.c ../../src/iso_alloc_printf.c ../../src/iso_alloc_random.c				\
				   ../../src/iso_alloc_search.c ../../src/iso_alloc_interfaces.c ../../src/iso_alloc_profiler.c	\
				   ../../src/iso_alloc_sanity.c ../../src/iso_alloc_util.c ../../src/malloc_hook.c 				\
				   ../../src/libc_hook.c ../../src/iso_alloc_mem_tags.c ../../src/iso_alloc_options.c

LOCAL_C_INCLUDES := ../../include/

LOCAL_MODULE    := libisoalloc

include $(BUILD_SHARED_LIBRARY)
