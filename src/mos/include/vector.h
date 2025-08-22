#ifndef MOS_VECTOR_H
#define MOS_VECTOR_H

#include "alloc.h"
#include "nodiscard.h"

#include <stdbool.h>

// -- mos_vector struct --
//
// Consider the fields read-only, except for data, which clients may
// set to 0 in certain situations. In that case, client must call
// [clear] to set the size to zero. Prefer to use vec_move instead if
// possible.

typedef struct {
    size_t capacity;
    size_t size;
    char   data[];
} vec_data_header;

typedef struct vec {
    size_t           element_size;
    vec_data_header *data;
} vec_t;

// -- allocation and deallocation --

vec_t        *vec_alloc(allocator *);
void          vec_dealloc(allocator *, vec_t **);
void          vec_init_empty(vec_t *, size_t element_size);
nodiscard int vec_init(allocator *, vec_t *, size_t element_size, size_t initial_capacity);
void          vec_deinit(allocator *, vec_t *);
nodiscard int vec_reserve(allocator *, vec_t *, size_t);
void          vec_move(vec_t *dst, vec_t *src);

// -- read-only access --

size_t vec_size(vec_t const *);
size_t vec_capacity(vec_t const *);
bool   vec_empty(vec_t const *);

// -- data and iterator access --

char       *vec_data(vec_t *);
void       *vec_begin(vec_t *);
void const *vec_cbegin(vec_t const *);
void const *vec_end(vec_t *);
void       *vec_at(vec_t *, size_t);
void       *vec_back(vec_t *);

// -- insertion and removal --

nodiscard int vec_push_back(allocator *, vec_t *, void const *element);
nodiscard int vec_copy_back(allocator *, vec_t *, void const *start, size_t count);
void          vec_pop_back(vec_t *);
void          vec_erase(vec_t *, char *);
nodiscard int vec_resize(allocator *, vec_t *, size_t);
void          vec_clear(vec_t *);

// -- association lists --
//
// element_size must be >= sizeof(size_t), which is the association
// key. May contain duplicate values, but [get] and [erase] operate on
// the first one found, searching from the back.

nodiscard int vec_assoc_set(allocator *, vec_t *, void const *);
char         *vec_assoc_get(vec_t *, size_t);
void          vec_assoc_erase(vec_t *, size_t);

#endif
