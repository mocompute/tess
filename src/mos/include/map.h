#ifndef MOS_MAP_H
#define MOS_MAP_H

#include "alloc.h"

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

typedef struct mos_map mos_map_t;

typedef struct mos_map_header {
  size_t key;
} mos_map_header_t;

// -- allocation and deallocation --

mos_map_t *mos_map_alloc(mos_allocator_t *);
void       mos_map_dealloc(mos_allocator_t *, mos_map_t *);
int        mos_map_init(mos_allocator_t *, mos_map_t *, size_t element_size, uint32_t buckets,
                        float max_load_factor);
void       mos_map_deinit(mos_allocator_t *, mos_map_t *);

// -- read-only access --

size_t mos_map_size(mos_map_t const *);
bool   mos_map_empty(mos_map_t const *);
float  mos_map_load_factor(mos_map_t const *);

// -- data and iterator access --
//
// Data cell includes a size_t header, which is the key used to store the item.
//

char *mos_map_unchecked_at(mos_map_t *, uint32_t);

// -- insertion and removal --

int   mos_map_set(mos_allocator_t *, mos_map_t *, size_t key, void *data);
void *mos_map_get(mos_map_t *, size_t);
void  mos_map_erase(mos_map_t *, size_t);

// -- utilities --

uint32_t mos_map_next_power_of_two(uint32_t);
size_t   mos_align_to_word_size(size_t);

#endif
