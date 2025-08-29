#include "vector.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <string.h>

//
// capacity bitfield
//
// Lowest bit indicates whether or not an additional pointer-sized
// field exists before the start of data. Valid capacity must be an
// even number. An odd number indicates the additional field exists.
// The correct offset to the flexible array member [buffer] is added
// in [vec_data]
//

struct vector_data_header {
    u32 capacity_;                 // bitfield
    alignas(void *) char buffer[]; // buffer must be max aligned
};

struct vectora_data_header {
    u32        capacity_; // bitfield
    allocator *alloc;
    alignas(void *) char buffer[];
};

static noreturn void fatal(char const *restrict fmt, ...) __attribute__((format(printf, 1, 2)));
static void          init_vector(allocator *, vector *, u32 num, u32 size);
static void          init_vectora(allocator *, vectora *, u32 num, u32 size);
static void          vec_capacity_set(vector *vec, u32 val);
static size_t        header_size(vector *);

//

vector *vec_create(allocator *alloc, u32 num, u32 size) {
    vector *self = alloc_malloc(alloc, sizeof(*self));
    init_vector(alloc, self, num, size);
    return self;
}

vectora *veca_create(allocator *alloc, u32 num, u32 size) {
    vectora *self = alloc_malloc(alloc, sizeof(*self));
    init_vectora(alloc, self, num, size);
    return self;
}

void vec_destroy(allocator *alloc, vector **vec) {
    vec_deinit(alloc, *vec);
    alloc_free(alloc, *vec);
    *vec = null;
}

void veca_destroy(vectora **vec) {
    veca_deinit(*vec);
    alloc_free((*vec)->data->alloc, *vec);
    *vec = null;
}

struct vector vec_init(u32 element_size) {
    struct vector self;
    alloc_zero(&self);
    self.element_size = element_size;
    self.size         = 0;
    return self;
}

struct vectora veca_init(allocator *alloc, u32 element_size) {
    struct vectora self;
    alloc_zero(&self);
    init_vectora(alloc, &self, 0, element_size);
    return self;
}

void init_vector(allocator *alloc, vector *vec, u32 num, u32 size) {

    alloc_zero(vec);
    vec->element_size = size;
    vec->size         = 0;

    if (num) {
        u32 capacity = num;
        if (capacity % 2 == 1) ++capacity; // initial capacity must be even

        vec->data =
          alloc_calloc(alloc, 1, sizeof(struct vector_data_header) + capacity * vec->element_size);

        vec->data->capacity_ = capacity;
    }
}

void init_vectora(allocator *alloc, vectora *vec, u32 num, u32 size) {

    alloc_zero(vec);
    vec->element_size = size;
    vec->size         = 0;

    u32 capacity      = num;
    if (capacity % 2 == 1) ++capacity; // initial capacity must be even

    vec->data = alloc_calloc(
      alloc, 1, sizeof(void *) + sizeof(struct vectora_data_header) + capacity * vec->element_size);
    vec->data->capacity_ = capacity + 1; // add 1 to mark this as extended data header
    vec->data->alloc     = alloc;
}

void vec_deinit(allocator *alloc, vector *vec) {
    alloc_free(alloc, vec->data);
    alloc_invalidate(vec);
}

void veca_deinit(vectora *vec) {
    alloc_free(vec->data->alloc, vec->data);
    alloc_invalidate(vec);
}

void vec_reserve(allocator *alloc, vector *vec, u32 count) {

    if (null == vec->data) return init_vector(alloc, vec, count, vec->element_size);

    if (vec_capacity(vec) >= count) return;

    u32 new_capacity = vec_capacity(vec) * 2;
    if (new_capacity == 0) new_capacity = 8;
    while (new_capacity < count) new_capacity *= 2;

    void *resized = alloc_realloc(alloc, vec->data, header_size(vec) + new_capacity * vec->element_size);
    assert(resized);

    vec->data = resized;
    vec_capacity_set(vec, new_capacity);
}

void veca_reserve(vectora *vec, u32 count) {
    assert(vec->data);
    return vec_reserve(vec->data->alloc, (vector *)vec, count);
}

void vec_move(vector *dst, vector *src) {
    alloc_copy(dst, src);
    alloc_invalidate(src);
}

void veca_move(vectora *dst, vectora *src) {
    return vec_move((vector *)dst, ((vector *)src));
}

bool vec_empty(vector const *vec) {
    return vec->size == 0;
}

bool veca_empty(vectora const *vec) {
    return vec->size == 0;
}

void vec_push_back(allocator *alloc, vector *vec, struct vector_iterator_base *iter_) {
    struct vector_iterator *iter = (struct vector_iterator *)iter_;
    if (vec->element_size != iter->private.element_size) {
        fatal("vec_push_back: element size mismatch: got %u, expected %u\n", iter->private.element_size,
              vec->element_size);
    }

    vec_push_back_void(alloc, vec, iter->ptr);
}

void veca_push_back(vectora *vec, struct vector_iterator_base *iter) {
    return vec_push_back(vec->data->alloc, (vector *)vec, iter);
}

