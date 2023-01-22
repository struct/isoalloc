/* android.h - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#pragma once

#include <sys/prctl.h>

/* This magic number is usually defined by Android Bionic:
 * https://android.googlesource.com/platform/bionic/+/263325d/libc/include/sys/prctl.h#42 */
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif

#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif
