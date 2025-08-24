#include "hashmap.h"
#include "hashmap_internal.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_LOAD_FACTOR 0.75f
#define MAX_PROBE_LEN       (1 << 6) - 1

// -- statics --

static inline bool is_occupied(u8 status) constfun;
static inline bool is_tombstone(u8 status) constfun;
static inline u8   get_probe_distance(u8 status) constfun;

static inline bool is_occupied(u8 status) {
    return status & 1;
}

static inline bool is_tombstone(u8 status) {
    return status & 2;
}

static inline u8 get_probe_distance(u8 status) {
    return status >> 2;
}

static inline void set_status(u8 *status, bool occupied, u8 probe_distance) {
    assert(probe_distance <= MAX_PROBE_LEN);
    *status = (u8)(probe_distance << 2) | (occupied ? 1 : 0);
}

static inline void set_tombstone(u8 *status) {
    *status |= 2;  // set tombstone
    *status &= ~1; // clear occupied
}

static inline size_t bucket_size(hashmap const *map) {
    return map->aligned_element_size + sizeof(hashmap_element_header);
}

static inline u32 key_to_bucket(hashmap const *map, hashmap_key key) {
    assert(map);
    return key % map->n_cells;
}

static inline u32 incr_index(hashmap const *map, u32 index) {
    return (index + 1) % map->n_cells;
}

// Returns: if key exists, pointer to header. Sets out_index to found
// bucket index.
static hashmap_element_header *map_find(hashmap *map, hashmap_key key) {
    assert(map);

    u32 index          = key_to_bucket(map, key);
    u32 probe_distance = 0;

    while (1) {
        if (probe_distance++ > MAX_PROBE_LEN) return 0;

        hashmap_element_header *const cell   = map_unchecked_at(map, index);
        u8                            status = cell->status;

        if (is_tombstone(status)) {

            index = incr_index(map, index);

        } else if (is_occupied(status)) {

            if (cell->key == key) return cell;

            index = incr_index(map, index);

        } else {

            return null;
        }
    }
}

static int set_one(hashmap *map, hashmap_element_header const *header, byte const *element) {

    byte to_store[HASHMAP_MAX_ELEMENT_SIZE + sizeof(hashmap_element_header)];
    byte tmp[HASHMAP_MAX_ELEMENT_SIZE + sizeof(hashmap_element_header)];

    assert(map);

    size_t const cell_size = sizeof(hashmap_element_header) + map->element_size;
    assert(bucket_size(map) >= cell_size);

    // write header and data to to_store
    assert(HASHMAP_MAX_ELEMENT_SIZE >= map->element_size);
    memcpy(to_store, header, sizeof *header);
    memcpy(to_store + sizeof *header, element, map->element_size);

    u32 index           = key_to_bucket(map, header->key);
    u8  probe_distance  = 0;
    int warning_printed = 0;

    while (1) {
        if (probe_distance > MAX_PROBE_LEN) {
            dbg("map.c set_one: overflow\n");
            return 1; // overflow
        }
        if (probe_distance > 16 && !warning_printed) {
            dbg("warning: high probe distance for key: %u, load factor: %f\n",
                ((hashmap_element_header *)to_store)->key, map_load_factor(map));
            warning_printed = 1;
        }

        hashmap_element_header *const cell = map_unchecked_at(map, index);

        if (is_occupied(cell->status)) {
            if (probe_distance <= get_probe_distance(cell->status)) {
                // continue probing
                ++probe_distance;
                index = incr_index(map, index);
            } else {
                // swap

                // save relevant status of cell to be evicted
                u8 evicted_probe_distance = get_probe_distance(cell->status);

                // copy evicted cell (header + data) to tmp
                memcpy(tmp, cell, cell_size);

                // write header and data (from to_store) in place of evicted cell
                memcpy(cell, to_store, cell_size);

                // set correct status for installed cell
                set_status(&cell->status, true, probe_distance);

                // write evicted cell (header + data) into to_store
                memcpy(to_store, tmp, cell_size);

                // continue probing with swapped element, using evicted key
                warning_printed = 0;
                probe_distance  = evicted_probe_distance + 1;
                index           = incr_index(map, index);
            }
        } else {
            // found an empty slot

            // write cell (header + data) to bucket and set status
            memcpy(cell, to_store, cell_size);

            set_status(&cell->status, true, probe_distance);
            map->n_occupied++;

            return 0;
        }
    }
    return 0;
}

