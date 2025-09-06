#ifndef MOS_SEXP_H
#define MOS_SEXP_H

#include "alloc.h"
#include "array.h"
#include "mos_string.h"
#include "nodiscard.h"

#include <stdint.h>

#define SEXP_MAX_UNBOXED_INT (INT64_MAX / 2)
#define SEXP_MIN_UNBOXED_INT (INT64_MIN / 2) // negative shift is UB

// -- sexp --

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define MOS_SEXP_BOXED_TAGS(X)                                                                             \
    X(sexp_box_i64, "[signed]")                                                                            \
    X(sexp_box_u64, "[unsigned]")                                                                          \
    X(sexp_box_f64, "[double]")                                                                            \
    X(sexp_box_symbol, "[symbol]")                                                                         \
    X(sexp_box_string, "[string]")                                                                         \
    X(sexp_box_list, "[list]")

typedef enum { MOS_SEXP_BOXED_TAGS(MOS_TAG_NAME) } sexp_box_tag;

typedef struct sexp {
    union {
        struct sexp_box *ptr;
        i64              integer;
    };
} sexp;

typedef struct {
    array_header;
    struct sexp *v;
} sexp_array;

typedef struct {
    array_sized;
    struct sexp *v;
} sexp_sized;

typedef struct sexp_box {
    union {
        struct {
            i64 val;
        } i64;
        struct {
            u64 val;
        } u64;
        struct {
            f64 val;
        } f64;
        struct {
            string_t name;
        } symbol;
        struct {
            string_t name;
        } string;
        struct {
            sexp_sized list;
        } list;
    };

    sexp_box_tag tag;
} sexp_box;

// -- allocation and deallocation --

sexp sexp_init_unboxed(i64);
sexp sexp_init_boxed(allocator *);
sexp sexp_init_i64(allocator *, i64);
sexp sexp_init_u64(allocator *, u64);
sexp sexp_init_f64(allocator *, f64);
sexp sexp_init_sym(allocator *, char const *);
sexp sexp_init_list(allocator *, sexp const *, u32);
sexp sexp_init_list_single(allocator *, sexp);
sexp sexp_init_list_pair(allocator *, sexp, sexp);
sexp sexp_init_list_triple(allocator *, sexp, sexp, sexp);
sexp sexp_init_list_quad(allocator *, sexp, sexp, sexp, sexp);
sexp sexp_init_list_penta(allocator *, sexp, sexp, sexp, sexp, sexp);
void sexp_deinit(allocator *, sexp *);

void sexp_box_init_empty(sexp_box *);
void sexp_box_init_move_string(sexp_box *, sexp_box_tag, string_t *);
void sexp_box_init_move_list(sexp_box *, sexp_sized);
void sexp_box_deinit(allocator *, sexp_box *);

// -- access --

bool      sexp_is_boxed(sexp);
i64       sexp_unboxed_get(sexp);
sexp_box *sexp_box_get(sexp);

// -- utilities --

char *sexp_to_string(allocator *, sexp);
int   sexp_to_string_buf(sexp const *, char *, size_t);

#endif
