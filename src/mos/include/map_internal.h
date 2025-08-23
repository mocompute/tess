#ifndef MOS_MAP_INTERNAL_H
#define MOS_MAP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define MAP_MAX_ELEMENT_SIZE 24

typedef struct {
    uint8_t occupied       : 1;
    uint8_t tombstone      : 1;
    uint8_t probe_distance : 6;
} map_cell_status;

typedef struct {
    uint32_t        key;
    map_cell_status status;
    char            pad[3];
    char            data[];
    // size = 8
} map_header;

struct map {
    uint8_t  element_size;
    uint8_t  aligned_element_size;

    float    max_load_factor;
    uint32_t n_cells;
    uint32_t n_occupied;
    char     data[]; // map_header + aligned_element_size
};

#endif
