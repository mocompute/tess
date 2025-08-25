#ifndef MOS_VECTOR_H
#define MOS_VECTOR_H

#include "alloc.h"
#include "nodiscard.h"
#include "types.h"

#include <stdbool.h>

// -- vector struct --
//
// Consider the fields read-only, except for data, which clients may
// set to 0 in certain situations. In that case, client must call
// [clear] to set the size to zero. Prefer to use vec_move instead if
// possible.
//
// As an exception due to how common is the use of this module, the
// function names are shortened.

typedef struct {
#ifndef NDEBUG
    allocator *alloc;
#endif
    u32  capacity;
    u32  size;
    byte data[];
} vector_data_header;

typedef struct vector {
    u32                 element_size;
    vector_data_header *data;
} vector;

// -- allocation and deallocation --

vector *vec_alloc(allocator *);
void    vec_dealloc(allocator *, vector **);
void    vec_init_empty(vector *, u32 el_size);
void    vec_init(allocator *, vector *, u32 el_size, u32 capacity); // never fails
void    vec_deinit(allocator *, vector *);
void    vec_reserve(allocator *, vector *, u32); // never fails
void    vec_move(vector *dst, vector *src);

// -- read-only access --

u32  vec_size(vector const *);
u32  vec_capacity(vector const *);
bool vec_empty(vector const *);

// -- data and iterator access --

void       *vec_data(vector *);
void       *vec_begin(vector *);
void const *vec_cbegin(vector const *);
void       *vec_end(vector const *);
void const *vec_cend(vector const *);
void       *vec_at(vector *, u32);
void const *vec_cat(vector const *, u32);
void       *vec_back(vector *);

// -- insertion and removal --

void vec_push_back(allocator *, vector *, void const *);
void vec_copy_back(allocator *, vector *, void const *, u32);
void vec_pop_back(vector *);
void vec_erase(vector *, void *);
void vec_resize(allocator *, vector *, u32); // never fails
void vec_clear(vector *);

// -- association lists --
//
// element_size must be >= sizeof(u32), which is the association
// key. May contain duplicate values, but [get] and [erase] operate on
// the first one found, searching from the back.

void  vec_assoc_set(allocator *, vector *, void const *);
void *vec_assoc_get(vector *, u32);
void  vec_assoc_erase(vector *, u32);

// -- byte vectors --
//
// optimized for the case where element_size == 1
nodiscard int vec_push_back_byte(allocator *, vector *, u8);
nodiscard int vec_copy_back_bytes(allocator *, vector *, u8 const *, u32);
nodiscard int vec_copy_back_c_string(allocator *alloc, vector *, char const *);

#endif
