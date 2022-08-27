/* macos.h - A secure memory allocator
 * Copyright 2022 - chris.rohlf@gmail.com */

#pragma once

#include <libkern/OSByteOrder.h>
#include <mach/vm_statistics.h>
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#define ENVIRON NULL
