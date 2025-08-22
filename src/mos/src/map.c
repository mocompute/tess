#include "map.h"
#include "map_internal.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_LOAD_FACTOR 0.75f
#define MAX_PROBE_LEN       (1 << 6) - 1

// -- statics --

static size_t bucket_size(map_t const *map) {
    return map->aligned_element_size + sizeof(map_header);
}

static uint32_t key_to_bucket(map_t const *map, size_t key) {
    return key % map->n_cells;
}

static uint32_t incr_index(map_t const *map, uint32_t index) {
    return (index + 1) % map->n_cells;
}

// Returns: if key exists, pointer to header. Sets out_index to found
// bucket index.
static char *map_find(map_t *map, size_t key, uint32_t *out_index) {
    uint32_t index          = key_to_bucket(map, key);

    uint32_t probe_distance = 0;

    while (1) {
        if (probe_distance++ > MAX_PROBE_LEN) return 0;

        map_cell_status status = map->status_array[index];

        if (status.tombstone) {
            // keep looking
            index = incr_index(map, index);
        } else if (status.occupied) {
            char *const cell      = map_unchecked_at(map, index);
            size_t      found_key = *(size_t *)cell;

            if (found_key != key) {
                // keep looking
                index = incr_index(map, index);
            } else {
                // found
                if (out_index) *out_index = (uint32_t)index;
                return cell;
            }
        } else {
            // target bucket is empty and not a tombstone
            return 0;
        }
    }
}

static int set_one(map_t *map, size_t const key, char const *element) {

    size_t const cell_size = sizeof(map_header) + map->element_size;
    assert(bucket_size(map) >= cell_size);

    // write header and data to to_store
    memcpy(map->to_store, &key, sizeof key);
    memcpy(map->to_store + sizeof key, element, map->element_size);

    uint32_t      index           = key_to_bucket(map, key);
    unsigned char probe_distance  = 0;

    int           warning_printed = 0;

    while (1) {
        if (probe_distance > MAX_PROBE_LEN) {
            dbg("map.c set_one: overflow\n");
            return 1; // overflow
        }
        if (probe_distance > 16 && !warning_printed) {
            fprintf(stderr, "warning: high probe distance for key: %zu, load factor: %f\n",
                    *(size_t *)map->to_store, map_load_factor(map));
            warning_printed = 1;
        }

        if (map->status_array[index].occupied) {
            if (probe_distance <= map->status_array[index].probe_distance) {
                // continue probing
                ++probe_distance;
                index = incr_index(map, index);
            } else {
                // swap
                char *const cell = map_unchecked_at(map, index);

                // copy evicted cell (header + data) to tmp
                memcpy(map->tmp, cell, cell_size);
                // write header (from to_store) in place of evicted cell
                memcpy(cell, map->to_store, cell_size);
                // write evicted cell (header + data) into to_store
                memcpy(map->to_store, map->tmp, cell_size);

                uint8_t evicted_probe_distance          = map->status_array[index].probe_distance;
                map->status_array[index].probe_distance = probe_distance;

                // continue probing with swapped element, using evicted key
                warning_printed = 0;
                probe_distance  = evicted_probe_distance + 1;
                index           = incr_index(map, index);
            }
        } else {
            // found an empty slot
            map->status_array[index].occupied       = 1;
            map->status_array[index].tombstone      = 0;
            map->status_array[index].probe_distance = probe_distance;
            map->n_occupied++;

            char *const cell = map_unchecked_at(map, index);

            // write cell (header + data) to bucket
            memcpy(cell, map->to_store, cell_size);
            return 0;
        }
    }
    return 0;
}

static int set_one_cell(map_t *map, char *cell) {
    return set_one(map, *(size_t *)cell, cell + sizeof(size_t));
}

