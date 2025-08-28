#ifndef MOS_HASHMAP_INTERNAL_H
#define MOS_HASHMAP_INTERNAL_H

#include "types.h"

#include <stdalign.h>
#include <stddef.h>

#define HASHMAP_MAX_ELEMENT_SIZE 24

struct hashmap_entry {
    u16  key_len;
    u16  data_len;
    u8   status;
    byte data[];
};

struct hashmap {

    u32 n_cells;
    u32 n_occupied;
    f32 max_load_factor;

    alignas(struct hashmap_entry) byte data[]; // map_header + aligned_element_size
};

#endif
