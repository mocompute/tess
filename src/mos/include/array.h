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
    byte *v;
} byte_array;

typedef struct {
    array_header;
    char *v;
} char_array;

typedef struct {
    array_header;
    char **v;
} c_string_array;

// -- interface --

#define array_reserve(p, n)                                                                                \
    (p).v = array_reserve_impl((array_header_t *)&(p), (p).v, (n), sizeof(p).v[0], alignof((p).v[0]))
#define array_push(p, x)                                                                                   \
    static_assert(sizeof((x)[0]) == sizeof((p).v[0]), "size mismatch");                                    \
    (p).v = array_push_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]), (x));
#define array_push_val(p, x)                                                                               \
    do {                                                                                                   \
        const typeof((p).v[0]) tmp = (x);                                                                  \
        array_push((p), &tmp);                                                                             \
    } while (0)
#define array_free(p) array_free_impl((array_header_t *)&(p), (p).v)
#define array_copy(p, xs, n)                                                                               \
    (p).v = array_copy_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]),              \
                            (void *)(xs), (u32)(n));
#define array_move(p, xs, n)                                                                               \
    (p).v = array_move_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof((p).v[0]),              \
                            (void *)(xs), (u32)(n));

#define array_insert(p, i, xs, n)                                                                          \
    (p).v =                                                                                                \
      array_insert_impl((array_header_t *)&(p), (p).v, (i), sizeof(p).v[0], alignof((p).v[0]), (xs), (n))

#define array_shrink(p)                                                                                    \
    (p).v = array_shrink_impl((array_header_t *)&(p), (p).v, sizeof(p).v[0], alignof(p.v[0]))

// -- implementation --

nodiscard void *array_alloc_impl(array_header_t *, u32, u32, u16) mallocfun;
nodiscard void *array_reserve_impl(array_header_t *, void *, u32, u32, u16);
nodiscard void *array_push_impl(array_header_t *h, void *restrict, u32, u16, void const *restrict);
nodiscard void *array_copy_impl(array_header_t *h, void *restrict, u32, u16, void const *restrict, u32);
nodiscard void *array_move_impl(array_header_t *h, void *, u32, u16, void *, u32);
nodiscard void *array_insert_impl(array_header_t *h, void *ptr, u32 index, u32, u16, void *, u32);
nodiscard void *array_shrink_impl(array_header_t *h, void *, u32, u16);
void            array_free_impl(array_header_t *, void *);

#endif
