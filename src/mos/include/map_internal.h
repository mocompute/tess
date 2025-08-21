#ifndef MOS_MAP_INTERNAL_H
#define MOS_MAP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t occupied       : 1;
  uint8_t tombstone      : 1;
  uint8_t probe_distance : 6;
} map_bucket_status;

struct map {
  size_t             element_size;
  size_t             aligned_element_size;
  char              *data;
  map_bucket_status *status;

  // buffers size of aligned_element_size + sizeof(header_t), used during robin hood
  // swapping
  char    *to_store;
  char    *tmp;

  float    max_load_factor;
  uint32_t buckets;
  uint32_t occupied;
};

typedef struct {
  size_t key;
} map_header;

#endif