static int set_one_cell(hashmap *map, hashmap_element_header *cell) {
    return set_one(map, cell, cell->data);
}

static int grow_buckets(allocator *alloc, hashmap **map) {
    // make a new map with 1.618x the number of buckets. Copy all data to
    // the new map. Then release the old map's buffers, and overwrite
    // its struct with the new map.

    size_t new_buckets = (size_t)((*map)->n_cells * 1.618);

    if (new_buckets > UINT32_MAX) {
        dbg("map grow_buckets: too many buckets\n");
        return 1;
    }

    hashmap *new_map = map_create(alloc, (*map)->element_size, (u32)new_buckets, (*map)->max_load_factor);

    if (!new_map) {
        dbg("map grow_buckets: oom\n");
        return 1;
    }

    for (u32 i = 0; i < (*map)->n_cells; ++i) {
        hashmap_element_header *cell = map_unchecked_at(*map, i);
        if (is_occupied(cell->status)) {
            if (set_one_cell(new_map, cell)) return 1;
        }
    }

    // destroy old map and return new map
    map_destroy(alloc, map);
    *map = new_map;

    return 0;
}

// -- externals --

f32 map_load_factor(hashmap const *map) {
    return (f32)map->n_occupied / (f32)map->n_cells;
}

size_t map_size(hashmap const *map) {
    return map->n_occupied;
}

bool map_empty(hashmap const *map) {
    return map->n_occupied == 0;
}

// Returns pointer to cell, whether or not it is valid, occupied, etc.
hashmap_element_header *map_unchecked_at(hashmap *map, u32 index) {
    return (hashmap_element_header *)(((byte *)map->data) + index * bucket_size(map));
}

// Returns: input if already a power of two, or else the next higher
// power of two.
u32 map_next_power_of_two(u32 n) {

    if (n > (1U << 31)) return 0; // overflow
    if (n == 0) return 1;

    // set all bits to the right of the highest set bit by masking.
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

//

hashmap *map_create(allocator *alloc, u8 element_size, u32 n_buckets, f32 max_load_factor) {

    if (element_size > HASHMAP_MAX_ELEMENT_SIZE) return null;
    if (max_load_factor < 0.01) max_load_factor = DEFAULT_LOAD_FACTOR;

    n_buckets = map_next_power_of_two(n_buckets);
    assert(n_buckets > 0);

    u8 aligned_element_size = (u8)alloc_align_to_word_size(element_size);
    assert(alloc_align_to_word_size(element_size) <= HASHMAP_MAX_ELEMENT_SIZE);
    size_t   bucket_size      = aligned_element_size + sizeof(hashmap_element_header);

    hashmap *map              = alloc_calloc(alloc, 1, sizeof(struct hashmap) + n_buckets * bucket_size);
    map->element_size         = element_size;
    map->aligned_element_size = (u8)alloc_align_to_word_size(element_size);
    map->n_cells              = n_buckets;
    map->max_load_factor      = max_load_factor;

    return map;
}

void map_destroy(allocator *alloc, hashmap **map) {
    alloc_free(alloc, *map);
    *map = null;
}

hashmap *map_copy(allocator *alloc, hashmap const *src) {
    size_t   size = sizeof(struct hashmap) + src->n_cells * bucket_size(src);
    hashmap *dst  = alloc_malloc(alloc, size);
    if (!dst) return dst;
    memcpy(dst, src, size);
    return dst;
}

int map_set(allocator *alloc, hashmap **map, hashmap_key key, void *data) {

    // Must check for existing key. Replace if present.
    void *existing = map_get(*map, key);
    if (existing) {
        memcpy(existing, data, (*map)->element_size);
        return 0;
    }

    if (map_load_factor(*map) >= (*map)->max_load_factor) {
        if (grow_buckets(alloc, map)) {
            dbg("map_set: oom\n");
            return 1;
        }
    }

    hashmap_element_header header = {.key = key, .status = 0};
    return set_one(*map, &header, data);
}

void *map_get(hashmap *map, hashmap_key key) {
    hashmap_element_header *cell = map_find(map, key);
    if (!cell) return null;
    return cell->data;
}

void map_erase(hashmap *map, hashmap_key key) {
    hashmap_element_header *cell = map_find(map, key);
    if (!cell) return;

    set_tombstone(&cell->status);
    map->n_occupied--;
}
