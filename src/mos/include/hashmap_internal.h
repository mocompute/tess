#ifndef MOS_HASHMAP_INTERNAL_H
#define MOS_HASHMAP_INTERNAL_H

#include "alloc.h"
#include "types.h"

#include <stdalign.h>
#include <stddef.h>

#define HASHMAP_MAX_ELEMENT_SIZE 24

struct hashmap_entry {
    struct hashmap_key *key;
    u8                  status;
    alignas(sizeof(void *)) byte data[]; // size: hashmap.value_size
};

struct hashmap_key {
    u16  size;
    byte data[];
};

struct hashmap {

    allocator *parent_alloc;
    allocator *key_alloc;

    u32        n_cells;
    u32        n_occupied;

    u16        value_size;
    u16        aligned_value_size;

    alignas(struct hashmap_entry) byte entries[];
};

struct hashmap_iterator {
    u32 index;
};

#endif