void vec_push_back_void(allocator *alloc, vector *vec, void const *element) {
    u32 size = vec->size;
    vec_reserve(alloc, vec, size + 1);
    memcpy(vec_at(vec, size), element, vec->element_size);
    vec->size += 1;
}

void veca_push_back_void(vectora *vec, void const *element) {
    return vec_push_back_void(vec->data->alloc, (vector *)vec, element);
}

void vec_copy_back(allocator *alloc, vector *vec, struct vector_iterator_base const *start, u32 count) {
    struct vector_iterator *iter = (struct vector_iterator *)start;
    if (vec->element_size != iter->private.element_size) {
        fatal("vec_copy_back: element size mismatch: got %u, expected %u\n", iter->private.element_size,
              vec->element_size);
    }
    return vec_copy_back_void(alloc, vec, iter->ptr, count);
}

void veca_copy_back(vectora *vec, struct vector_iterator_base const *start, u32 count) {
    return vec_copy_back(vec->data->alloc, (vector *)vec, start, count);
}

void vec_copy_back_void(allocator *alloc, vector *vec, void const *start, u32 count) {
    vec_reserve(alloc, vec, vec->size + count);
    memcpy(vec_at(vec, vec->size), start, count * vec->element_size);
    vec->size += count;
}

void veca_copy_back_void(vectora *vec, void const *start, u32 count) {
    return vec_copy_back_void(vec->data->alloc, (vector *)vec, start, count);
}

void vec_push_back_byte(allocator *alloc, vector *vec, u8 b) {
    assert(1 == vec->element_size);
    assert(vec->data);
    vec_reserve(alloc, vec, vec->data ? vec->size + 1 : 1);
    *(byte *)(&vec_data(vec)[vec->size]) = b;
    vec->size += 1;
}

void veca_push_back_byte(vectora *vec, u8 b) {
    return vec_push_back_byte(vec->data->alloc, (vector *)vec, b);
}

void vec_copy_back_bytes(allocator *alloc, vector *vec, u8 const *start, u32 count) {
    assert(1 == vec->element_size);
    assert(vec->data);
    vec_reserve(alloc, vec, vec->data ? vec->size + count : count);

    if (!vec->data) fatal("vec_copy_back_bytes: null pointer");
    memcpy(&vec_data(vec)[vec->size], start, count);
    vec->size += count;
}

void veca_copy_back_bytes(vectora *vec, u8 const *start, u32 count) {
    return vec_copy_back_bytes(vec->data->alloc, (vector *)vec, start, count);
}

void vec_copy_back_c_string(allocator *alloc, vector *vec, char const *str) {
    size_t len = strlen(str);
    if (len > UINT32_MAX) fatal("vec_copy_back_c_string: overflow size = %zu\n", len);

    return vec_copy_back_bytes(alloc, vec, (u8 const *)str, (u32)len);
}

void veca_copy_back_c_string(vectora *vec, char const *str) {
    return vec_copy_back_c_string(vec->data->alloc, (vector *)vec, str);
}

void *vec_at(vector *vec, u32 index) {
    if (!vec->data) return null;
    return &vec_data(vec)[index * vec->element_size];
}

void *veca_at(vectora *vec, u32 index) {
    return vec_at((vector *)vec, index);
}

void const *vec_cat(vector const *vec, u32 index) {
    return vec_at((vector *)vec, index);
}

void const *veca_cat(vectora const *vec, u32 index) {
    return vec_cat((vector *)vec, index);
}

void *vec_back(vector *vec) {
    return vec_at(vec, vec->size - 1);
}

void *veca_back(vectora *vec) {
    return vec_back((vector *)vec);
}

void vec_pop_back(vector *vec) {
    vec->size -= 1;
}

void veca_pop_back(vectora *vec) {
    return vec_pop_back((vector *)vec);
}

void vec_erase(vector *vec, struct vector_iterator_base *it) {
    struct vector_iterator *iter = (struct vector_iterator *)it;
    if (vec->element_size != iter->private.element_size) {
        fatal("vec_erase: element size mismatch: got %u, expected %u\n", iter->private.element_size,
              vec->element_size);
    }

    if (iter->private.next > 0) --iter->private.next; // adjust iterator location
    return vec_erase_void(vec, iter->ptr);
}

void veca_erase(vectora *vec, struct vector_iterator_base *it) {
    return vec_erase((vector *)vec, it);
}

void vec_erase_void(vector *vec, void *it_) {
    byte *const       it  = it_;
    byte const *const end = vec_end(vec);

    assert(end > it);
    ptrdiff_t len = end - it - (ptrdiff_t)vec->element_size;

    memmove(&it[0], &it[vec->element_size], (size_t)len);
    vec->size -= 1;
}

void veca_erase_void(vectora *vec, void *it) {
    return vec_erase_void((vector *)vec, it);
}

void vec_resize(allocator *alloc, vector *vec, u32 n) {
    if (null == vec->data) return init_vector(alloc, vec, n, vec->element_size);
    if (n > vec_capacity(vec)) vec_reserve(alloc, vec, n);
    vec->size = n;
}

void veca_resize(vectora *vec, u32 n) {
    return vec_resize(vec->data->alloc, (vector *)vec, n);
}

