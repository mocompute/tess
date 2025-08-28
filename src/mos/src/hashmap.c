#include "hashmap.h"
#include "hashmap_internal.h"

#include "alloc.h"
#include "dbg.h"
#include "hash.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_LOAD_FACTOR 0.75f
#define DEFAULT_N_BUCKETS   64
#define MAX_PROBE_LEN       (1 << 6) - 1

// -- statics --

static struct hashmap_entry *map_unchecked_at(hashmap *map, u32 index);

static inline bool           is_occupied(u8 status) constfun;
static inline bool           is_tombstone(u8 status) constfun;
static inline u8             get_probe_distance(u8 status) constfun;

static inline bool           is_occupied(u8 status) {
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

static inline size_t hashmap_entry_size(hashmap const *map) {
    return map->aligned_value_size + sizeof(struct hashmap_entry);
}

static inline u32 key_to_bucket(hashmap const *map, byte const *key, u16 key_len) {
    assert(map);
    u32 const h = hash32(key, key_len);
    return h % map->n_cells;
}

static inline u32 incr_index(hashmap const *map, u32 index) {
    return (index + 1) % map->n_cells;
}

// Returns: if key exists, pointer to header. Sets out_index to found
// bucket index.
static struct hashmap_entry *map_find(hashmap *map, byte const *key, u16 key_len) {
    assert(map);

    u32 index          = key_to_bucket(map, key, key_len);
    u32 probe_distance = 0;

    while (1) {
        if (probe_distance++ > MAX_PROBE_LEN) return 0;

        struct hashmap_entry *const cell   = map_unchecked_at(map, index);
        u8                          status = cell->status;

        if (is_tombstone(status)) {

            index = incr_index(map, index);

        } else if (is_occupied(status)) {

            if (cell->key->size == key_len && 0 == memcmp(key, cell->key->data, cell->key->size))
                return cell;

            index = incr_index(map, index);

        } else {

            return null;
        }
    }
}

static int set_one(hashmap *map, struct hashmap_entry const *header, byte const *element) {

    byte to_store[HASHMAP_MAX_ELEMENT_SIZE + sizeof(struct hashmap_entry)];
    byte tmp[HASHMAP_MAX_ELEMENT_SIZE + sizeof(struct hashmap_entry)];

    assert(map);

    size_t const cell_size = sizeof(struct hashmap_entry) + map->value_size;
    assert(hashmap_entry_size(map) >= cell_size);

    // write header and data to to_store
    assert(HASHMAP_MAX_ELEMENT_SIZE >= map->value_size);
    memcpy(to_store, header, sizeof *header);
    memcpy(to_store + sizeof *header, element, map->value_size);

    u32 index           = key_to_bucket(map, header->key->data, header->key->size);
    u8  probe_distance  = 0;
    int warning_printed = 0;

    while (1) {
        if (probe_distance > MAX_PROBE_LEN) {
            dbg("map.c set_one: overflow\n");
            return 1; // overflow
        }
        if (probe_distance > 16 && !warning_printed) {
            dbg("warning: high probe distance for key: %p, load factor: %f\n",
                ((struct hashmap_entry *)to_store)->key, map_load_factor(map));
            warning_printed = 1;
        }

        struct hashmap_entry *const cell = map_unchecked_at(map, index);

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

// static int set_one_cell(hashmap *map, struct hashmap_entry *cell) {
//     return set_one(map, cell, cell->data);
// }

static int grow_buckets(allocator *alloc, hashmap **map) {
    // make a new map with 1.618x the number of buckets. Copy all data to
    // the new map. Then release the old map's buffers, and overwrite
    // its struct with the new map.

    u64 new_buckets = (u64)((*map)->n_cells * 1.618);

    if (new_buckets > UINT32_MAX) {
        dbg("map grow_buckets: too many buckets\n");
        return 1;
    }

    hashmap *new_map = map_create_n(alloc, (*map)->value_size, (u32)new_buckets);

    for (u32 i = 0; i < (*map)->n_cells; ++i) {
        struct hashmap_entry *cell = map_unchecked_at(*map, i);
        if (is_occupied(cell->status)) {
            // use map_set api in order to copy keys to new storage,
            // under the assumption data locality would be better than
            // preserving existing key strorage.
            map_set(alloc, &new_map, cell->key->data, cell->key->size, cell->data);
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

struct hashmap_entry *map_unchecked_at(hashmap *map, u32 index) {
    return (struct hashmap_entry *)&map->entries[index * hashmap_entry_size(map)];
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

hashmap *map_create_n(allocator *alloc, u16 value_size, u32 n_buckets) {

    size_t aligned_value_size = alloc_align_to_word_size(value_size);
    if (aligned_value_size > HASHMAP_MAX_ELEMENT_SIZE) {
        dbg("map_create_n: element size too large\n");
        assert(false);
        exit(1);
    }

    hashmap *map = alloc_calloc(
      alloc, 1, sizeof(struct hashmap) + n_buckets * (sizeof(struct hashmap_entry) + aligned_value_size));

    map->key_alloc          = alloc;

    map->n_cells            = n_buckets;
    map->n_occupied         = 0;
    map->value_size         = value_size;
    map->aligned_value_size = (u16)alloc_align_to_word_size(value_size);

    assert(map->aligned_value_size <= HASHMAP_MAX_ELEMENT_SIZE);
    if (map->aligned_value_size > HASHMAP_MAX_ELEMENT_SIZE) {
        alloc_free(alloc, map);
        map = null;
    }

    return map;
}

hashmap *map_create(allocator *alloc, u16 value_size) {
    return map_create_n(alloc, value_size, DEFAULT_N_BUCKETS);
}

void map_destroy(allocator *alloc, hashmap **map) {

    struct hashmap_iterator     iter = {0};
    struct hashmap_entry const *entry;
    while (map_citer(*map, &iter, &entry)) alloc_free((*map)->key_alloc, entry->key);

    alloc_free(alloc, *map);
    *map = null;
}

hashmap *map_copy(allocator *alloc, hashmap const *src) {
    size_t   size = sizeof(struct hashmap) + src->n_cells * hashmap_entry_size(src);
    hashmap *dst  = alloc_malloc(alloc, size);
    memcpy(dst, src, size);

    struct hashmap_iterator iter = {0};
    struct hashmap_entry   *entry;
    while (map_iter(dst, &iter, &entry)) {
        if (!is_occupied(entry->status)) continue;

        // copy key storage
        dbg("index = %u\n", iter.index);
        assert(entry->key);
        struct hashmap_key *key =
          alloc_malloc(src->key_alloc, sizeof(struct hashmap_key) + entry->key->size);

        key->size = entry->key->size;
        memcpy(key->data, entry->key->data, key->size);

        entry->key = key;
    }

    return dst;
}

bool map_iter(hashmap const *self, struct hashmap_iterator *iter, struct hashmap_entry **out) {

    if (iter->index == self->n_cells) return false;

    *out = (struct hashmap_entry *)&self->entries[hashmap_entry_size(self) * iter->index];
    iter->index++;
    return true;
}

bool map_citer(hashmap const *self, struct hashmap_iterator *iter, struct hashmap_entry const **out) {
    return map_iter(self, iter, (struct hashmap_entry **)out);
}

bool map_contains(hashmap *self, void const *key, u16 key_len) {
    struct hashmap_entry *cell = map_find(self, key, key_len);
    return cell != null;
}

void map_set(allocator *alloc, hashmap **self, void const *key, u16 key_len, void const *data) {

    // Must check for existing key. Replace if present.
    struct hashmap_entry *existing = map_find(*self, key, key_len);

    if (existing) {
        memcpy(existing->data, data, (*self)->value_size);
        return;
    }

    if (map_load_factor(*self) >= DEFAULT_LOAD_FACTOR) {
        if (grow_buckets(alloc, self)) {
            dbg("map_set: oom\n");
            assert(false);
            exit(1);
        }
    }

    // allocate key storage
    struct hashmap_key *key_storage =
      alloc_malloc((*self)->key_alloc, sizeof(struct hashmap_key) + key_len);
    key_storage->size = key_len;
    memcpy(key_storage->data, key, key_len);

    // set the entry

    struct hashmap_entry entry = {.key = key_storage, .status = 0};
    if (set_one(*self, &entry, data)) {
        dbg("map_set: error in set_one\n");
        assert(false);
        exit(1);
    }
}

void *map_get(hashmap *map, void const *key, u16 key_len) {
    struct hashmap_entry *cell = map_find(map, key, key_len);
    if (!cell) return null;
    return cell->data;
}

void map_erase(hashmap *map, void const *key, u16 key_len) {
    struct hashmap_entry *cell = map_find(map, key, key_len);
    if (!cell) return;

    set_tombstone(&cell->status);
    map->n_occupied--;
}
