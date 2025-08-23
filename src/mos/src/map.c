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

static uint32_t key_to_bucket(map_t const *map, map_key key) {
    assert(map);
    return key % map->n_cells;
}

static uint32_t incr_index(map_t const *map, uint32_t index) {
    return (index + 1) % map->n_cells;
}

// Returns: if key exists, pointer to header. Sets out_index to found
// bucket index.
static map_header *map_find(map_t *map, map_key key) {
    assert(map);

    uint32_t index          = key_to_bucket(map, key);
    uint32_t probe_distance = 0;

    while (1) {
        if (probe_distance++ > MAX_PROBE_LEN) return 0;

        map_header *const cell   = map_unchecked_at(map, index);
        map_cell_status   status = cell->status;

        if (status.tombstone) {
            // keep looking
            index = incr_index(map, index);
        } else if (status.occupied) {

            if (cell->key != key) {
                // keep looking
                index = incr_index(map, index);
            } else {
                // found
                return cell;
            }
        } else {
            // target bucket is empty and not a tombstone
            return 0;
        }
    }
}

static int set_one(map_t *map, map_header const *header, char const *element) {

    char to_store[MAP_MAX_ELEMENT_SIZE + sizeof(map_header)];
    char tmp[MAP_MAX_ELEMENT_SIZE + sizeof(map_header)];

    assert(map);

    size_t const cell_size = sizeof(map_header) + map->element_size;
    assert(bucket_size(map) >= cell_size);

    // write header and data to to_store
    assert(MAP_MAX_ELEMENT_SIZE >= map->element_size);
    memcpy(to_store, header, sizeof *header);
    memcpy(to_store + sizeof *header, element, map->element_size);

    uint32_t      index           = key_to_bucket(map, header->key);
    unsigned char probe_distance  = 0;
    int           warning_printed = 0;

    while (1) {
        if (probe_distance > MAX_PROBE_LEN) {
            dbg("map.c set_one: overflow\n");
            return 1; // overflow
        }
        if (probe_distance > 16 && !warning_printed) {
            fprintf(stderr, "warning: high probe distance for key: %u, load factor: %f\n",
                    ((map_header *)to_store)->key, map_load_factor(map));
            warning_printed = 1;
        }

        map_header *const cell = map_unchecked_at(map, index);

        if (cell->status.occupied) {
            if (probe_distance <= cell->status.probe_distance) {
                // continue probing
                ++probe_distance;
                index = incr_index(map, index);
            } else {
                // swap

                // save relevant status of cell to be evicted
                uint8_t evicted_probe_distance = cell->status.probe_distance;

                // copy evicted cell (header + data) to tmp
                memcpy(tmp, cell, cell_size);

                // write header and data (from to_store) in place of evicted cell
                memcpy(cell, to_store, cell_size);

                // set correct status for installed cell
                cell->status = (map_cell_status){
                  .occupied       = 1,
                  .tombstone      = 0,
                  .probe_distance = probe_distance,
                };

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

            cell->status = (map_cell_status){
              .occupied       = 1,
              .tombstone      = 0,
              .probe_distance = probe_distance,
            };
            map->n_occupied++;

            return 0;
        }
    }
    return 0;
}

static int set_one_cell(map_t *map, map_header *cell) {
    return set_one(map, cell, cell->data);
}

static int grow_buckets(allocator *alloc, map_t **map) {
    // make a new map with 1.618x the number of buckets. Copy all data to
    // the new map. Then release the old map's buffers, and overwrite
    // its struct with the new map.

    size_t new_buckets = (size_t)((*map)->n_cells * 1.618);

    if (new_buckets > UINT32_MAX) {
        dbg("map grow_buckets: too many buckets\n");
        return 1;
    }

    map_t *new_map =
      map_create(alloc, (*map)->element_size, (uint32_t)new_buckets, (*map)->max_load_factor);

    if (!new_map) {
        dbg("map grow_buckets: oom\n");
        return 1;
    }

    for (uint32_t i = 0; i < (*map)->n_cells; ++i) {
        map_header *cell = map_unchecked_at(*map, i);
        if (cell->status.occupied) {
            if (set_one_cell(new_map, cell)) return 1;
        }
    }

    // destroy old map and return new map
    map_destroy(alloc, map);
    *map = new_map;

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
map_header *map_unchecked_at(map_t *map, uint32_t index) {
    return (map_header *)(((char *)map->data) + index * bucket_size(map));
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

map_t *map_create(allocator *alloc, uint8_t element_size, uint32_t n_buckets, float max_load_factor) {

    assert(sizeof(map_cell_status) == 1);

    if (element_size > MAP_MAX_ELEMENT_SIZE) return NULL;
    if (max_load_factor < 0.01) max_load_factor = DEFAULT_LOAD_FACTOR;

    n_buckets = map_next_power_of_two(n_buckets);
    assert(n_buckets > 0);

    uint8_t aligned_element_size = (uint8_t)alloc_align_to_word_size(element_size);
    assert(alloc_align_to_word_size(element_size) <= MAP_MAX_ELEMENT_SIZE);
    size_t bucket_size        = aligned_element_size + sizeof(map_header);

    map_t *map                = alloc->calloc(alloc, 1, sizeof(struct map) + n_buckets * bucket_size);
    map->element_size         = element_size;
    map->aligned_element_size = (uint8_t)alloc_align_to_word_size(element_size);
    map->n_cells              = n_buckets;
    map->max_load_factor      = max_load_factor;

    return map;
}

void map_destroy(allocator *alloc, map_t **map) {
    alloc->free(alloc, *map);
    *map = NULL;
}

int map_set(allocator *alloc, map_t **map, map_key key, void *data) {

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

    map_header header = {.key = key, {0}};
    return set_one(*map, &header, data);
}

void *map_get(map_t *map, map_key key) {
    map_header *cell = map_find(map, key);
    if (!cell) return 0;
    return cell->data;
}

void map_erase(map_t *map, map_key key) {
    map_header *cell = map_find(map, key);
    if (!cell) return;

    cell->status.occupied  = 0;
    cell->status.tombstone = 1;
    map->n_occupied--;
}
