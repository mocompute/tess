#ifndef MOS_SEXP_H
#define MOS_SEXP_H

#include "alloc.h"
#include "mos_string.h"
#include "nodiscard.h"
#include "util.h"
#include "vector.h"

#include <stdint.h>

#define SEXP_MAX_UNBOXED_INT (INT64_MAX / 2)
#define SEXP_MIN_UNBOXED_INT (INT64_MIN / 2) // negative shift is UB

// -- sexp --

#define MOS_SEXP_BOXED_TAGS(X)                                                                             \
    X(sexp_box_i64, "[signed]")                                                                            \
    X(sexp_box_u64, "[unsigned]")                                                                          \
    X(sexp_box_f64, "[double]")                                                                            \
    X(sexp_box_symbol, "[symbol]")                                                                         \
    X(sexp_box_string, "[string]")                                                                         \
    X(sexp_box_list, "[list]")

typedef enum { MOS_SEXP_BOXED_TAGS(MOS_TAG_NAME) } sexp_box_tag;

typedef struct {
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
            vector list;
        } list;
    };

    sexp_box_tag tag;
} sexp_box;

typedef struct {

    union {
        sexp_box *ptr;
        i64       integer;
    };

} sexp;

// -- allocation and deallocation --

void          sexp_init_unboxed(sexp *, i64);
nodiscard int sexp_init_boxed(allocator *, sexp *);
nodiscard int sexp_init_i64(allocator *, sexp *, i64);
nodiscard int sexp_init_u64(allocator *, sexp *, u64);
nodiscard int sexp_init_f64(allocator *, sexp *, f64);
void          sexp_deinit(allocator *, sexp *);

void          sexp_box_init_empty(sexp_box *);
void          sexp_box_init_move_string(sexp_box *, sexp_box_tag, string_t *);
void          sexp_box_init_move_list(sexp_box *, vector *);
void          sexp_box_deinit(allocator *, sexp_box *);

// -- access --

bool      sexp_is_boxed(sexp);
i64       sexp_unboxed_get(sexp);
sexp_box *sexp_box_get(sexp);

// -- utilities

char *sexp_to_string(allocator *, sexp);
int   sexp_to_string_buf(sexp const *, char *, size_t);

#endif
