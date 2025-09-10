#include "sexp.h"
#include "alloc.h"
#include "array.h"
#include "dbg.h"
#include "string_t.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

static int is_boxed(sexp) constfun;

static int is_boxed(sexp self) {
    return (self.integer & 1) == 0;
}

sexp sexp_init_unboxed(i64 val) {
    assert(val <= SEXP_MAX_UNBOXED_INT && val >= SEXP_MIN_UNBOXED_INT);
    sexp out;
    out.integer = (i64)((u64)val << 1) | 1;
    return out;
}

i64 sexp_unboxed_get(sexp self) {
    return self.integer >> 1;
}

sexp_box *sexp_box_get(sexp self) {
    return self.ptr;
}

void sexp_box_init_empty(sexp_box *self) {
    alloc_zero(self);
    self->tag       = sexp_box_list;
    self->list.list = (sexp_sized){0};
}

void sexp_box_init_move_string(sexp_box *self, sexp_box_tag tag, string_t *src) {
    self->tag = tag;
    string_t_move(&self->symbol.name, src);
}

void sexp_box_init_move_list(sexp_box *self, sexp_sized *src) {
    self->tag = sexp_box_list;
    slice_move(self->list.list, src);
}

sexp sexp_init_boxed(allocator *alloc) {
    sexp out;
    out.ptr = alloc_malloc(alloc, sizeof(sexp_box));
    sexp_box_init_empty(out.ptr);
    return out;
}

void sexp_deinit(allocator *alloc, sexp *self) {
    if (is_boxed(*self)) {
        sexp_box_deinit(alloc, self->ptr);
        alloc_free(alloc, self->ptr);
    }
    alloc_invalidate(self);
}

sexp sexp_init_i64(allocator *alloc, i64 val) {
    sexp out;
    if (val >= SEXP_MIN_UNBOXED_INT && val <= SEXP_MAX_UNBOXED_INT) {
        out = sexp_init_unboxed(val);
    } else {
        out           = sexp_init_boxed(alloc);
        sexp_box *box = sexp_box_get(out);
        box->tag      = sexp_box_i64;
        box->i64.val  = val;
    }
    return out;
}

sexp sexp_init_u64(allocator *alloc, u64 val) {
    sexp      out = sexp_init_boxed(alloc);
    sexp_box *box = sexp_box_get(out);
    box->tag      = sexp_box_u64;
    box->u64.val  = val;
    return out;
}

sexp sexp_init_f64(allocator *alloc, f64 val) {
    sexp      out = sexp_init_boxed(alloc);
    sexp_box *box = sexp_box_get(out);
    box->tag      = sexp_box_f64;
    box->f64.val  = val;
    return out;
}

sexp sexp_init_sym(allocator *alloc, char const *str) {
    sexp      out    = sexp_init_boxed(alloc);
    sexp_box *box    = sexp_box_get(out);
    box->tag         = sexp_box_symbol;
    box->symbol.name = string_t_init(alloc, str);
    return out;
}

sexp sexp_init_list(allocator *alloc, sexp const *elements, u32 count) {
    sexp      out = sexp_init_boxed(alloc);
    sexp_box *box = sexp_box_get(out);
    box->tag      = sexp_box_list;
    if (count) {
        box->list.list.v    = alloc_malloc(alloc, count * sizeof box->list.list.v[0]);
        box->list.list.size = count;
        memcpy(box->list.list.v, elements, count * sizeof box->list.list.v[0]);
    }

    return out;
}

sexp sexp_init_list_single(allocator *alloc, sexp element) {
    return sexp_init_list(alloc, &element, 1);
}

sexp sexp_init_list_pair(allocator *alloc, sexp left, sexp right) {
    sexp pair[2] = {left, right};
    return sexp_init_list(alloc, pair, sizeof pair / sizeof *pair);
}

sexp sexp_init_list_triple(allocator *alloc, sexp first, sexp second, sexp third) {
    sexp triple[3] = {first, second, third};
    return sexp_init_list(alloc, triple, sizeof triple / sizeof *triple);
}

