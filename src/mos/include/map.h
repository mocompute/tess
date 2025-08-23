#ifndef MOS_MAP_H
#define MOS_MAP_H

#include "map_internal.h"

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

typedef struct map map_t;
typedef u32        map_key;

// -- allocation and deallocation --

nodiscard map_t *map_create(allocator *, u8 element_size, u32 buckets, f32 max_load_factor);
void             map_destroy(allocator *, map_t **);

// -- read-only access --

size_t map_size(map_t const *);
bool   map_empty(map_t const *);
f32    map_load_factor(map_t const *);

// -- data and iterator access --
//
// Data cell includes header struct.

map_header *map_unchecked_at(map_t *, map_key);

// -- insertion and removal --

nodiscard int map_set(allocator *, map_t **, u32 key, void *data);
void         *map_get(map_t *, map_key);
void          map_erase(map_t *, map_key);

// -- utilities --

u32 map_next_power_of_two(u32);

#endif
