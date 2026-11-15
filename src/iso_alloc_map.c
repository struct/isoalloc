/* iso_alloc_map.c - A secure memory allocator
 * Copyright 2026 - chris.rohlf@gmail.com */

/* An open addressing hash map from a 64 bit key to a 64 bit
 * value with linear probing and backward shift deletion. It
 * answers the question every free has to ask, which zone owns
 * this pointer, in a bounded number of probes. The previous
 * table hashed the pointer to one of 65535 slots with no key
 * check, so a miss fell back to a scan of every zone.
 *
 * Writers hold whatever lock owns the map. Readers of a map
 * created with lock_free_readers never take it: a sequence
 * counter is odd while a writer is inside the map and changes
 * across every write, so a reader that observes the same even
 * value before and after its probes saw a consistent table.
 * Such a map never grows because a reader may still be
 * probing the old entries */
#include "iso_alloc_internal.h"

/* Fibonacci hashing, the multiplier is 2^64 divided by the
 * golden ratio. The top bits select the home slot */
#define PTR_MAP_HASH_MULT 0x9E3779B97F4A7C15ULL

/* Probes a reader makes before it gives up on a table that
 * may be mid write and rechecks the sequence counter */
#define PTR_MAP_SPIN_YIELD 1024

static INLINE uint64_t ptr_map_home(const iso_ptr_map_t *map, uint64_t key) {
    return ((key * PTR_MAP_HASH_MULT) >> map->shift);
}

static INLINE uint64_t ptr_map_capacity(const iso_ptr_map_t *map) {
    return (map->mask + 1);
}

static INLINE uint64_t ptr_map_load_key(const iso_ptr_map_entry_t *e) {
    return __atomic_load_n(&e->key, __ATOMIC_RELAXED);
}

static INLINE uint64_t ptr_map_load_value(const iso_ptr_map_entry_t *e) {
    return __atomic_load_n(&e->value, __ATOMIC_RELAXED);
}

static INLINE void ptr_map_store(iso_ptr_map_entry_t *e, uint64_t key, uint64_t value) {
    /* The value is written before the key so a reader that
     * sees the key also sees its value. Readers of a lock free
     * map validate the sequence counter as well */
    __atomic_store_n(&e->value, value, __ATOMIC_RELAXED);
    __atomic_store_n(&e->key, key, __ATOMIC_RELEASE);
}

/* Marks the start and end of a write for lock free readers */
static INLINE void ptr_map_write_begin(iso_ptr_map_t *map) {
    __atomic_store_n(&map->seq, map->seq + 1, __ATOMIC_RELEASE);
}

static INLINE void ptr_map_write_end(iso_ptr_map_t *map) {
    __atomic_store_n(&map->seq, map->seq + 1, __ATOMIC_RELEASE);
}

static iso_ptr_map_entry_t *ptr_map_alloc_entries(uint64_t capacity) {
    iso_ptr_map_entry_t *entries = (iso_ptr_map_entry_t *) mmap_guarded_rw_pages(capacity * sizeof(iso_ptr_map_entry_t), false, NULL);

    if(UNLIKELY(entries == NULL)) {
        LOG_AND_ABORT("Cannot allocate pointer map with %lu entries", capacity);
    }

    return entries;
}

INTERNAL_HIDDEN void iso_ptr_map_init(iso_ptr_map_t *map, uint64_t capacity, bool lock_free_readers) {
    if(UNLIKELY(capacity < 2 || (capacity & (capacity - 1)) != 0)) {
        LOG_AND_ABORT("Pointer map capacity %lu is not a power of 2", capacity);
    }

    map->entries = ptr_map_alloc_entries(capacity);
    map->mask = (capacity - 1);
    map->shift = (64 - __builtin_ctzll(capacity));
    map->count = 0;
    map->seq = 0;
    map->lock_free_readers = lock_free_readers;
}

INTERNAL_HIDDEN void iso_ptr_map_destroy(iso_ptr_map_t *map) {
    if(map->entries != NULL) {
        unmap_guarded_pages(map->entries, ptr_map_capacity(map) * sizeof(iso_ptr_map_entry_t));
        map->entries = NULL;
    }
}

/* Places an entry in a table with at least one empty slot */
static void ptr_map_place(iso_ptr_map_entry_t *entries, uint64_t mask, uint32_t shift, uint64_t key, uint64_t value) {
    uint64_t i = ((key * PTR_MAP_HASH_MULT) >> shift);

    while(ptr_map_load_key(&entries[i]) != 0) {
        i = ((i + 1) & mask);
    }

    ptr_map_store(&entries[i], key, value);
}

/* Doubles the table. Only for maps whose readers hold the
 * lock, a lock free reader could still be probing the old
 * table when it is unmapped */
