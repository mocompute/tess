#ifndef MOS_VECTOR_H
#define MOS_VECTOR_H

#include "alloc.h"

typedef struct mos_vector_t {
  size_t element_size;
  size_t capacity;
  size_t size;
  char *data;
} mos_vector_t;

mos_vector_t *mos_vector_alloc(mos_allocator_t *);
void mos_vector_dealloc(mos_allocator_t *, mos_vector_t *);
void mos_vector_init(mos_vector_t *, size_t element_size);
void mos_vector_deinit(mos_allocator_t *, mos_vector_t *);

// Returns: 0 on success.
int mos_vector_reserve(mos_allocator_t *, mos_vector_t *, size_t);

// Returns: 1 if empty, or else 0.
int mos_vector_empty(mos_vector_t const *);

// Returns: 0 on success.
int mos_vector_push_back(mos_allocator_t *, mos_vector_t *, void const *element);

// Returns: 0 on success.
int mos_vector_copy_back(mos_allocator_t *, mos_vector_t *, void const *start,
                         size_t count);

// Returns: pointer to element at index. Undefined if index is out of range.
void *mos_vector_at(mos_vector_t *, size_t);

// Returns: pointer to back element. Undefined if vector is empty.
void *mos_vector_back(mos_vector_t *);

// Reduces vector size by one. Undefined if vector is empty.
void mos_vector_pop_back(mos_vector_t *);

// Erase element.
void mos_vector_erase(mos_vector_t *, char *);

char *mos_vector_data(mos_vector_t *);
size_t mos_vector_size(mos_vector_t const *);
size_t mos_vector_capacity(mos_vector_t const *);

//
// Association lists
//
// Vector element is understood to be a pair: the first field is a
// size_t, and the (optionally zero-length) remainder of the element
// size is the data.
//
// Association lists may contain duplicate values, but get() and
// erase() operate only on the first one found, searching from the
// back of the vector.
//

void mos_vector_assoc(mos_allocator_t *, mos_vector_t *, void const *);
char *mos_vector_assoc_get(mos_vector_t *, size_t);
void mos_vector_assoc_erase(mos_vector_t *, size_t);

#endif
