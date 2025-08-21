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

typedef struct mos_map mos_map;

typedef struct mos_map_header {
  size_t key;
} mos_map_header_t;

// -- allocation and deallocation --

mos_map      *mos_map_alloc(allocator *);
void          mos_map_dealloc(allocator *, mos_map **);
nodiscard int mos_map_init(allocator *, mos_map *, size_t element_size, uint32_t buckets,
                           float max_load_factor);
void          mos_map_deinit(allocator *, mos_map *);

// -- read-only access --

size_t mos_map_size(mos_map const *);
bool   mos_map_empty(mos_map const *);
float  mos_map_load_factor(mos_map const *);

// -- data and iterator access --
//
// Data cell includes a size_t header, which is the key used to store the item.
//

char *mos_map_unchecked_at(mos_map *, uint32_t);

// -- insertion and removal --

nodiscard int mos_map_set(allocator *, mos_map *, size_t key, void *data);
void         *mos_map_get(mos_map *, size_t);
void          mos_map_erase(mos_map *, size_t);

// -- utilities --

uint32_t mos_map_next_power_of_two(uint32_t);

#endif
