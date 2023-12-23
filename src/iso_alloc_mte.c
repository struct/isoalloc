/* iso_alloc_mte.c - A secure memory allocator
 * Copyright 2024 - chris.rohlf@gmail.com */

#include "iso_alloc_internal.h"

/* The majority of this code is adapted from Scudos implementation
 * of ARM MTE support. That code can be found here:
 * https://android.googlesource.com/platform/external/scudo/+/refs/tags/android-14.0.0_r1/standalone/ 
 * Its license (The LLVM Project is under the Apache License v2.0 with LLVM Exceptions) can be found here
 * https://android.googlesource.com/platform/external/scudo/+/refs/tags/android-14.0.0_r1/LICENSE.TXT */

#if ARM_MTE == 1

uintptr_t iso_mte_untag_ptr(void *p) {
    return (uintptr_t) p & ((1ULL << 56) - 1);
}

uint8_t iso_mte_extract_tag(void *p) {
    return ((uintptr_t) p >> 56) & 0xF;
}

/* Check for the MTE bit in the ELF Auxv */
bool iso_is_mte_supported(void) {
#ifndef HWCAP2_MTE
#define HWCAP2_MTE (1 << 18)
#endif
    return getauxval(AT_HWCAP2) & HWCAP2_MTE;
}

void *iso_mte_set_tag_range(void *p, size_t size) {
    void *tagged_ptr = iso_mte_create_tag(p, 0x0);
    return (void *) iso_mte_set_tags(tagged_ptr, tagged_ptr + size);
}

/* Uses IRG to create a random tag */
void *iso_mte_create_tag(void *p, uint64_t exclusion_mask) {
    exclusion_mask |= 1;
    void *tagged_ptr;
    __asm__ __volatile__(
        ".arch_extension memtag\n"
        "irg %[tagged_ptr], %[p], %[exclusion_mask]\n"
        : [tagged_ptr] "=r"(tagged_ptr)
        : [p] "r"(p), [exclusion_mask] "r"(exclusion_mask));
    return tagged_ptr;
}

/* Uses STG to lock a tag to an address */
void iso_mte_set_tag(void *p) {
    __asm__ __volatile__(
        ".arch_extension memtag\n"
        "stg %0, [%0]\n"
        :
        : "r"(p)
        : "memory");
}

/* Uses LDG to load a tag */
void *iso_mte_get_tag(void *p) {
    void *tagged_ptr = p;
    __asm__ __volatile__(
        ".arch_extension memtag\n"
        "ldg %0, [%0]\n"
        : "+r"(tagged_ptr)
        :
        : "memory");
    return tagged_ptr;
}

/* Set tag for a region of memory, zeroize */
void *iso_mte_set_tags(void *start, void *end) {
    uintptr_t line_size, next, tmp;
    __asm__ __volatile__(
        ".arch_extension memtag\n"

        // Compute the cache line size in bytes (DCZID_EL0 stores it as the log2
        // of the number of 4-byte words) and bail out to the slow path if DCZID_EL0
        // indicates that the DC instructions are unavailable.
        "DCZID .req %[tmp]\n"
        "mrs DCZID, dczid_el0\n"
        "tbnz DCZID, #4, 3f\n"
        "and DCZID, DCZID, #15\n"
        "mov %[line_size], #4\n"
        "lsl %[line_size], %[line_size], DCZID\n"
        ".unreq DCZID\n"

        // Our main loop doesn't handle the case where we don't need to perform any
        // DC GZVA operations. If the size of our tagged region is less than
        // twice the cache line size, bail out to the slow path since it's not
        // guaranteed that we'll be able to do a DC GZVA.
        "Size .req %[tmp]\n"
        "sub Size, %[end], %[cur]\n"
        "cmp Size, %[line_size], lsl #1\n"
        "b.lt 3f\n"
        ".unreq Size\n"

        "line_mask .req %[tmp]\n"
        "sub line_mask, %[line_size], #1\n"

        // STZG until the start of the next cache line.
        "orr %[next], %[cur], line_mask\n"

        "1:\n"
        "stzg %[cur], [%[cur]], #16\n"
        "cmp %[cur], %[next]\n"
        "b.lt 1b\n"

        // DC GZVA cache lines until we have no more full cache lines.
        "bic %[next], %[end], line_mask\n"
        ".unreq line_mask\n"

        "2:\n"
        "dc gzva, %[cur]\n"
        "add %[cur], %[cur], %[line_size]\n"
        "cmp %[cur], %[next]\n"
        "b.lt 2b\n"

        // STZG until the end of the tagged region. This loop is also used to handle
        // slow path cases.

        "3:\n"
        "cmp %[cur], %[end]\n"
        "b.ge 4f\n"
        "stzg %[cur], [%[cur]], #16\n"
        "b 3b\n"

        "4:\n"

        : [cur] "+&r"(start), [line_size] "=&r"(line_size), [next] "=&r"(next), [tmp] "=&r"(tmp)
        : [end] "r"(end)
        : "memory");

    return start;
}
#endif
