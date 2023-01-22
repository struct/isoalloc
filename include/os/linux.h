/* linux.h - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#pragma once

#include <sys/prctl.h>
#include <byteswap.h>

#if defined(CPU_PIN) && defined(_GNU_SOURCE) && defined(__linux__)
#include <sched.h>
#endif

#define ENVIRON environ
