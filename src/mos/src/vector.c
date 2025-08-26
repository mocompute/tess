#include "vector.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <string.h>

static noreturn void fatal(char const *restrict fmt, ...) __attribute__((format(printf, 1, 2)));

//

vector *vec_alloc(allocator *alloc) {
    return alloc_malloc(alloc, sizeof(vector));
}

void vec_dealloc(allocator *alloc, vector **vec) {
    alloc_assert_invalid(*vec);
    alloc_free(alloc, *vec);
    *vec = null;
}

void vec_init_empty(vector *vec, u32 element_size) {
    alloc_zero(vec);
    vec->element_size = element_size;
}

void vec_init(allocator *alloc, vector *vec, u32 element_size, u32 initial_capacity) {

    alloc_zero(vec);
    vec->element_size = element_size;

    if (initial_capacity) {
        vec->data = alloc_malloc(alloc, sizeof(vector_data_header) + initial_capacity * element_size);
        alloc_zero(vec->data);
        vec->data->capacity = initial_capacity;

#ifndef NDEBUG
        vec->data->alloc = alloc;
#endif
    }
}

void vec_deinit(allocator *alloc, vector *vec) {
    alloc_free(alloc, vec->data);
    alloc_invalidate(vec);
}

void vec_reserve(allocator *alloc, vector *vec, u32 count) {

    if (null == vec->data) return vec_init(alloc, vec, vec->element_size, count);

    if (vec->data->capacity >= count) return;

    u32 new_capacity = vec->data->capacity * 2;
    if (new_capacity == 0) new_capacity = 8;
    while (new_capacity < count) new_capacity *= 2;

    void *resized =
      alloc_realloc(alloc, vec->data, sizeof(vector_data_header) + new_capacity * vec->element_size);

    vec->data           = resized;
    vec->data->capacity = new_capacity;
}

void vec_move(vector *dst, vector *src) {
    alloc_copy(dst, src);
    alloc_invalidate(src);
}

bool vec_empty(vector const *vec) {
    return vec->data == null || vec->data->size == 0;
}

void vec_push_back(allocator *alloc, vector *vec, void const *element) {
    vec_reserve(alloc, vec, vec->data ? vec->data->size + 1 : 1);
    memcpy(vec->data->data + vec->data->size * vec->element_size, element, vec->element_size);
    vec->data->size += 1;
    assert(alloc == vec->data->alloc);
}

void vec_copy_back(allocator *alloc, vector *vec, void const *start, u32 count) {
    vec_reserve(alloc, vec, vec->data ? vec->data->size + count : count);
    memcpy(vec->data->data + vec->data->size * vec->element_size, start, count * vec->element_size);
    vec->data->size += count;
    assert(alloc == vec->data->alloc);
}

void vec_push_back_byte(allocator *alloc, vector *vec, u8 b) {
    assert(1 == vec->element_size);
    vec_reserve(alloc, vec, vec->data ? vec->data->size + 1 : 1);
    *(vec->data->data + vec->data->size) = b;
    vec->data->size += 1;
    assert(alloc == vec->data->alloc);
}

void vec_copy_back_bytes(allocator *alloc, vector *vec, u8 const *start, u32 count) {
    assert(1 == vec->element_size);
    vec_reserve(alloc, vec, vec->data ? vec->data->size + count : count);

    memcpy(vec->data->data + vec->data->size, start, count);
    vec->data->size += count;
    assert(alloc == vec->data->alloc);
}

void vec_copy_back_c_string(allocator *alloc, vector *vec, char const *str) {
    size_t len = strlen(str);
    if (len > UINT32_MAX) fatal("vec_copy_back_c_string: overflow size = %zu\n", len);

    assert(alloc == vec->data->alloc);
    return vec_copy_back_bytes(alloc, vec, (u8 const *)str, (u32)len);
}

void *vec_at(vector *vec, u32 index) {
    return vec->data->data + index * vec->element_size;
}

void const *vec_cat(vector const *vec, u32 index) {
    return vec->data->data + index * vec->element_size;
}

void *vec_back(vector *vec) {
    return vec->data->data + (vec->data->size - 1) * vec->element_size;
}

void vec_pop_back(vector *vec) {
    vec->data->size -= 1;
}

void vec_erase(vector *vec, void *it_) {
    byte *const       it  = it_;
    byte const *const end = vec->data->data + vec->data->size * vec->element_size;
    ptrdiff_t         len = end - it - (ptrdiff_t)vec->element_size;

    memmove(it, it + vec->element_size, (size_t)len);
    vec->data->size -= 1;
}

void vec_resize(allocator *alloc, vector *vec, u32 n) {
    if (null == vec->data) return vec_init(alloc, vec, vec->element_size, n);
    if (n > vec->data->capacity) vec_reserve(alloc, vec, n);
    vec->data->size = n;
    assert(alloc == vec->data->alloc);
}

void vec_clear(vector *vec) {
    // Note: Do not free data.
    if (!vec_empty(vec)) vec->data->size = 0;
}

void *vec_data(vector *vec) {
    if (vec_empty(vec)) return null;
    return vec->data->data;
}

void *vec_begin(vector *vec) {
    if (vec_empty(vec)) return null;
    return vec->data->data;
}

void const *vec_cbegin(vector const *vec) {
    if (vec_empty(vec)) return null;
    return vec->data->data;
}

void *vec_end(vector const *vec) {
    // points 1 past the end
    if (vec_empty(vec)) return null;
    return vec->data->data + vec->data->size * vec->element_size;
}

void const *vec_cend(vector const *vec) {
    // points 1 past the end
    if (vec_empty(vec)) return null;
    return vec->data->data + vec->data->size * vec->element_size;
}

u32 vec_size(vector const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->data->size;
}

u32 vec_capacity(vector const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->data->capacity;
}

//
void vec_map(vector const *self, vec_map_fun fun, void *ctx, void *out) {
    u32 const   element_size = self->element_size;
    void const *it           = vec_cbegin(self);
    void const *end          = vec_cend(self);
    while (it != end) {
        fun(ctx, out, it);
        out += element_size;
        it += element_size;
    }
}

//

void vec_assoc_set(allocator *alloc, vector *vec, void const *pair) {
    assert(vec->element_size >= sizeof(u32));
    vec_push_back(alloc, vec, pair);
}

void *vec_assoc_get(vector *vec, u32 key) {
    if (vec_empty(vec)) return null;

    // From the back, search for an element whose first u32 field
    // matches the search term.

    byte const *const last         = vec->data->data;
    byte             *it           = vec_back(vec);
    u32 const         element_size = vec->element_size;

    while (1) {
        if (key == *(u32 *)it) return (it + sizeof(u32)); // return second element of pair

        if (it == last) break; // examined last pair
        it -= element_size;
    }
    return null;
}

void vec_assoc_erase(vector *vec, u32 key) {

    char *it = vec_assoc_get(vec, key);
    if (!it) return;

    // it points just past the key, so reverse it
    it -= sizeof(u32);

    vec_erase(vec, it);
}

static noreturn void fatal(char const *restrict fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}
