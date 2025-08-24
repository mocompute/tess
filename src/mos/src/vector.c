#include "vector.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

vector *vec_alloc(allocator *alloc) {
    return alloc_malloc(alloc, sizeof(vector));
}

void vec_dealloc(allocator *alloc, vector **vec) {
    alloc_assert_invalid(*vec);
    alloc_free(alloc, *vec);
    *vec = null;
}

void vec_init_empty(vector *vec, size_t element_size) {
    alloc_zero(vec);
    vec->element_size = element_size;
}

int vec_init(allocator *alloc, vector *vec, size_t element_size, size_t initial_capacity) {
    assert(element_size <= PTRDIFF_MAX);
    alloc_zero(vec);
    vec->element_size = element_size;

    if (initial_capacity) {
        vec->data = alloc_malloc(alloc, sizeof(vector_data_header) + initial_capacity * element_size);
        if (null == vec->data) {
            dbg("vec_init: oom\n");
            return 1;
        }
        alloc_zero(vec->data);
        vec->data->capacity = initial_capacity;
    }
    return 0;
}

void vec_deinit(allocator *alloc, vector *vec) {
    alloc_free(alloc, vec->data);
    alloc_invalidate(vec);
}

int vec_reserve(allocator *alloc, vector *vec, size_t count) {

    if (null == vec->data) return vec_init(alloc, vec, vec->element_size, count);

    if (vec->data->capacity >= count) return 0;

    size_t new_capacity = vec->data->capacity * 2;
    if (new_capacity == 0) new_capacity = 8;
    while (new_capacity < count) new_capacity *= 2;

    void *resized =
      alloc_realloc(alloc, vec->data, sizeof(vector_data_header) + new_capacity * vec->element_size);
    if (!resized) {
        dbg("vec_reserve: oom\n");
        return 1;
    }

    vec->data           = resized;
    vec->data->capacity = new_capacity;
    return 0;
}

void vec_move(vector *dst, vector *src) {
    alloc_copy(dst, src);
    alloc_invalidate(src);
}

bool vec_empty(vector const *vec) {
    return vec->data == null || vec->data->size == 0;
}

int vec_push_back(allocator *alloc, vector *vec, void const *element) {

    if (vec_reserve(alloc, vec, vec->data ? vec->data->size + 1 : 1)) return 1;

    memcpy(vec->data->data + vec->data->size * vec->element_size, element, vec->element_size);
    vec->data->size += 1;
    return 0;
}

int vec_copy_back(allocator *alloc, vector *vec, void const *start, size_t count) {
    if (vec_reserve(alloc, vec, vec->data ? vec->data->size + count : count)) return 1;

    memcpy(vec->data->data + vec->data->size * vec->element_size, start, count * vec->element_size);
    vec->data->size += count;
    return 0;
}

int vec_push_back_byte(allocator *alloc, vector *vec, u8 b) {
    assert(1 == vec->element_size);
    if (vec_reserve(alloc, vec, vec->data ? vec->data->size + 1 : 1)) return 1;
    *(vec->data->data + vec->data->size) = b;
    vec->data->size += 1;
    return 0;
}

int vec_copy_back_bytes(allocator *alloc, vector *vec, u8 const *start, size_t count) {
    assert(1 == vec->element_size);
    if (vec_reserve(alloc, vec, vec->data ? vec->data->size + count : count)) return 1;

    memcpy(vec->data->data + vec->data->size, start, count);
    vec->data->size += count;
    return 0;
}

int vec_copy_back_c_string(allocator *alloc, vector *vec, char const *str) {
    return vec_copy_back_bytes(alloc, vec, (u8 const *)str, strlen(str));
}

void *vec_at(vector *vec, size_t index) {
    return vec->data->data + index * vec->element_size;
}

void const *vec_cat(vector const *vec, size_t index) {
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

nodiscard int vec_resize(allocator *alloc, vector *vec, size_t n) {
    if (null == vec->data) return vec_init(alloc, vec, vec->element_size, n);

    if (n > vec->data->capacity)
        if (vec_reserve(alloc, vec, n)) {
            dbg("vec_resize: oom\n");
            return 1;
        }

    vec->data->size = n;
    return 0;
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

void const *vec_end(vector const *vec) {
    // points 1 past the end
    if (vec_empty(vec)) return null;
    return vec->data->data + vec->data->size * vec->element_size;
}

size_t vec_size(vector const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->data->size;
}

size_t vec_capacity(vector const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->data->capacity;
}

int vec_assoc_set(allocator *alloc, vector *vec, void const *pair) {
    assert(vec->element_size >= sizeof(size_t));
    if (vec_push_back(alloc, vec, pair)) {
        dbg("vec_assoc_set: oom\n");
        return 1;
    }
    return 0;
}

void *vec_assoc_get(vector *vec, size_t key) {
    if (vec_empty(vec)) return null;

    // From the back, search for an element whose first size_t field
    // matches the search term.

    byte const *const last         = vec->data->data;
    byte             *it           = vec_back(vec);
    size_t const      element_size = vec->element_size;

    while (1) {
        if (key == *(size_t *)it) return (it + sizeof(size_t)); // return second element of pair

        if (it == last) break; // examined last pair
        it -= element_size;
    }
    return null;
}

void vec_assoc_erase(vector *vec, size_t key) {

    char *it = vec_assoc_get(vec, key);
    if (!it) return;

    // it points just past the key, so reverse it
    it -= sizeof(size_t);

    vec_erase(vec, it);
}
