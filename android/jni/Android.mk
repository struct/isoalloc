LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS := -DTHREAD_SUPPORT=1 -pthread -DTHREAD_ZONE_CACHE=1 			\
	-DPRE_POPULATE_PAGES=0 -DSMALL_MEM_STARTUP=0 -DSANITIZE_CHUNKS=0 		\
	-DFUZZ_MODE=0 -DPERM_FREE_REALLOC=0 -DDISABLE_CANARY=0 -Werror 			\
	-pedantic -Wno-pointer-arith -Wno-gnu-zero-variadic-macro-arguments 	\
	-Wno-format-pedantic -DMALLOC_HOOK=1  -fvisibility=hidden -std=c11  	\
	-DALLOC_SANITY=0 -DUNINIT_READ_SANITY=0 -DCPU_PIN=0 -DEXPERIMENTAL=0 	\
	-DUAF_PTR_PAGE=0 -DVERIFY_BIT_SLOT_CACHE=0 -DNAMED_MAPPINGS=1 -fPIC 	\
	-shared  -DDEBUG=1 -DLEAK_DETECTOR=1 -DMEM_USAGE=1  				 	\
	-g -ggdb3 -fno-omit-frame-pointer

LOCAL_SRC_FILES := ../../src/iso_alloc.c ../../src/iso_alloc_printf.c	../../src/iso_alloc_random.c \
				   ../../src/iso_alloc_search.c ../../src/iso_alloc_interfaces.c ../../src/iso_alloc_profiler.c	\
				   ../../src/iso_alloc_sanity.c ../../src/malloc_hook.c

LOCAL_C_INCLUDES := ../../include/

LOCAL_MODULE    := libisoalloc

include $(BUILD_SHARED_LIBRARY)
