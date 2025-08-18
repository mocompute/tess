#include "vector.h"

/* #include "alloc.h" */

#include <stdio.h>
#include <stdlib.h>

int test_vector(void) {
  int error = 0;

  mos_allocator_t *alloc = mos_alloc_default_allocator();

  mos_vector_t *vec = mos_vector_alloc(alloc);
  mos_vector_init(vec, sizeof(int));
  mos_vector_reserve(alloc, vec, 2);

  error += mos_vector_empty(vec) == 1 ? 0 : 1;

  int val = 123;
  error += mos_vector_push_back(alloc, vec, &val) == 0 ? 0 : 1;
  error += mos_vector_empty(vec) == 0 ? 0 : 1;
  error += *(int *)(mos_vector_back(vec)) == 123 ? 0 : 1;

  val = 456;
  error += mos_vector_push_back(alloc, vec, &val) == 0 ? 0 : 1;
  error += mos_vector_empty(vec) == 0 ? 0 : 1;
  error += *(int *)(mos_vector_back(vec)) == 456 ? 0 : 1;

  mos_vector_pop_back(vec);
  error += mos_vector_empty(vec) == 0 ? 0 : 1;
  error += *(int *)(mos_vector_back(vec)) == 123 ? 0 : 1;

  mos_vector_pop_back(vec);
  error += mos_vector_empty(vec) == 1 ? 0 : 1;

  int data[] = {321, 234, 654};
  mos_vector_copy_back(alloc, vec, data, 3);
  error += 3 == mos_vector_size(vec) ? 0 : 1;
  error += 3 <= mos_vector_capacity(vec) ? 0 : 1;
  error += ((int *)mos_vector_data(vec))[0] == 321 ? 0 : 1;
  error += ((int *)mos_vector_data(vec))[1] == 234 ? 0 : 1;
  error += ((int *)mos_vector_data(vec))[2] == 654 ? 0 : 1;

  mos_vector_deinit(alloc, vec);
  mos_vector_dealloc(alloc, vec);

  return error;
}

int test_assoc(void) {
  int error = 0;

  mos_allocator_t *alloc = mos_alloc_default_allocator();

  mos_vector_t *vec = mos_vector_alloc(alloc);
  mos_vector_init(vec, 2 * sizeof(void *));

  mos_vector_assoc(alloc, vec, (void *)1, (void *)2);
  error += (void *)2 == *mos_vector_assoc_get(vec, (void *)1) ? 0 : 1;

  mos_vector_assoc(alloc, vec, (void *)2, (void *)3);
  error += (void *)3 == *mos_vector_assoc_get(vec, (void *)2) ? 0 : 1;
  mos_vector_assoc(alloc, vec, (void *)1, (void *)2);
  error += (void *)2 == *mos_vector_assoc_get(vec, (void *)1) ? 0 : 1;

  mos_vector_assoc(alloc, vec, (void *)1, (void *)30);
  error += (void *)30 == *mos_vector_assoc_get(vec, (void *)1) ? 0 : 1;
  error += (void *)3 == *mos_vector_assoc_get(vec, (void *)2) ? 0 : 1;

  error += 0 == mos_vector_assoc_get(vec, (void *)999) ? 0 : 1;

  mos_vector_deinit(alloc, vec);
  mos_vector_dealloc(alloc, vec);

  return error;
}

int main(void) {
  int error = 0;

  error += test_vector();
  error += test_assoc();

  return error;
}
