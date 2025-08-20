#ifndef MOS_VECTOR_H
#define MOS_VECTOR_H

#include "alloc.h"
#include "nodiscard.h"

#include <stdbool.h>

// -- mos_vector struct --
//
// Consider the fields read-only, except for data, which clients may
// set to 0 in certain situations. In that case, client must call
// [clear] to set the size to zero.
typedef struct mos_vector {
  size_t element_size;
  size_t capacity;
  size_t size;
  char  *data;
} mos_vector;

// -- allocation and deallocation --

mos_vector   *mos_vector_alloc(mos_allocator *);
void          mos_vector_dealloc(mos_allocator *, mos_vector **);
void          mos_vector_init(mos_vector *, size_t element_size);
void          mos_vector_deinit(mos_allocator *, mos_vector *);
nodiscard int mos_vector_reserve(mos_allocator *, mos_vector *, size_t);
void          mos_vector_move(mos_vector *dst, mos_vector *src);

// -- read-only access --

size_t mos_vector_size(mos_vector const *);
size_t mos_vector_capacity(mos_vector const *);
bool   mos_vector_empty(mos_vector const *);

// -- data and iterator access --

char       *mos_vector_data(mos_vector *);
void       *mos_vector_begin(mos_vector *);
void const *mos_vector_end(mos_vector *);
void       *mos_vector_at(mos_vector *, size_t);
void       *mos_vector_back(mos_vector *);

// -- insertion and removal --

nodiscard int mos_vector_push_back(mos_allocator *, mos_vector *, void const *element);
nodiscard int mos_vector_copy_back(mos_allocator *, mos_vector *, void const *start, size_t count);
void          mos_vector_pop_back(mos_vector *);
void          mos_vector_erase(mos_vector *, char *);
nodiscard int mos_vector_resize(mos_allocator *, mos_vector *, size_t);
void          mos_vector_clear(mos_vector *);

// -- association lists --
//
// element_size must be >= sizeof(size_t), which is the association
// key. May contain duplicate values, but [get] and [erase] operate on
// the first one found, searching from the back.

nodiscard int mos_vector_assoc_set(mos_allocator *, mos_vector *, void const *);
char         *mos_vector_assoc_get(mos_vector *, size_t);
void          mos_vector_assoc_erase(mos_vector *, size_t);

#endif
