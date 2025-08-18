#include "map.h"
#include "alloc.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#define DEFAULT_LOAD_FACTOR 0.80f
#define MAX_PROBE_LEN (1 << 6) - 1

typedef struct status_t {
  uint occupied : 1;
  uint tombstone : 1;
  uint probe_distance : 6;
} status_t;

// The header of each entry
typedef struct header_t {
  size_t key;
} header_t;

struct mos_map_t {
  size_t element_size;
  size_t aligned_element_size;
  char *data;
  status_t *status;

  // buffers size of element_size, used during robin hood swapping
  char *to_store;
  char *tmp;

  float max_load_factor;
  uint32_t buckets;
  uint32_t occupied;
};

static float load_factor(mos_map_t *map) {
  return (float)map->occupied / (float)map->buckets;
}

static size_t bucket_size(mos_map_t const *map) {
  return map->aligned_element_size + sizeof(header_t);
}

size_t mos_align_to_word_size(size_t n) {
  size_t mask = sizeof(void *) - 1;
  return (n + mask) & ~mask;
}

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

void mos_map_dealloc(mos_allocator_t *alloc, mos_map_t *p) { alloc->free(p); }

int mos_map_init(mos_allocator_t *alloc, mos_map_t *map, size_t element_size,
                 uint32_t buckets, float max_load_factor) {

  assert(element_size <= PTRDIFF_MAX);

  buckets = mos_map_next_power_of_two(buckets);
  if (max_load_factor < 0.01) max_load_factor = DEFAULT_LOAD_FACTOR;

  memset(map, 0, sizeof *map);
  map->element_size = element_size;
  map->aligned_element_size = mos_align_to_word_size(element_size);
  map->buckets = buckets;
  map->max_load_factor = max_load_factor;
  map->data = alloc->malloc(buckets * bucket_size(map));
  map->status = alloc->calloc(buckets, sizeof *map->status);
  map->to_store = alloc->malloc(element_size);
  map->tmp = alloc->malloc(element_size);
  if (!map->data || !map->status || !map->to_store || !map->tmp) return 1;

  return 0;
}

void mos_map_deinit(mos_allocator_t *alloc, mos_map_t *map) {
  alloc->free(map->to_store);
  alloc->free(map->tmp);
  alloc->free(map->status);
  alloc->free(map->data);
}

int set_one(mos_map_t *map, size_t key, char const *element) {

  memcpy(map->to_store, element, map->element_size);

  uint32_t index = key % map->buckets;
  uint probe_distance = 0;

  while (1) {
    if (probe_distance > MAX_PROBE_LEN) return 1; // overflow

    if (map->status[index].occupied) {
      if (probe_distance <= map->status[index].probe_distance) {
        // continue probing
        ++probe_distance;
        index = (index + 1) % map->buckets;
      } else {
        // swap
        char *const p = map->data + index * bucket_size(map);

        memcpy(map->tmp, p, map->element_size);
        memcpy(p, map->to_store, map->element_size);
        memcpy(map->to_store, map->tmp, map->element_size);

        uint evicted_probe_distance = map->status[index].probe_distance;
        map->status[index].probe_distance = probe_distance;

        // continue probing with swapped element
        probe_distance = evicted_probe_distance + 1;
        index = (index + 1) % map->buckets;
      }
    } else {
      // found an empty slot
      map->status[index].occupied = 1;
      map->status[index].probe_distance = probe_distance;
      map->occupied++;

      char *const p = map->data + index * bucket_size(map);

      // write header
      memcpy(p, &key, sizeof key);
      // write element
      memcpy(p + sizeof key, map->to_store, map->element_size);
      break;
    }
  }
  return 0;
}

static int grow_buckets(mos_allocator_t *alloc, mos_map_t *map) {
  // make a new map with 2x the number of buckets. Copy all data to
  // the new map. Then release the old map's buffers, and overwrite
  // its struct with the new map.

  size_t new_buckets = (size_t)map->buckets * 2;
  if (new_buckets > UINT32_MAX) return 1;

  mos_map_t new_map;
  if (mos_map_init(alloc, &new_map, map->element_size, (uint32_t)new_buckets,
                   map->max_load_factor))
    return 1;

  for (size_t i = 0; i < map->buckets; ++i) {
    if (map->status[i].occupied) {
      char *el = map->data + i * bucket_size(map);
      set_one(&new_map, *(size_t *)el, el + sizeof(size_t));
    }
  }

  mos_map_deinit(alloc, map);
  memcpy(map, &new_map, sizeof *map);

  return 0;
}

int mos_map_set(mos_allocator_t *alloc, mos_map_t *map, size_t key, void *data) {

  if (load_factor(map) >= map->max_load_factor) {
    if (grow_buckets(alloc, map)) return 1;
  }

  return set_one(map, key, data);
}

char *map_find(mos_map_t *map, size_t key, uint32_t *out_index) {
  size_t index = key & map->buckets;

  while (1) {
    if (index > UINT32_MAX) return 0; // overflow
    status_t status = map->status[index];

    if (status.tombstone) {
      // keep looking
      ++index;
      continue;
    } else if (status.occupied) {
      char *const p = map->data + index * bucket_size(map);
      size_t found_key = *(size_t *)p;

      if (found_key != key) {
        // keep looking
        ++index;
        continue;
      } else {
        // found
        if (out_index) *out_index = (uint32_t)index;
        return p;
      }
    } else {
      // empty
      return 0;
    }
  }
}

void *mos_map_get(mos_map_t *map, size_t key) {
  char *p = map_find(map, key, 0);
  if (!p) return 0;
  return p + sizeof(header_t);
}

void mos_map_erase(mos_map_t *map, size_t key) {
  uint32_t index;
  char *p = map_find(map, key, &index);
  if (!p) return;

  map->status[index].occupied = 0;
  map->status[index].tombstone = 1;
  map->occupied--;
}
