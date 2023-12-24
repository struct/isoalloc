/* iso_alloc_util.c - A secure memory allocator
 * Copyright 2023 - chris.rohlf@gmail.com */

#include "iso_alloc.h"

static uint64_t iso_options[OPTION_LAST + 1] = {
    0};

uint64_t _iso_option_get(iso_option_t id) {
    assert(id >= OPTION_FIRST && id <= OPTION_LAST);
    return iso_options[id];
}

void _iso_option_set(iso_option_t id, uint64_t val) {
    assert(id >= OPTION_FIRST && id <= OPTION_LAST);
    iso_options[id] = val;
}
