#ifndef MOS_HASHMAP_H
#define MOS_HASHMAP_H

#include "alloc.h"
#include "nodiscard.h"
#include "types.h"

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>

// -- hash map --

typedef struct hashmap hashmap;

typedef struct {
    u32 index;
} hashmap_iterator;

typedef struct {
    struct hashmap_key *key;
    u8                  status;
    alignas(sizeof(void *)) byte data[]; // size: hashmap.value_size
} hashmap_entry;

// -- allocation and deallocation --

nodiscard hashmap *map_create(allocator *, u16 value_size) mallocfun;
nodiscard hashmap *map_create_n(allocator *alloc, u16 value_size, u32 n_buckets) mallocfun;
void               map_destroy(hashmap **);
nodiscard hashmap *map_copy(hashmap const *) mallocfun;

// -- read-only access --

size_t map_size(hashmap const *);
bool   map_empty(hashmap const *);
f32    map_load_factor(hashmap const *);

// -- data and iterator access --

// -- insertion and removal --

void  map_set(hashmap **, void const *key, u16 key_len, void const *data);
bool  map_contains(hashmap *, void const *key, u16 key_len);
void *map_get(hashmap *, void const *key, u16 key_len);
void  map_erase(hashmap *, void const *key, u16 key_len);

// pass zero-init iterator to start
bool map_iter(hashmap const *, hashmap_iterator *, hashmap_entry **out);
bool map_citer(hashmap const *, hashmap_iterator *, hashmap_entry const **out);

// -- utilities --

u32 map_next_power_of_two(u32);

#endif