void vec_clear(vector *vec) {
    // Note: Do not free data.
    if (!vec_empty(vec)) vec->size = 0;
}

void veca_clear(vectora *vec) {
    return vec_clear((vector *)vec);
}

void *vec_data(vector *vec) {
    if (!vec->data) return null;
    return &vec->data->buffer[(vec->data->capacity_ % 2 == 1 ? sizeof(void *) : 0)]; // expanded header
}

void *veca_data(vectora *vec) {
    return vec_data((vector *)vec);
}

void *vec_begin(vector *vec) {
    return vec_data(vec);
}

void *veca_begin(vectora *vec) {
    return vec_begin((vector *)vec);
}

void const *vec_cbegin(vector const *vec) {
    return vec_begin((vector *)vec);
}

void const *veca_cbegin(vectora const *vec) {
    return vec_begin((vector *)vec);
}

void *vec_end(vector *vec) {
    return vec_at(vec, vec->size);
}

void *veca_end(vectora *vec) {
    return vec_end((vector *)vec);
}

void const *vec_cend(vector const *vec) {
    return vec_cat(vec, vec->size);
}

void const *veca_cend(vectora const *vec) {
    return vec_cend((vector const *)vec);
}

u32 vec_size(vector const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->size;
}

u32 veca_size(vectora const *vec) {
    return vec_size((vector *)vec);
}

u32 vec_capacity(vector const *vec) {
    if (vec->data->capacity_ % 2 == 1) return vec->data->capacity_ - 1;
    return vec->data->capacity_;
}

void vec_capacity_set(vector *vec, u32 val) {
    assert(vec->data);
    assert(val % 2 == 0);
    if (vec->data->capacity_ % 2 == 1) vec->data->capacity_ = val + 1;
    else vec->data->capacity_ = val;
}

static size_t header_size(vector *vec) {
    return sizeof(struct vector_data_header) + (vec->data->capacity_ % 2 == 1 ? sizeof(void *) : 0);
}

u32 veca_capacity(vectora const *vec) {
    return vec_capacity((vector const *)vec);
}

//

void vec_iterator_init(vector const *self, struct vector_iterator_base *iter) {
    iter->next         = 0;
    iter->element_size = self->element_size;
}

void veca_iterator_init(vectora const *self, struct vector_iterator_base *iter) {
    return vec_iterator_init((vector const *)self, iter);
}

bool vec_iter(vector *self, struct vector_iterator_base *iter_) {
    struct vector_iterator *iter = (struct vector_iterator *)iter_;
    if (iter->private.next == vec_size(self)) return false;
    iter->private.element_size = self->element_size;
    iter->ptr                  = vec_at(self, iter->private.next++);
    return true;
}

bool vec_citer(vector const *self, struct vector_iterator_base *iter) {
    return vec_iter((vector *)self, iter);
}

bool veca_iter(vectora *self, struct vector_iterator_base *iter) {
    return vec_iter((vector *)self, iter);
}

bool veca_citer(vectora const *self, struct vector_iterator_base *iter) {
    return vec_iter((vector *)self, iter);
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

void veca_map(vectora const *self, vec_map_fun fun, void *ctx, void *out) {
    return vec_map((vector const *)self, fun, ctx, out);
}

void vec_map_n(vector const *self, vec_map_fun fun, void *ctx, void *out, u32 max) {
    u32 const   element_size = self->element_size;
    void const *it           = vec_cbegin(self);
    void const *end          = vec_cend(self);
    while (max-- && it != end) {
        fun(ctx, out, it);
        out += element_size;
        it += element_size;
    }
}

void veca_map_n(vectora const *self, vec_map_fun fun, void *ctx, void *out, u32 max) {
    return vec_map_n((vector const *)self, fun, ctx, out, max);
}

//

void vec_assoc_set(allocator *alloc, vector *vec, void const *pair) {
    assert(vec->element_size >= sizeof(u32));
    vec_push_back_void(alloc, vec, pair);
}

void veca_assoc_set(vectora *vec, void const *pair) {
    return vec_assoc_set(vec->data->alloc, (vector *)vec, pair);
}

void *vec_assoc_get(vector *vec, u32 key) {
    if (vec_empty(vec)) return null;

    // From the back, search for an element whose first u32 field
    // matches the search term.

    byte const *const last         = vec_data(vec);
    byte             *it           = vec_back(vec);
    u32 const         element_size = vec->element_size;

    while (1) {
        if (key == *(u32 *)it) return (&it[sizeof(u32)]); // return second element of pair

        if (it == last) break; // examined last pair
        it -= element_size;
    }
    return null;
}

void *veca_assoc_get(vectora *vec, u32 key) {
    return vec_assoc_get((vector *)vec, key);
}

void vec_assoc_erase(vector *vec, u32 key) {

    char *it = vec_assoc_get(vec, key);
    if (!it) return;

    // it points just past the key, so reverse it
    it -= sizeof(u32);

    vec_erase_void(vec, it);
}

void veca_assoc_erase(vectora *vec, u32 key) {
    return vec_assoc_erase((vector *)vec, key);
}

static noreturn void fatal(char const *restrict fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}
