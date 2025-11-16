#include "hashmap.h"

#include "alloc.h"
#include "array.h"
#include "dbg.h"
#include "hash.h"
#include "str.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_LOAD_FACTOR 0.75
#define DEFAULT_N_BUCKETS   64
#define MAX_PROBE_LEN       (1 << 6) - 1

static_assert(16 == sizeof(hashmap_entry), "");

struct hashmap {

    allocator *parent_alloc;
    allocator *key_alloc;

    u32        n_cells;
    u32        n_occupied;

    u16        value_size;

    alignas(alignof(hashmap_entry)) byte entries[];
};

// -- statics --

static hashmap_entry *map_unchecked_at(hashmap *map, u32 index);

// pass zero-init iterator to start
static int        internal_iter(hashmap const *, hashmap_iterator *, hashmap_entry **out);
static int        internal_citer(hashmap const *, hashmap_iterator *, hashmap_entry const **out);

static inline int is_occupied(u8 status) constfun;
static inline int is_tombstone(u8 status) constfun;
static inline u8  get_probe_distance(u8 status) constfun;

static inline int is_occupied(u8 status) {
    return status & 1;
}

static inline int is_tombstone(u8 status) {
    return status & 2;
}

static inline u8 get_probe_distance(u8 status) {
    return status >> 2;
}

static inline void set_status(u8 *status, int is_occupied, u8 probe_distance) {
    assert(probe_distance <= MAX_PROBE_LEN);
    *status = (u8)(probe_distance << 2) | (is_occupied ? 1 : 0);
}

static inline void set_tombstone(u8 *status) {
    *status |= 2;  // set tombstone
    *status &= ~1; // clear occupied
}

static inline size_t hashmap_entry_size(hashmap const *map) {
    size_t aligned_value_size = alloc_align_to_word_size(map->value_size);
    return aligned_value_size + sizeof(hashmap_entry);
}

static inline u32 key_to_bucket(hashmap const *map, byte const *key, u8 key_len) {
    assert(map);
    u32 const h = hash32(key, key_len);
    return h % map->n_cells;
}

static inline u32 incr_index(hashmap const *map, u32 index) {
    return (index + 1) % map->n_cells;
}

// Returns: if key exists, pointer to header. Sets out_index to found
// bucket index.
static hashmap_entry *map_find(hashmap *map, byte const *key, u8 key_len) {
    assert(map);

    u32 index          = key_to_bucket(map, key, key_len);
    u32 probe_distance = 0;

    while (1) {
        if (probe_distance++ > MAX_PROBE_LEN) return 0;

        hashmap_entry *const cell   = map_unchecked_at(map, index);
        u8                   status = cell->status;

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

static int set_one(hashmap *map, hashmap_entry const *header, byte const *element) {

    byte to_store[HASHMAP_MAX_ELEMENT_SIZE + sizeof(hashmap_entry)];
    byte tmp[HASHMAP_MAX_ELEMENT_SIZE + sizeof(hashmap_entry)];

    assert(map);

    size_t const cell_size = sizeof(hashmap_entry) + map->value_size;
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
                ((hashmap_entry *)to_store)->key, map_load_factor(map));
            warning_printed = 1;
        }

        hashmap_entry *const cell = map_unchecked_at(map, index);

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
                set_status(&cell->status, 1, probe_distance);

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

            set_status(&cell->status, 1, probe_distance);
            map->n_occupied++;

            return 0;
        }
    }
    return 0;
}

