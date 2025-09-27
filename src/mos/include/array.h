#ifndef MOS_ARRAY_H
#define MOS_ARRAY_H

#include "alloc.h"
#include "types.h"

#include <assert.h>
#include <stdalign.h>

typedef struct {
    allocator *alloc;
    u32        size;
    u32        capacity;
} array_header_t;

#define array_header                                                                                       \
    allocator *alloc;                                                                                      \
    u32        size;                                                                                       \
    u32        capacity

typedef struct {
    array_header;
    void *v;
} array_tmpl;

typedef struct {
    u32 begin;
    u32 end;
} array_slice_t;

#define array_slice                                                                                        \
    u32 begin;                                                                                             \
    u32 end

typedef struct {
    u32 size;
} array_sized_t;

#define array_sized u32 size

typedef struct {
    array_header;
    byte *v;
} byte_array;

typedef struct {
    array_header;
    char *v;
} char_array;

typedef struct {
    array_header;
    char const *v;
} char_carray;

typedef struct {
    array_sized;
    char *v;
} char_sized;

typedef struct {
    array_sized;
    char const *v;
} char_csized;

typedef struct {
    array_slice;
    char *v;
} char_slice;

typedef struct {
    array_slice;
    char const *v;
} char_cslice;

typedef struct {
    array_header;
    char **v;
} c_string_array;

typedef struct {
    array_header;
    char const **v;
} c_string_carray;

typedef struct {
    array_sized;
    char **v;
} c_string_sized;

typedef struct {
    array_sized;
    char const **v;
} c_string_csized;

typedef struct {
    array_slice;
    char **v;
} c_string_slice;

typedef struct {
    array_slice;
    char const **v;
} c_string_cslice;

// -- interface --

#define array_reserve(p, n)                                                                                \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_reserve_impl((array_header_t *)&(p), (p).v, (n), sizeof(p).v[0], alignof((p).v[0])); \
    } while (0)

#define array_push(p, x)                                                                                   \
    do {                                                                                                   \
        static_assert(sizeof((&x)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_push_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (&x));   \
    } while (0)

#define array_contains(p, x)                                                                               \
    array_contains_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (&x))

#define array_push_val(p, x)                                                                               \
    do {                                                                                                   \
        const typeof((p).v[0]) tmp = (x);                                                                  \
        array_push((p), tmp);                                                                              \
    } while (0)

#define array_free(p)                                                                                      \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        array_free_impl((array_header_t *)&(p), (p).v);                                                    \
    } while (0)

#define array_copy(p, xs, n)                                                                               \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_copy_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]),          \
                                (void *)(xs), (u32)(n));                                                   \
    } while (0)

#define array_copy_(p, ty, xs, n)                                                                          \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof(ty), "size mismatch");                                     \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v =                                                                                            \
          array_copy_impl((array_header_t *)&(p), (p).v, sizeof(ty), alignof(ty), (void *)(xs), (u32)(n)); \
    } while (0)

// TODO: this doesn't do what you think it does
#define array_move(p, xs, n)                                                                               \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_move_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]),          \
                                (void *)(xs), (u32)(n));                                                   \
    } while (0)

#define array_insert(p, i, xs, n)                                                                          \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_insert_impl((array_header_t *)&(p), (p).v, (i), sizeof(p).v[0], alignof((p).v[0]),   \
                                  (xs), (n));                                                              \
    } while (0)

#define array_insert_sorted(p, x, cmp)                                                                     \
    do {                                                                                                   \
        static_assert(sizeof((x)[0]) == sizeof((p).v[0]), "size mismatch");                                \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_insert_sorted_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), \
                                         (x), (cmp));                                                      \
    } while (0)

#define array_erase(p, i)                                                                                  \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        array_erase_impl((array_header_t *)&(p), (p).v, (i), sizeof(p).v[0], alignof((p).v[0]));           \
    } while (0)

#define array_shrink(p)                                                                                    \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_tmpl), "not an array");                                    \
        (p).v = array_shrink_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]));       \
    } while (0)

#define slice_all(x)  {.v = (x).v, .end = (x).size}
#define slice_size(x) ((x).end - (x).begin)
#define sized_all(x)  {.v = (x).v, .size = (x).size}
#define slice_move(dst, src)                                                                               \
    do {                                                                                                   \
        (dst)       = *(src);                                                                              \
        (src)->size = 0;                                                                                   \
        (src)->v    = 0;                                                                                   \
    } while (0)

#define array_init_from_slice(dst, src)                                                                    \
    do {                                                                                                   \
        assert((dst)->alloc);                                                                              \
        (dst)->size     = (src)->size;                                                                     \
        (dst)->capacity = (dst)->size;                                                                     \
        (dst)->v        = (src)->v;                                                                        \
    } while (0)

#define forall(idx, arr) for (u32 idx = 0; idx < (arr).size; ++idx)

char_cslice char_cslice_from(char const *, u32);

// -- implementation --

nodiscard void *array_alloc_impl(array_header_t *, u32, u32, u16) mallocfun;
nodiscard void *array_reserve_impl(array_header_t *, void *, u32, u32, u16);
nodiscard void *array_push_impl(array_header_t *h, void *restrict, u32, u16, void const *restrict);
nodiscard void *array_copy_impl(array_header_t *h, void *restrict, u32, u16, void const *restrict, u32);
nodiscard void *array_move_impl(array_header_t *h, void *, u32, u16, void *, u32);
nodiscard void *array_insert_impl(array_header_t *h, void *restrict ptr, u32 index, u32, u16,
                                  void const *restrict, u32);

typedef int (*array_cmp_fun)(void const *lhs, void const *rhs);
nodiscard void *array_insert_sorted_impl(array_header_t *h, void *restrict ptr, u32, u16,
                                         void const *restrict, array_cmp_fun);

int             array_contains_impl(array_header_t *, void *restrict, u32, u16, void const *restrict);
nodiscard void *array_shrink_impl(array_header_t *h, void *, u32, u16);

void            array_erase_impl(array_header_t *h, void *ptr, u32 index, u32, u16);
void            array_free_impl(array_header_t *, void *);

// -- utilities --

#endif
