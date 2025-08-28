#ifndef MOS_HASHMAP_H
#define MOS_HASHMAP_H

#include "hashmap_internal.h"

#include "alloc.h"
#include "nodiscard.h"
#include "types.h"

#include <assert.h>
#include <stdbool.h>

// -- hash map --

typedef struct hashmap hashmap;

// -- allocation and deallocation --

nodiscard hashmap *map_create(allocator *) mallocfun;
void               map_destroy(allocator *, hashmap **);
nodiscard hashmap *map_copy(allocator *, hashmap const *) mallocfun;

// -- read-only access --

size_t map_size(hashmap const *);
bool   map_empty(hashmap const *);
f32    map_load_factor(hashmap const *);

// -- data and iterator access --

// -- insertion and removal --

void map_set(allocator *, hashmap **, byte const *key, u16 key_len, byte const *data, u16 data_len);
void map_contains(hashmap *, byte const *key, u16 key_len);
void map_get(hashmap *, byte const *key, u16 key_len, byte *data, u16 *data_len);
void map_erase(hashmap *, byte const *key, u16 key_len);

// -- utilities --

u32 map_next_power_of_two(u32);

#endif