static int grow_buckets(hashmap **map) {
    // make a new map with 1.618x the number of buckets. Copy all data to
    // the new map. Then release the old map's buffers, and overwrite
    // its struct with the new map.

    u64 new_buckets = (u64)((*map)->n_cells * 1.618);

    if (new_buckets > UINT32_MAX) {
        dbg("map grow_buckets: too many buckets\n");
        return 1;
    }

    hashmap *new_map = map_create((*map)->parent_alloc, (*map)->value_size, (u32)new_buckets);

    for (u32 i = 0; i < (*map)->n_cells; ++i) {
        hashmap_entry *cell = map_unchecked_at(*map, i);
        if (is_occupied(cell->status)) {
            // use map_set api in order to copy keys to new storage,
            // under the assumption data locality would be better than
            // preserving existing key strorage.
            map_set(&new_map, cell->key->data, cell->key->size, cell->data);
        }
    }

    // destroy old map and return new map
    map_destroy(map);
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

int map_empty(hashmap const *map) {
    return map->n_occupied == 0;
}

hashmap_entry *map_unchecked_at(hashmap *map, u32 index) {
    return (hashmap_entry *)&map->entries[index * hashmap_entry_size(map)];
}

//

hashmap *map_create(allocator *alloc, u16 value_size, u32 n_buckets) {
    if (n_buckets < 8) n_buckets = 8;

    size_t aligned_value_size = alloc_align_to_word_size(value_size);
    if (aligned_value_size > HASHMAP_MAX_ELEMENT_SIZE) fatal("map_create_n: element size too large\n");

    size_t   bucket_size = (sizeof(hashmap_entry) + aligned_value_size);

    hashmap *map         = alloc_calloc(alloc, 1, sizeof(hashmap) + n_buckets * bucket_size);

    map->parent_alloc    = alloc;
    map->key_alloc       = alloc;

    map->n_cells         = n_buckets;
    map->n_occupied      = 0;
    map->value_size      = value_size;

    memset(map->entries, 0, n_buckets * bucket_size);

    return map;
}

hashmap *map_create_ptr(allocator *alloc, u32 n_buckets) {
    return map_create(alloc, sizeof(void *), n_buckets);
}

void map_destroy(hashmap **map) {
    if (!map || !*map) return;

    hashmap_iterator     iter = {0};
    hashmap_entry const *entry;
    while (internal_citer(*map, &iter, &entry)) alloc_free((*map)->key_alloc, entry->key);

    alloc_free((*map)->parent_alloc, *map);
    *map = null;
}

hashmap *map_copy(hashmap const *src) {
    size_t   size = sizeof(hashmap) + src->n_cells * hashmap_entry_size(src);
    hashmap *dst  = alloc_malloc(src->parent_alloc, size);
    memcpy(dst, src, size);

    hashmap_iterator iter = {0};
    hashmap_entry   *entry;
    while (internal_iter(dst, &iter, &entry)) {
        if (!is_occupied(entry->status)) continue;

        // copy key storage
        assert(entry->key);
        hashmap_key *key = alloc_malloc(src->key_alloc, sizeof(hashmap_key) + entry->key->size);

        key->size        = entry->key->size;
        memcpy(key->data, entry->key->data, key->size);

        entry->key = key;
    }

    return dst;
}

int internal_iter(hashmap const *self, hashmap_iterator *iter, hashmap_entry **out) {

    if (iter->index == self->n_cells) return 0;

    *out = (hashmap_entry *)&self->entries[hashmap_entry_size(self) * iter->index];
    iter->index++;
    return 1;
}

int map_iter(hashmap const *self, hashmap_iterator *iter) {
    while (1) {
        if (iter->index == self->n_cells) return 0;

        hashmap_entry *entry = (hashmap_entry *)&self->entries[hashmap_entry_size(self) * iter->index];
        iter->index++;

        if (is_occupied(entry->status)) {
            iter->key_ptr  = &entry->key->data;
            iter->key_size = entry->key->size;
            iter->data     = &entry->data;
            return 1;
        }
    }
}

int internal_citer(hashmap const *self, hashmap_iterator *iter, hashmap_entry const **out) {
    return internal_iter(self, iter, (hashmap_entry **)out);
}

int map_contains(hashmap const *self, void const *key, u8 key_len) {
    hashmap_entry *cell = map_find((hashmap *)self, key, key_len);
    return cell != null;
}

int str_map_contains(hashmap const *self, str key) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    return map_contains(self, s.buf, s.len);
}

void map_set(hashmap **self, void const *key, u8 key_len, void const *data) {

    // Must check for existing key. Replace if present.
    hashmap_entry *existing = map_find(*self, key, key_len);

    if (existing) {
        memcpy(existing->data, data, (*self)->value_size);
        return;
    }

    if (map_load_factor(*self) >= DEFAULT_LOAD_FACTOR) {
        if (grow_buckets(self)) {
            dbg("map_set: oom\n");
            assert(0);
            exit(1);
        }
    }

    hashmap_entry entry = {
      .key    = alloc_malloc((*self)->key_alloc, sizeof(hashmap_key) + key_len),
      .status = 0,
    };

    // copy key into storage
    entry.key->size = key_len;
    memcpy(entry.key->data, key, key_len);

    if (set_one(*self, &entry, data)) {
        dbg("map_set: error in set_one\n");
        assert(0);
        exit(1);
    }
}
void map_set_ptr(hashmap **self, void const *key, u8 key_len, void const *data) {
    return map_set(self, key, key_len, &data);
}

