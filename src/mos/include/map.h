#ifndef MOS_MAP_H
#define MOS_MAP_H

#include "alloc.h"
#include "nodiscard.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

// -- hash map --
//
// A hash map with size_t keys and aribitrary-sized values. Note that
// keys are size_t because they should be a good hash selected by the
// client.
//
// Each cell places the key in a header, followed by client's
// element_size bytes of data.

typedef struct map map_t;

// -- allocation and deallocation --

map_t        *map_alloc(allocator *);
void          map_dealloc(allocator *, map_t **);
nodiscard int map_init(allocator *, map_t *, size_t element_size, uint32_t buckets, float max_load_factor);
void          map_deinit(allocator *, map_t *);

// -- read-only access --

size_t map_size(map_t const *);
bool   map_empty(map_t const *);
float  map_load_factor(map_t const *);

// -- data and iterator access --
//
// Data cell includes a size_t header, which is the key used to store the item.
//

char *map_unchecked_at(map_t *, uint32_t);

// -- insertion and removal --

nodiscard int map_set(allocator *, map_t *, size_t key, void *data);
void         *map_get(map_t *, size_t);
void          map_erase(map_t *, size_t);

// -- utilities --

uint32_t map_next_power_of_two(uint32_t);

#endif
