#ifndef MOS_VECTOR_H
#define MOS_VECTOR_H

#include "alloc.h"

#include <stdbool.h>

typedef struct mos_vector_t {
  size_t element_size;
  size_t capacity;
  size_t size;
  char  *data;
} mos_vector_t;

// -- allocation and deallocation --

mos_vector_t *mos_vector_alloc(mos_allocator_t *);
void          mos_vector_dealloc(mos_allocator_t *, mos_vector_t *);
void          mos_vector_init(mos_vector_t *, size_t element_size);
void          mos_vector_deinit(mos_allocator_t *, mos_vector_t *);
int           mos_vector_reserve(mos_allocator_t *, mos_vector_t *, size_t);

// -- read-only access --

size_t        mos_vector_size(mos_vector_t const *);
size_t        mos_vector_capacity(mos_vector_t const *);
bool          mos_vector_empty(mos_vector_t const *);

// -- data and iterator access --

char         *mos_vector_data(mos_vector_t *);
char         *mos_vector_begin(mos_vector_t *);
char         *mos_vector_end(mos_vector_t *);
void         *mos_vector_at(mos_vector_t *, size_t);
void         *mos_vector_back(mos_vector_t *);

// -- insertion and removal --

int           mos_vector_push_back(mos_allocator_t *, mos_vector_t *, void const *element);
int           mos_vector_copy_back(mos_allocator_t *, mos_vector_t *, void const *start, size_t count);
void          mos_vector_pop_back(mos_vector_t *);
void          mos_vector_erase(mos_vector_t *, char *);

// -- association lists --
//
// element_size must be >= sizeof(size_t), which is the association
// key. May contain duplicate values, but [get] and [erase] operate on
// the first one found, searching from the back.

void          mos_vector_assoc_set(mos_allocator_t *, mos_vector_t *, void const *);
char         *mos_vector_assoc_get(mos_vector_t *, size_t);
void          mos_vector_assoc_erase(mos_vector_t *, size_t);

#endif