static void ptr_map_grow(iso_ptr_map_t *map) {
    if(UNLIKELY(map->lock_free_readers == true)) {
        LOG_AND_ABORT("Pointer map with lock free readers is full at %lu entries", map->count);
    }

    const uint64_t old_capacity = ptr_map_capacity(map);
    const uint64_t capacity = (old_capacity << 1);
    iso_ptr_map_entry_t *entries = ptr_map_alloc_entries(capacity);
    const uint64_t mask = (capacity - 1);
    const uint32_t shift = (64 - __builtin_ctzll(capacity));

    for(uint64_t i = 0; i < old_capacity; i++) {
        const uint64_t key = map->entries[i].key;

        if(key != 0) {
            ptr_map_place(entries, mask, shift, key, map->entries[i].value);
        }
    }

    unmap_guarded_pages(map->entries, old_capacity * sizeof(iso_ptr_map_entry_t));
    map->entries = entries;
    map->mask = mask;
    map->shift = shift;
}

/* Inserts key with value. The caller holds the lock that owns
 * the map and key must not already be present */
INTERNAL_HIDDEN void iso_ptr_map_insert(iso_ptr_map_t *map, uint64_t key, uint64_t value) {
    if(UNLIKELY(key == 0)) {
        LOG_AND_ABORT("Pointer map keys cannot be 0");
    }

    /* Keep the table at most half full so probe sequences
     * stay short and an empty slot always ends a lookup */
    if(UNLIKELY(((map->count + 1) << 1) > ptr_map_capacity(map))) {
        ptr_map_grow(map);
    }

    ptr_map_write_begin(map);
    ptr_map_place(map->entries, map->mask, map->shift, key, value);
    map->count++;
    ptr_map_write_end(map);
}

/* Removes key. Returns true and the value that was stored if
 * it was present. The caller holds the lock that owns the map */
INTERNAL_HIDDEN bool iso_ptr_map_remove(iso_ptr_map_t *map, uint64_t key, uint64_t *value) {
    const uint64_t mask = map->mask;
    uint64_t i = ptr_map_home(map, key);

    while(true) {
        const uint64_t k = ptr_map_load_key(&map->entries[i]);

        if(k == key) {
            break;
        }

        if(k == 0) {
            return false;
        }

        i = ((i + 1) & mask);
    }

    if(value != NULL) {
        *value = ptr_map_load_value(&map->entries[i]);
    }

    ptr_map_write_begin(map);

    /* Backward shift deletion. Every entry after the hole whose
     * home slot is at or before the hole, in probe order, moves
     * into it so no lookup ever stops early at an empty slot
     * that used to hold a displaced entry */
    uint64_t hole = i;
    uint64_t j = i;

    while(true) {
        j = ((j + 1) & mask);
        const uint64_t k = ptr_map_load_key(&map->entries[j]);

        if(k == 0) {
            break;
        }

        const uint64_t home = ptr_map_home(map, k);
        const bool wraps = (j < hole);
        const bool movable = wraps ? (home <= hole && home > j) : (home <= hole || home > j);

        if(movable == true) {
            ptr_map_store(&map->entries[hole], k, ptr_map_load_value(&map->entries[j]));
            hole = j;
        }
    }

    ptr_map_store(&map->entries[hole], 0, 0);
    map->count--;
    ptr_map_write_end(map);
    return true;
}

/* Looks key up without taking any lock. Returns true and the
 * value if present. Safe against concurrent writers through
 * the sequence counter, see the file comment */
INTERNAL_HIDDEN bool iso_ptr_map_find(const iso_ptr_map_t *map, uint64_t key, uint64_t *value) {
    uint32_t spins = 0;

    while(true) {
        const uint64_t seq = __atomic_load_n(&map->seq, __ATOMIC_ACQUIRE);

        if(UNLIKELY((seq & 1) != 0)) {
            /* A writer is inside the map */
            if(UNLIKELY(++spins > PTR_MAP_SPIN_YIELD)) {
                sched_yield();
                spins = 0;
            }

            continue;
        }

        const uint64_t mask = map->mask;
        const iso_ptr_map_entry_t *entries = map->entries;
        uint64_t i = ptr_map_home(map, key);
        bool found = false;
        uint64_t v = 0;

        /* A table that is being rewritten could lack an empty
         * slot in this probe sequence, so never probe more
         * than the capacity before rechecking the counter */
        for(uint64_t probes = 0; probes <= mask; probes++) {
            const uint64_t k = ptr_map_load_key(&entries[i]);

            if(k == key) {
                v = ptr_map_load_value(&entries[i]);
                found = true;
                break;
            }

            if(k == 0) {
                break;
            }

            i = ((i + 1) & mask);
        }

        __atomic_thread_fence(__ATOMIC_ACQUIRE);

        if(LIKELY(__atomic_load_n(&map->seq, __ATOMIC_RELAXED) == seq)) {
            if(found == true && value != NULL) {
                *value = v;
            }

            return found;
        }
    }
}
