/* iso_alloc zone_retirement.c
 * Copyright 2026 - chris.rohlf@gmail.com */

/* Regression test for retirement of zone index 0. _iso_new_zone
 * previously labeled a replacement for zone 0 with index zones_used
 * (index > 0 instead of index >= 0) which poisoned the
 * zone_lookup_table and chunk_lookup_table and lead to an abort in
 * find_suitable_zone on a later allocation of the same size */
#include "iso_alloc.h"
#include "iso_alloc_internal.h"

int main(int argc, char *argv[]) {
    /* Prime the allocator */
    void *p = iso_alloc(ZONE_32);
    iso_free_size(p, ZONE_32);

    iso_alloc_root *root = _get_root();

    if(root == NULL || root->zones[0].chunk_size != ZONE_32) {
        /* Default zone layout changed, this test needs updating */
        return ERR;
    }

    const uint64_t chunk_count = root->zones[0].chunk_count;
    const uint64_t cycles = (chunk_count << root->zone_retirement_shf) + chunk_count;

    /* iso_free_size() releases chunks immediately (no quarantine)
     * so af_count reaches 0 on every cycle and the zone retires
     * once alloc_count crosses the retirement threshold */
    for(uint64_t i = 0; i < cycles; i++) {
        void *q = iso_alloc(ZONE_32);

        if(q == NULL) {
            return ERR;
        }

        iso_free_size(q, ZONE_32);
    }

    /* Zone 0 should have been retired and replaced in place. If
     * alloc_count was never reset then retirement never triggered
     * and this test is no longer testing anything */
    if(root->zones[0].alloc_count >= cycles) {
        return ERR;
    }

    /* The replacement zone must keep index 0 */
    if(root->zones[0].index != 0) {
        return ERR;
    }

    /* The lookup table entry for this size must be empty (zone 0
     * shares the value of the empty sentinel) or point at a zone
     * holding chunks of this size */
    uint16_t idx = root->zone_lookup_table[SZ_TO_ZONE_LOOKUP_IDX(ZONE_32)];

    if(idx != 0 && root->zones[idx].chunk_size != ZONE_32) {
        return ERR;
    }

    /* Force creation of a brand new zone at index zones_used and
     * then allocate from the retired size class again. With the
     * index mislabeling bug this aborts in find_suitable_zone with
     * "Zone lookup table failed to match sizes" */
    void *big = iso_alloc(MAX_DEFAULT_ZONE_SZ + SZ_ALIGNMENT);
    void *small = iso_alloc(ZONE_32);

    if(big == NULL || small == NULL) {
        return ERR;
    }

    iso_free(big);
    iso_free(small);

    return OK;
}
