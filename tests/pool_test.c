/* iso_alloc pool_test.c
 * Copyright 2022 - chris.rohlf@gmail.com */

#include "iso_alloc.h"
#include "iso_alloc_internal.h"

static const uint32_t allocation_sizes[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128,
                                            ZONE_256, ZONE_512, ZONE_1024,
                                            ZONE_2048, ZONE_4096, ZONE_8192};

static const uint32_t array_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};

int allocate(size_t array_size, size_t allocation_size) {
    iso_alloc_zone_handle *zone = iso_alloc_new_zone(allocation_size);
    size_t total_chunks = iso_zone_chunk_count(zone);
    int32_t alloc_count = 0;

    /* We can treat private zones like pools of chunks
     * that don't need to be freed. Instead we can just
     * destroy the whole zone when we are done. We get
     * the benefits of pools with all of the security
     * properties of an IsoAlloc zone */
    for(int i = 0; i < total_chunks; i++) {
        void *p = iso_alloc_from_zone(zone);

        if(p == NULL) {
            LOG_AND_ABORT("Failed to allocate %ld bytes after %d total allocations from zone with %d total chunks", allocation_size, alloc_count, total_chunks);
        }

        alloc_count++;
    }

    iso_alloc_destroy_zone(zone);

    return OK;
}

int main(int argc, char *argv[]) {
    for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
        for(int z = 0; z < sizeof(allocation_sizes) / sizeof(uint32_t); z++) {
            allocate(array_sizes[i], allocation_sizes[z]);
        }
    }

    for(int i = 0; i < sizeof(array_sizes) / sizeof(uint32_t); i++) {
        allocate(array_sizes[i], 0);
    }

    return 0;
}
