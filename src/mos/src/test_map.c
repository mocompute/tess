#include "map.h"
#include "vector.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

int test_power_of_two(void) {
  int error = 0;

  error += 1 == mos_map_next_power_of_two(0) ? 0 : 1;
  error += 1 == mos_map_next_power_of_two(1) ? 0 : 1;
  error += 2 == mos_map_next_power_of_two(2) ? 0 : 1;
  error += 4 == mos_map_next_power_of_two(3) ? 0 : 1;
  error += 4 == mos_map_next_power_of_two(4) ? 0 : 1;
  error += 8 == mos_map_next_power_of_two(5) ? 0 : 1;
  error += 8 == mos_map_next_power_of_two(8) ? 0 : 1;
  error += 16 == mos_map_next_power_of_two(9) ? 0 : 1;
  error += 16 == mos_map_next_power_of_two(16) ? 0 : 1;
  error += 32 == mos_map_next_power_of_two(17) ? 0 : 1;

  return error;
}

int test_align(void) {
  int error = 0;

  assert(8 == sizeof(void *));

  error += 8 == mos_align_to_word_size(2) ? 0 : 1;
  error += 8 == mos_align_to_word_size(4) ? 0 : 1;
  error += 8 == mos_align_to_word_size(6) ? 0 : 1;
  error += 8 == mos_align_to_word_size(8) ? 0 : 1;
  error += 16 == mos_align_to_word_size(9) ? 0 : 1;
  return error;
}

int test_map(void) {
  int error = 0;

  mos_allocator_t *alloc = mos_alloc_default_allocator();

  mos_map_t *map = mos_map_alloc(alloc);

  if (mos_map_init(alloc, map, sizeof(int), 8, 0)) return 1;

  int data = 0;

  error += 0 == mos_map_get(map, 0) ? 0 : 1;

  data = 123;
  error += 0 == mos_map_set(alloc, map, 0, &data) ? 0 : 1;
  error += 123 == *(int *)mos_map_get(map, 0) ? 0 : 1;

  error += 0 == mos_map_get(map, 1) ? 0 : 1;
  data = 456;
  error += 0 == mos_map_set(alloc, map, 1, &data) ? 0 : 1;
  error += 456 == *(int *)mos_map_get(map, 1) ? 0 : 1;

  mos_map_erase(map, 0);
  error += 0 == mos_map_get(map, 0) ? 0 : 1;
  error += 456 == *(int *)mos_map_get(map, 1) ? 0 : 1;

  mos_map_deinit(alloc, map);
  mos_map_dealloc(alloc, map);

  return error;
}

int test_big_map(void) {
  int error = 0;

  size_t N = 1000000;

  typedef struct pair_t {
    int left, right;
  } pair_t;

  mos_allocator_t *alloc = mos_alloc_default_allocator();
  mos_vector_t vec;
  mos_vector_init(&vec, sizeof(pair_t));

  for (size_t i = 0; i < N; ++i) {
    pair_t pair = {rand(), rand()};
    if (mos_vector_push_back(alloc, &vec, &pair)) { return 1; }
  }

  mos_map_t *map = mos_map_alloc(alloc);
  mos_map_init(alloc, map, sizeof(int), 1024, 0); // TODO

  // insert data into map
  for (size_t i = 0; i < N; ++i) {
    pair_t *pair = mos_vector_at(&vec, i);
    if (mos_map_set(alloc, map, (size_t)pair->left, &pair->right)) return 1;
  }

  mos_map_deinit(alloc, map);
  mos_map_dealloc(alloc, map);

  mos_vector_deinit(alloc, &vec);
  return error;
}

#define T(name)                                                                        \
  this_error = name();                                                                 \
  if (this_error) {                                                                    \
    fprintf(stderr, "FAILED: %s\n", #name);                                            \
    error += this_error;                                                               \
  }

int main(void) {
  int error = 0;
  int this_error = 0;

  srand((uint)time(0));

  T(test_power_of_two);
  T(test_align);
  T(test_map);
  T(test_big_map);

  return error;
}