void map_set_v(hashmap **self, void const *key, u8 key_len, void const *data) {
    assert((*self)->value_size <= sizeof(void *));
    map_set(self, key, key_len, &data);
}

void str_map_set_v(hashmap **self, str key, void const *data) {
    assert((*self)->value_size <= sizeof(void *));
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    map_set(self, s.buf, s.len, &data);
}

void str_map_set(hashmap **self, str key, void const *data) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    map_set(self, s.buf, s.len, data);
}

void str_map_set_ptr(hashmap **self, str key, void const *data) {
    return str_map_set(self, key, &data);
}

str_array str_map_sorted_keys(allocator *alloc, hashmap *self) {
    str_array out = {.alloc = alloc};
    array_reserve(out, self->n_occupied);
    hashmap_iterator iter = {0};
    while (map_iter(self, &iter)) {
        str key = str_init_n(alloc, iter.key_ptr, iter.key_size);
        array_push(out, key);
    }
    array_shrink(out);
    str_sized_sort((str_sized)sized_all(out));
    return out;
}

void *map_get(hashmap *map, void const *key, u8 key_len) {
    // returns pointer to value or null
    hashmap_entry *cell = map_find(map, key, key_len);
    if (!cell) return null;
    return cell->data;
}
void *map_get_ptr(hashmap *map, void const *key, u8 key_len) {
    void **res = map_get(map, key, key_len);
    return res ? *res : null;
}

void *str_map_get(hashmap *self, str key) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    return map_get(self, s.buf, s.len);
}
void *str_map_get_ptr(hashmap *self, str key) {
    void **res = str_map_get(self, key);
    return res ? *res : null;
}

void map_erase(hashmap *map, void const *key, u8 key_len) {
    hashmap_entry *cell = map_find(map, key, key_len);
    if (!cell) return;

    set_tombstone(&cell->status);

    alloc_free(map->key_alloc, cell->key);
    cell->key = null;

    map->n_occupied--;
}

void str_map_erase(hashmap *self, str key) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    return map_erase(self, s.buf, s.len);
}

void map_reset(hashmap *map) {
    if (!map) return;
    if (map->n_occupied) {
        memset(map->entries, 0, map->n_cells * hashmap_entry_size(map));
        map->n_occupied = 0;
    }
}

// -- hash set --

//

hashmap *hset_create(allocator *alloc, u32 n_buckets) {
    return map_create(alloc, sizeof(int), n_buckets);
}

void hset_destroy(hashmap **self) {
    map_destroy(self);
}

void hset_insert(hashmap **self, void const *key, u8 len) {
    int one = 1;
    map_set(self, key, len, &one);
}

void str_hset_insert(hashmap **self, str key) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    return hset_insert(self, s.buf, s.len);
}

void ptr_hset_insert(hashmap **self, void const *key) {
    return hset_insert(self, &key, sizeof(void *));
}

int hset_contains(hashmap const *self, void const *key, u8 len) {
    return map_contains(self, key, len);
}

int str_hset_contains(hashmap const *self, str key) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    return map_contains(self, s.buf, s.len);
}
int ptr_hset_contains(hashmap const *self, void const *key) {
    return map_contains(self, &key, sizeof(void *));
}

int hset_is_subset(hashmap const *super, hashmap const *sub) {
    hashmap_iterator iter = {0};
    while (map_iter(sub, &iter)) {
        if (!hset_contains(super, iter.key_ptr, iter.key_size)) return 0;
    }
    return 1;
}

void hset_remove(hashmap *self, void const *key, u8 len) {
    map_erase(self, key, len);
}

void ptr_hset_remove(hashmap *self, void const *key) {
    map_erase(self, &key, sizeof(void *));
}

void str_hset_remove(hashmap *self, str key) {
    span s = str_span(&key);
    assert(s.len < UINT8_MAX);
    map_erase(self, s.buf, s.len);
}

void hset_reset(hashmap *self) {
    map_reset(self);
}

size_t hset_size(hashmap const *self) {
    return map_size(self);
}

hashmap *hset_of_str(allocator *alloc, str_sized in) {
    hashmap *out = hset_create(alloc, (u32)(in.size * DEFAULT_LOAD_FACTOR));
    forall(i, in) {
        span s = str_span(&in.v[i]);
        hset_insert(&out, s.buf, s.len);
    }
    return out;
}

int hset_iter(hashmap const *self, hashmap_iterator *iter) {
    int res    = map_iter(self, iter);
    iter->data = null;
    return res;
}
