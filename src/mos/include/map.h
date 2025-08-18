#ifndef MOS_MAP_H
#define MOS_MAP_H

#include "alloc.h"

#include <stdint.h>

// A hash map with size_t keys and aribitrary-sized values. Note that
// keys are size_t because they should be a good hash selected by the
// client.

typedef struct mos_map_t mos_map_t;

mos_map_t *mos_map_alloc(mos_allocator_t *);
void mos_map_dealloc(mos_allocator_t *, mos_map_t *);

// [max_load_factor]: between 0 and 100. If 0, map will use a default
// value. Returns 1 on allocation error.
int mos_map_init(mos_allocator_t *, mos_map_t *, size_t element_size, uint32_t buckets,
                 float max_load_factor);

void mos_map_deinit(mos_allocator_t *, mos_map_t *);

// Set map key to data, replacing if key already exists. Returns 1 on
// allocation error.
int mos_map_set(mos_allocator_t *, mos_map_t *, size_t key, void *data);
void *mos_map_get(mos_map_t *, size_t);
void mos_map_erase(mos_map_t *, size_t);

// Returns: input if already a power of two, or else the next higher
// power of two.
uint32_t mos_map_next_power_of_two(uint32_t);

// Returns: argument aligned to next word size.
size_t mos_align_to_word_size(size_t);

#endif
