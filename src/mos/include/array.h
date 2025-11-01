#ifndef MOS_ARRAY_H
#define MOS_ARRAY_H

#include "alloc.h"
#include "types.h"

#include <assert.h>
#include <stdalign.h>

#define defarray(NAME, T)                                                                                  \
    typedef struct {                                                                                       \
        T         *v;                                                                                      \
        allocator *alloc;                                                                                  \
        u32        size;                                                                                   \
        u32        capacity;                                                                               \
    } NAME;

#define defsized(NAME, T)                                                                                  \
    typedef struct {                                                                                       \
        T  *v;                                                                                             \
        u32 size;                                                                                          \
    } NAME;

#define defslice(NAME, T)                                                                                  \
    typedef struct {                                                                                       \
        T  *v;                                                                                             \
        u32 begin;                                                                                         \
        u32 end;                                                                                           \
    } NAME;

defarray(array_t, void);

defarray(byte_array, byte);
defarray(char_array, char);
defarray(char_carray, char const);
defsized(char_sized, char);
defsized(char_csized, char const);
defslice(char_slice, char);
defslice(char_cslice, char const);
defarray(c_string_array, char *);
defarray(c_string_carray, char const *);
defsized(c_string_sized, char *);
defsized(c_string_csized, char const *);
defslice(c_string_slice, char *);
defslice(c_string_cslice, char const *);

// -- interface --

#define array_reserve(p, n)                                                                                \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_reserve_impl((array_t *)&(p), (p).v, (n), sizeof(p).v[0], alignof((p).v[0]));        \
    } while (0)

#define array_push(p, x)                                                                                   \
    do {                                                                                                   \
        static_assert(sizeof((&x)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_push_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (&(x)));        \
    } while (0)

#define array_contains(p, x)                                                                               \
    array_contains_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (&(x)))

#define array_push_val(p, x)                                                                               \
    do {                                                                                                   \
        const typeof((p).v[0]) tmp = (x);                                                                  \
        array_push((p), tmp);                                                                              \
    } while (0)

#define array_free(p)                                                                                      \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        array_free_impl((array_t *)&(p), (p).v);                                                           \
    } while (0)

#define array_copy(p, s)                                                                                   \
    do {                                                                                                   \
        static_assert(sizeof((s).v[0]) == sizeof((p).v[0]), "size mismatch");                              \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        static_assert(sizeof(s) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_push_many_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]),            \
                                     (void *)(s).v, (s).size);                                             \
    } while (0)

#define array_push_many(p, xs, n)                                                                          \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_push_many_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]),            \
                                     (void *)(xs), (u32)(n));                                              \
    } while (0)

#define array_push_many_(p, ty, xs, n)                                                                     \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof(ty), "size mismatch");                                     \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v =                                                                                            \
          array_push_many_impl((array_t *)&(p), (p).v, sizeof(ty), alignof(ty), (void *)(xs), (u32)(n));   \
    } while (0)

// TODO: this doesn't do what you think it does
#define array_move(p, xs, n)                                                                               \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_move_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (void *)(xs),   \
                                (u32)(n));                                                                 \
    } while (0)

#define array_insert(p, i, xs, n)                                                                          \
    do {                                                                                                   \
        static_assert(sizeof((xs)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v =                                                                                            \
          array_insert_impl((array_t *)&(p), (p).v, (i), sizeof(p).v[0], alignof((p).v[0]), (xs), (n));    \
    } while (0)

#define array_insert_sorted(p, x, cmp)                                                                     \
    do {                                                                                                   \
        static_assert(sizeof((x)[0]) == sizeof((p).v[0]), "size mismatch");                                \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v =                                                                                            \
          array_insert_sorted_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (x), (cmp)); \
    } while (0)

#define array_erase(p, i)                                                                                  \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        array_erase_impl((array_t *)&(p), (p).v, (i), sizeof(p).v[0], alignof((p).v[0]));                  \
    } while (0)

#define array_shrink(p)                                                                                    \
    do {                                                                                                   \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_shrink_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]));              \
    } while (0)

#define array_sized(p)                                                                                     \
    {.size = (p).size, .v = array_shrink_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]))}

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

// -- set operations --
//
// insert x if x is not in p
#define array_set_insert(p, x)                                                                             \
    do {                                                                                                   \
        static_assert(sizeof((&x)[0]) == sizeof((p).v[0]), "size mismatch");                               \
        static_assert(sizeof(p) >= sizeof(array_t), "not an array");                                       \
        (p).v = array_set_insert_impl((array_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (&x));    \
    } while (0)

// each array is restrict - must not alias
#define array_set_difference(res, lhs, rhs)                                                                \
    do {                                                                                                   \
        static_assert(sizeof(res) >= sizeof(array_t), "not an array");                                     \
        static_assert(sizeof(lhs) >= sizeof(array_t), "not an array");                                     \
        static_assert(sizeof(rhs) >= sizeof(array_t), "not an array");                                     \
        (res).v =                                                                                          \
          array_set_difference_impl((array_t *)&(res), (res).v, (array_t *)&(lhs), (lhs).v,                \
                                    (array_t *)&(rhs), (rhs).v, sizeof(lhs).v[0], alignof((lhs).v[0]));    \
    } while (0)

// each array is restrict - must not alias
#define array_set_union(res, lhs, rhs)                                                                     \
    do {                                                                                                   \
        static_assert(sizeof(res) >= sizeof(array_t), "not an array");                                     \
        static_assert(sizeof(lhs) >= sizeof(array_t), "not an array");                                     \
        static_assert(sizeof(rhs) >= sizeof(array_t), "not an array");                                     \
        (res).v = array_set_union_impl((array_t *)&(res), (res).v, (array_t *)&(lhs), (lhs).v,             \
                                       (array_t *)&(rhs), (rhs).v, sizeof(lhs).v[0], alignof((lhs).v[0])); \
    } while (0)

char_cslice char_cslice_from(char const *, u32);

// -- implementation --

nodiscard void *array_alloc_impl(array_t *, u32, u32, u16) mallocfun;
nodiscard void *array_reserve_impl(array_t *, void *, u32, u32, u16);
nodiscard void *array_push_impl(array_t *h, void *restrict, u32, u16, void const *restrict);
nodiscard void *array_push_many_impl(array_t *h, void *restrict, u32, u16, void const *restrict, u32);
nodiscard void *array_move_impl(array_t *h, void *, u32, u16, void *, u32);
nodiscard void *array_insert_impl(array_t *h, void *restrict ptr, u32 index, u32, u16, void const *restrict,
                                  u32);

typedef int (*array_cmp_fun)(void const *lhs, void const *rhs);
nodiscard void *array_insert_sorted_impl(array_t *h, void *restrict ptr, u32, u16, void const *restrict,
                                         array_cmp_fun);

int             array_contains_impl(array_t *, void *restrict, u32, u16, void const *restrict);
nodiscard void *array_shrink_impl(array_t *h, void *, u32, u16);

void            array_erase_impl(array_t *h, void *ptr, u32 index, u32, u16);
void            array_free_impl(array_t *, void *);

// -- array set operations --

nodiscard void *array_set_insert_impl(array_t *h, void *restrict, u32, u16, void const *restrict);

nodiscard void *array_set_difference_impl(array_t *res, void *restrict, array_t *lhs, void *restrict,
                                          array_t *rhs, void *restrict, u32, u16);

nodiscard void *array_set_union_impl(array_t *res, void *restrict, array_t *lhs, void *restrict,
                                     array_t *rhs, void *restrict, u32, u16);

// -- utilities --

#endif
