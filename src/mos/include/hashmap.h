#ifndef MOS_HASHMAP_H
#define MOS_HASHMAP_H

#include "hashmap_internal.h"

#include "alloc.h"
#include "nodiscard.h"
#include "types.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

// -- hash map --
//
// A hash map with u32 keys and smallish (up to 24 byte) values.
// Note that keys are u32 because they should be a good hash
// selected by the client.
//
// Each cell places the key in a header, followed by client's
// element_size bytes of data.

typedef struct hashmap hashmap;
typedef u32            hashmap_key;

// -- allocation and deallocation --

nodiscard hashmap *map_create(allocator *, u8 element_size, u32 buckets, f32 max_load_factor) mallocfun;
void               map_destroy(allocator *, hashmap **);
nodiscard hashmap *map_copy(allocator *, hashmap const *) mallocfun;

// -- read-only access --

size_t map_size(hashmap const *);
bool   map_empty(hashmap const *);
f32    map_load_factor(hashmap const *);

// -- data and iterator access --
//
// Data cell includes header struct.

hashmap_element_header *map_unchecked_at(hashmap *, hashmap_key);

// -- insertion and removal --

nodiscard int map_set(allocator *, hashmap **, u32 key, void *data);
void         *map_get(hashmap *, hashmap_key);
void          map_erase(hashmap *, hashmap_key);

// -- utilities --

u32 map_next_power_of_two(u32);

#endif
