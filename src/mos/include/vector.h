#ifndef MOS_VECTOR_H
#define MOS_VECTOR_H

#include "alloc.h"
#include "nodiscard.h"
#include "types.h"

#include <stdbool.h>

// -- vector struct --
//
// As an exception due to how common is the use of this module, the
// function names are shortened.

#define VEC(T)     vec_init(sizeof(T))
#define VECA(A, T) veca_init((A), sizeof(T))

typedef struct vector {
    struct vector_data_header *data;
    u32                        element_size;
    u32                        size;
} vector;

typedef struct vectora {
    struct vectora_data_header *data;
    u32                         element_size;
    u32                         size;
} vectora; // can cast to vector

struct vector_iterator {
    u32   next;
    void *ptr;
};

// -- allocation and deallocation --

nodiscard struct vector  *vec_create(allocator *, u32 num, u32 size);
nodiscard struct vectora *veca_create(allocator *, u32 num, u32 size);
void                      vec_destroy(allocator *, vector **);
void                      veca_destroy(vectora **);

struct vector             vec_init(u32 el_size);
struct vectora            veca_init(allocator *, u32 el_size);
void                      vec_deinit(allocator *, vector *);
void                      veca_deinit(vectora *);

void                      vec_reserve(allocator *, vector *, u32);
void                      veca_reserve(vectora *, u32);
void                      vec_move(vector *dst, vector *src);
void                      veca_move(vectora *dst, vectora *src);

// -- read-only access --

u32  vec_size(vector const *);
u32  veca_size(vectora const *);
u32  vec_capacity(vector const *);
u32  veca_capacity(vectora const *);
bool vec_empty(vector const *);
bool veca_empty(vectora const *);

// -- data and iterator access --

void       *vec_data(vector *);
void       *veca_data(vectora *);

void       *vec_begin(vector *);
void       *veca_begin(vectora *);
void const *vec_cbegin(vector const *);
void const *veca_cbegin(vectora const *);
void       *vec_end(vector *);
void       *veca_end(vectora *);
void const *vec_cend(vector const *);
void const *veca_cend(vectora const *);

void       *vec_at(vector *, u32);
void       *veca_at(vectora *, u32);
void const *vec_cat(vector const *, u32);
void const *veca_cat(vectora const *, u32);
void       *vec_back(vector *);
void       *veca_back(vectora *);

// pass zero-init iterator to start;
bool vec_iter(vector *, struct vector_iterator *);
bool vec_citer(vector const *, struct vector_iterator *);

// -- map --

typedef void (*vec_map_fun)(void *ctx, void *out, void const *el);

void vec_map(vector const *, vec_map_fun, void *ctx, void *out);
void veca_map(vectora const *, vec_map_fun, void *ctx, void *out);
void vec_map_n(vector const *, vec_map_fun, void *ctx, void *out, u32);
void veca_map_n(vectora const *, vec_map_fun, void *ctx, void *out, u32);

// -- insertion and removal --

void vec_push_back(allocator *, vector *, void const *);
void veca_push_back(vectora *, void const *);
void vec_copy_back(allocator *, vector *, void const *, u32);
void veca_copy_back(vectora *, void const *, u32);
void vec_pop_back(vector *);
void veca_pop_back(vectora *);
void vec_erase(vector *, void *);
void veca_erase(vectora *, void *);
void vec_resize(allocator *, vector *, u32);
void veca_resize(vectora *, u32);
void vec_clear(vector *);
void veca_clear(vectora *);

// -- association lists --
//
// element_size must be >= sizeof(u32), which is the association
// key. May contain duplicate values, but [get] and [erase] operate on
// the first one found, searching from the back.

void  vec_assoc_set(allocator *, vector *, void const *);
void  veca_assoc_set(vectora *, void const *);
void *vec_assoc_get(vector *, u32);
void *veca_assoc_get(vectora *, u32);
void  vec_assoc_erase(vector *, u32);
void  veca_assoc_erase(vectora *, u32);

// -- byte vectors --
//
// optimized for the case where element_size == 1
void vec_push_back_byte(allocator *, vector *, u8);
void veca_push_back_byte(vectora *, u8);
void vec_copy_back_bytes(allocator *, vector *, u8 const *, u32);
void veca_copy_back_bytes(vectora *, u8 const *, u32);
void vec_copy_back_c_string(allocator *alloc, vector *, char const *);
void veca_copy_back_c_string(vectora *, char const *);

#endif
