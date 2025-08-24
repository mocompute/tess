#include "compiler.h"
#include "alloc.h"
#include "ast.h"

#include "dbg.h"
#include "vector.h"

#include <assert.h>
#include <string.h>

struct tess_compiler {
    allocator      *alloc;
    ast_pool const *pool;
    vec_t          *bytes;
    allocator      *bytes_alloc;
};

static int     a_toplevel(tess_compiler *, ast_node const *);
static int     a_let(tess_compiler *, ast_node const *);

tess_compiler *tess_compiler_create(allocator *alloc, ast_pool const *pool, vec_t *bytes,
                                    allocator *bytes_alloc) {
    assert(1 == bytes->element_size);

    tess_compiler *self = alloc_malloc(alloc, sizeof *self);
    self->alloc         = alloc;
    self->pool          = pool;
    self->bytes         = bytes;
    self->bytes_alloc   = bytes_alloc;
    return self;
}

void tess_compiler_destroy(tess_compiler **self) {
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int tess_compiler_compile(tess_compiler *self, vec_t const *nodes) {
    (void)self;
    assert(sizeof(ast_node_h) == nodes->element_size);

    // find top level let nodes and look for main
    ast_node_h const *it  = vec_cbegin(nodes);
    ast_node_h const *end = vec_end(nodes);
    while (it != end) {

        ast_node const *node = ast_pool_cat(self->pool, *it);
        assert(node);

        int res = 0;
        if ((res = a_toplevel(self, node))) return res;

        ++it;
    }

    return 0;
}

static int a_toplevel(tess_compiler *self, ast_node const *node) {
    (void)self;

    switch (node->tag) {
    case ast_let:                         return a_let(self, node);
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_tuple:
    case ast_let_in:
    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_named_function_application:  break;
    }
    return 0;
}

static int a_let(tess_compiler *self, ast_node const *node) {

    ast_node const *name = ast_pool_cat(self->pool, node->let.name);
    assert(name);

    if (0 == ast_node_name_cmp(name, "main")) {

        dbg("found main\n");

        if (vec_copy_back_bytes(self->bytes_alloc, self->bytes, (u8 const *)"", 0)) return 1;
    }

    return 0;
}