static int grow_buckets(allocator *alloc, map_t *map) {
    // make a new map with 1.618x the number of buckets. Copy all data to
    // the new map. Then release the old map's buffers, and overwrite
    // its struct with the new map.

    size_t new_buckets = (size_t)(map->n_cells * 1.618);

    if (new_buckets > UINT32_MAX) {
        dbg("map grow_buckets: too many buckets\n");
        return 1;
    }

    map_t new_map;
    if (map_init(alloc, &new_map, map->element_size, (uint32_t)new_buckets, map->max_load_factor)) {
        dbg("map grow_buckets: oom\n");
        return 1;
    }

    for (uint32_t i = 0; i < map->n_cells; ++i) {
        if (map->status_array[i].occupied) {
            char *cell = map_unchecked_at(map, i);

            if (set_one_cell(&new_map, cell)) return 1;
        }
    }

    // free old map buffers and overwrite pointers with new map's pointers
    map_deinit(alloc, map);
    alloc_copy(map, &new_map);

    return 0;
}

// -- externals --

float map_load_factor(map_t const *map) {
    return (float)map->n_occupied / (float)map->n_cells;
}

size_t map_size(map_t const *map) {
    return map->n_occupied;
}

bool map_empty(map_t const *map) {
    return map->n_occupied == 0;
}

// Returns pointer to cell, whether or not it is valid, occupied, etc.
char *map_unchecked_at(map_t *map, uint32_t index) {
    return map->data + index * bucket_size(map);
}

// Returns: input if already a power of two, or else the next higher
// power of two.
uint32_t map_next_power_of_two(uint32_t n) {

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

map_t *map_alloc(allocator *alloc) {
    return alloc->malloc(alloc, sizeof(map_t));
}

void map_dealloc(allocator *alloc, map_t **p) {
    alloc->free(alloc, *p);
    *p = 0;
}

int map_init(allocator *alloc, map_t *map, size_t element_size, uint32_t buckets, float max_load_factor) {

    assert(sizeof(map_cell_status) == 1);
    assert(sizeof(map_header) == sizeof(size_t));

    assert(element_size <= PTRDIFF_MAX);

    buckets = map_next_power_of_two(buckets);
    assert(buckets > 0);

    if (max_load_factor < 0.01) max_load_factor = DEFAULT_LOAD_FACTOR;

    alloc_zero(map);
    map->element_size         = element_size;
    map->aligned_element_size = alloc_align_to_word_size(element_size);
    map->n_cells              = buckets;
    map->max_load_factor      = max_load_factor;
    map->data                 = alloc->malloc(alloc, buckets * bucket_size(map));
    map->status_array         = alloc->calloc(alloc, buckets, sizeof(map_cell_status));
    map->to_store             = alloc->malloc(alloc, map->aligned_element_size + sizeof(map_header));
    map->tmp                  = alloc->malloc(alloc, map->aligned_element_size + sizeof(map_header));
    if (!map->data || !map->status_array || !map->to_store || !map->tmp) {
        dbg("map_init: oom\n");
        return 1;
    }

    return 0;
}

void map_deinit(allocator *alloc, map_t *map) {
    alloc->free(alloc, map->to_store);
    alloc->free(alloc, map->tmp);
    alloc->free(alloc, map->status_array);
    alloc->free(alloc, map->data);
    alloc_invalidate(map);
}

int map_set(allocator *alloc, map_t *map, size_t key, void *data) {

    // Must check for existing key. Replace if present.
    void *existing = map_get(map, key);
    if (existing) {
        memcpy(existing, data, map->element_size);
        return 0;
    }

    if (map_load_factor(map) >= map->max_load_factor) {
        if (grow_buckets(alloc, map)) {
            dbg("map_set: oom\n");
            return 1;
        }
    }

    return set_one(map, key, data);
}

void *map_get(map_t *map, size_t key) {
    char *cell = map_find(map, key, 0);
    if (!cell) return 0;
    return cell + sizeof(map_header);
}

void map_erase(map_t *map, size_t key) {
    uint32_t index;
    char    *p = map_find(map, key, &index);
    if (!p) return;

    map->status_array[index].occupied  = 0;
    map->status_array[index].tombstone = 1;
    map->n_occupied--;
}
