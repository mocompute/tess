#include "map.h"
#include "alloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_LOAD_FACTOR 0.75f
#define MAX_PROBE_LEN       (1 << 6) - 1

typedef struct status_t {
  uint8_t occupied       : 1;
  uint8_t tombstone      : 1;
  uint8_t probe_distance : 6;
} status_t;

// The header of each entry
typedef struct header_t {
  size_t key;
} header_t;

struct mos_map_t {
  size_t    element_size;
  size_t    aligned_element_size;
  char     *data;
  status_t *status;

  // buffers size of aligned_element_size + sizeof(header_t), used during robin hood
  // swapping
  char     *to_store;
  char     *tmp;

  float     max_load_factor;
  uint32_t  buckets;
  uint32_t  occupied;
};

// -- statics --

static size_t bucket_size(mos_map_t const *map) {
  return map->aligned_element_size + sizeof(header_t);
}

static uint32_t key_to_bucket(mos_map_t const *map, size_t key) {
  return key % map->buckets;
}

static uint32_t incr_index(mos_map_t const *map, uint32_t index) {
  return (index + 1) % map->buckets;
}

// Returns: if key exists, pointer to header. Sets out_index to found
// bucket index.
static char *map_find(mos_map_t *map, size_t key, uint32_t *out_index) {
  uint32_t index          = key_to_bucket(map, key);

  uint32_t probe_distance = 0;

  while (1) {
    if (probe_distance++ > MAX_PROBE_LEN) return 0;

    status_t status = map->status[index];

    if (status.tombstone) {
      // keep looking
      index = incr_index(map, index);
    } else if (status.occupied) {
      char *const cell      = mos_map_unchecked_at(map, index);
      size_t      found_key = *(size_t *)cell;

      if (found_key != key) {
        // keep looking
        index = incr_index(map, index);
      } else {
        // found
        if (out_index) *out_index = (uint32_t)index;
        return cell;
      }
    } else {
      // target bucket is empty and not a tombstone
      return 0;
    }
  }
}

static int set_one(mos_map_t *map, size_t const key, char const *element) {

  size_t const cell_size = sizeof(header_t) + map->element_size;
  assert(bucket_size(map) >= cell_size);

  // write header and data to to_store
  memcpy(map->to_store, &key, sizeof key);
  memcpy(map->to_store + sizeof key, element, map->element_size);

  uint32_t      index           = key_to_bucket(map, key);
  unsigned char probe_distance  = 0;

  int           warning_printed = 0;

  while (1) {
    if (probe_distance > MAX_PROBE_LEN) return 1; // overflow

    if (probe_distance > 16 && !warning_printed) {
      fprintf(stderr, "warning: high probe distance for key: %zu, load factor: %f\n",
              *(size_t *)map->to_store, mos_map_load_factor(map));
      warning_printed = 1;
    }

    if (map->status[index].occupied) {
      if (probe_distance <= map->status[index].probe_distance) {
        // continue probing
        ++probe_distance;
        index = incr_index(map, index);
      } else {
        // swap
        char *const cell = mos_map_unchecked_at(map, index);

        // copy evicted cell (header + data) to tmp
        memcpy(map->tmp, cell, cell_size);
        // write header (from to_store) in place of evicted cell
        memcpy(cell, map->to_store, cell_size);
        // write evicted cell (header + data) into to_store
        memcpy(map->to_store, map->tmp, cell_size);

        uint8_t evicted_probe_distance    = map->status[index].probe_distance;
        map->status[index].probe_distance = probe_distance;

        // continue probing with swapped element, using evicted key
        warning_printed = 0;
        probe_distance  = evicted_probe_distance + 1;
        index           = incr_index(map, index);
      }
    } else {
      // found an empty slot
      map->status[index].occupied       = 1;
      map->status[index].tombstone      = 0;
      map->status[index].probe_distance = probe_distance;
      map->occupied++;

      char *const cell = mos_map_unchecked_at(map, index);

      // write cell (header + data) to bucket
      memcpy(cell, map->to_store, cell_size);
      return 0;
    }
  }
  return 0;
}

static int set_one_cell(mos_map_t *map, char *cell) {
  return set_one(map, *(size_t *)cell, cell + sizeof(size_t));
}