sexp sexp_init_list_quad(allocator *alloc, sexp first, sexp second, sexp third, sexp fourth) {
    sexp quad[4] = {first, second, third, fourth};
    return sexp_init_list(alloc, quad, sizeof quad / sizeof *quad);
}

sexp sexp_init_list_penta(allocator *alloc, sexp first, sexp second, sexp third, sexp fourth, sexp fifth) {
    sexp penta[5] = {first, second, third, fourth, fifth};
    return sexp_init_list(alloc, penta, sizeof penta / sizeof *penta);
}

void sexp_box_deinit(allocator *alloc, sexp_box *self) {
    switch (self->tag) {
    case sexp_box_i64:
    case sexp_box_u64:
    case sexp_box_f64:    break;
    case sexp_box_symbol:
    case sexp_box_string: string_t_deinit(alloc, &self->symbol.name); break;
    case sexp_box_list:   {
        if (self->list.list.size)
            for (u32 i = 0; i < self->list.list.size; ++i) sexp_deinit(alloc, &self->list.list.v[i]);

        alloc_free(alloc, self->list.list.v);

    } break;
    }

    alloc_invalidate(self);
}

int sexp_is_boxed(sexp self) {
    return (self.integer & 1) == 0;
}

static int print_node(sexp const *node, char *restrict buf, int const sz_, char const *restrict literal) {
    if (sz_ < 0) return -1;
    size_t const sz = (size_t)sz_;

    if (null != literal) {
        return snprintf(buf, sz, "%s", literal);
    }

    int offset = 0;

#define do_print_init() int res = 0;

#define do_print_node(NODE)                                                                                \
    do {                                                                                                   \
        if (buf) res = print_node(NODE, buf + offset, sz_ - offset, null);                                 \
        else res = print_node(NODE, null, 0, null);                                                        \
        if (res < 0) return res;                                                                           \
        offset += res;                                                                                     \
    } while (0)

#define do_print_literal(LITERAL)                                                                          \
    do {                                                                                                   \
        if (buf) res = print_node(null, buf + offset, sz_ - offset, LITERAL);                              \
        else res = print_node(null, null, 0, LITERAL);                                                     \
        if (res < 0) return res;                                                                           \
        offset += res;                                                                                     \
    } while (0)

    if (!sexp_is_boxed(*node)) return snprintf(buf, sz, "%" PRId64, sexp_unboxed_get(*node));
    sexp_box *box = sexp_box_get(*node);

    switch (box->tag) {
    case sexp_box_i64:    return snprintf(buf, sz, "%" PRId64, box->i64.val);
    case sexp_box_u64:    return snprintf(buf, sz, "%" PRIu64, box->u64.val);
    case sexp_box_f64:    return snprintf(buf, sz, "%f", box->f64.val);
    case sexp_box_symbol: return snprintf(buf, sz, "%s", string_t_str(&box->symbol.name));
    case sexp_box_string: return snprintf(buf, sz, "\"%s\"", string_t_str(&box->symbol.name));

    case sexp_box_list:   {
        do_print_init();
        do_print_literal("(");

        for (u32 i = 0; i < box->list.list.size; ++i) {
            do_print_node(&box->list.list.v[i]);
            if (i < box->list.list.size - 1) do_print_literal(" ");
        }

        do_print_literal(")");

    } break;
    }

    return offset;

#undef do_print_node
#undef do_print_literal
}

int sexp_to_string_buf(sexp const *node, char *buf, size_t sz_) {
    if (sz_ > INT_MAX) return 1;
    int sz  = (int)sz_;

    int res = print_node(node, buf, sz, null);

    // check error conditions from snprintf
    if (res < 0 || res > sz) return 1;
    return 0;
}

char *sexp_to_string(allocator *alloc, sexp node) {
    int sz = print_node(&node, null, 0, null);
    if (sz < 0) return null;

    char *out = alloc_malloc(alloc, (size_t)sz + 1);
    if (null == out) return out;

    print_node(&node, out, sz + 1, null);
    return out;
}
