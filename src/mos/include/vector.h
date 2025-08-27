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

#define VEC(T) vec_init(sizeof(T))

typedef struct vector {
    u32                        element_size;
    struct vector_data_header *data;
} vector;

// -- allocation and deallocation --

nodiscard struct vector *vec_create(allocator *, u32 num, u32 size);
void                     vec_destroy(allocator *, vector **);

struct vector            vec_init(u32 el_size);
void                     vec_deinit(allocator *, vector *);
void                     vec_reserve(allocator *, vector *, u32);
void                     vec_move(vector *dst, vector *src);

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

// -- map --

typedef void (*vec_map_fun)(void *ctx, void *out, void const *el);

void vec_map(vector const *, vec_map_fun, void *ctx, void *out);
void vec_map_n(vector const *, vec_map_fun, void *ctx, void *out, u32);

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
void vec_push_back_byte(allocator *, vector *, u8);
void vec_copy_back_bytes(allocator *, vector *, u8 const *, u32);
void vec_copy_back_c_string(allocator *alloc, vector *, char const *);

#endif