static int grow_buckets(mos_allocator_t *alloc, mos_map_t *map) {
  // make a new map with 2x the number of buckets. Copy all data to
  // the new map. Then release the old map's buffers, and overwrite
  // its struct with the new map.

  size_t new_buckets = (size_t)map->buckets * 2U;

  if (new_buckets > UINT32_MAX) return 1;

  mos_map_t new_map;
  if (mos_map_init(alloc, &new_map, map->element_size, (uint32_t)new_buckets, map->max_load_factor))
    return 1;

  for (uint32_t i = 0; i < map->buckets; ++i) {
    if (map->status[i].occupied) {
      char *cell = mos_map_unchecked_at(map, i);

      if (set_one_cell(&new_map, cell)) return 1;
    }
  }

  // free old map buffers and overwrite pointers with new map's pointers
  mos_map_deinit(alloc, map);
  memcpy(map, &new_map, sizeof *map);

  return 0;
}

// -- externals --

float mos_map_load_factor(mos_map_t const *map) {
  return (float)map->occupied / (float)map->buckets;
}

size_t mos_map_size(mos_map_t const *map) {
  return map->occupied;
}

bool mos_map_empty(mos_map_t const *map) {
  return map->occupied == 0;
}

// Returns pointer to cell, whether or not it is valid, occupied, etc.
char *mos_map_unchecked_at(mos_map_t *map, uint32_t index) {
  return map->data + index * bucket_size(map);
}

// Returns: argument aligned to next word size.
size_t mos_align_to_word_size(size_t n) {
  size_t mask = sizeof(void *) - 1;
  return (n + mask) & ~mask;
}

// Returns: input if already a power of two, or else the next higher
// power of two.
uint32_t mos_map_next_power_of_two(uint32_t n) {

  if (n > (1U << 31)) return 0; // overflow
  if (n == 0) return 1;

  // set all bits to the right of the highest set bit by masking.
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return n + 1;
}

//

mos_map_t *mos_map_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(mos_map_t));
}

void mos_map_dealloc(mos_allocator_t *alloc, mos_map_t *p) {
  alloc->free(p);
}

int mos_map_init(mos_allocator_t *alloc, mos_map_t *map, size_t element_size, uint32_t buckets,
                 float max_load_factor) {

  assert(sizeof(size_t) == sizeof(header_t));
  assert(sizeof(status_t) == 1);
  assert(element_size <= PTRDIFF_MAX);

  buckets = mos_map_next_power_of_two(buckets);
  if (max_load_factor < 0.01) max_load_factor = DEFAULT_LOAD_FACTOR;

  memset(map, 0, sizeof *map);
  map->element_size         = element_size;
  map->aligned_element_size = mos_align_to_word_size(element_size);
  map->buckets              = buckets;
  map->max_load_factor      = max_load_factor;
  map->data                 = alloc->malloc(buckets * bucket_size(map));
  map->status               = alloc->calloc(buckets, sizeof(status_t));
  map->to_store             = alloc->malloc(map->aligned_element_size + sizeof(header_t));
  map->tmp                  = alloc->malloc(map->aligned_element_size + sizeof(header_t));
  if (!map->data || !map->status || !map->to_store || !map->tmp) return 1;

  return 0;
}

void mos_map_deinit(mos_allocator_t *alloc, mos_map_t *map) {
  alloc->free(map->to_store);
  alloc->free(map->tmp);
  alloc->free(map->status);
  alloc->free(map->data);
}

int mos_map_set(mos_allocator_t *alloc, mos_map_t *map, size_t key, void *data) {

  // Must check for existing key. Replace if present.
  void *existing = mos_map_get(map, key);
  if (existing) {
    memcpy(existing, data, map->element_size);
    return 0;
  }

  if (mos_map_load_factor(map) >= map->max_load_factor) {
    if (grow_buckets(alloc, map)) return 1;
  }

  return set_one(map, key, data);
}

void *mos_map_get(mos_map_t *map, size_t key) {
  char *cell = map_find(map, key, 0);
  if (!cell) return 0;
  return cell + sizeof(header_t);
}

void mos_map_erase(mos_map_t *map, size_t key) {
  uint32_t index;
  char    *p = map_find(map, key, &index);
  if (!p) return;

  map->status[index].occupied  = 0;
  map->status[index].tombstone = 1;
  map->occupied--;
}
