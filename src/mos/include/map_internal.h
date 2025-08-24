#ifndef MOS_MAP_INTERNAL_H
#define MOS_MAP_INTERNAL_H

#include "types.h"

#include <stddef.h>

#define MAP_MAX_ELEMENT_SIZE 24

typedef struct {
    u32  key;
    u8   status;
    byte pad[3];
    byte data[];
    // size = 8
} hashmap_element_header;

struct hashmap {
    struct {
        u32 n_cells;
        u32 n_occupied;
        f32 max_load_factor;
        u8  element_size;
        u8  aligned_element_size;
        // aligned size = 16
    };

    byte data[]; // map_header + aligned_element_size
};

#endif
