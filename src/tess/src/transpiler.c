#include "alloc.h"
#include "ast.h"
#include "transpiler.h"

#include "dbg.h"
#include "vector.h"

#include <assert.h>
#include <string.h>

struct transpiler {
    allocator      *alloc;
    ast_pool const *pool;
    vec_t          *bytes;
    allocator      *bytes_alloc;
    int             indent_level;
};

// -- embed externs --

extern char const *embed_std;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);
static int  a_toplevel(transpiler *, ast_node const *);
static int  a_body(transpiler *, ast_node const *);
static int  a_let(transpiler *, ast_node const *);
static int  a_fun_apply(transpiler *, ast_node const *);
static int  a_std_apply(transpiler *, ast_node const *, char const *);
static int  a_string(transpiler *, ast_node const *);

static int  out_put(transpiler *, char const *);

transpiler *transpiler_create(allocator *alloc, ast_pool const *pool, vec_t *bytes,
                              allocator *bytes_alloc) {
    assert(1 == bytes->element_size);

    transpiler *self  = alloc_calloc(alloc, 1, sizeof *self);
    self->alloc       = alloc;
    self->pool        = pool;
    self->bytes       = bytes;
    self->bytes_alloc = bytes_alloc;
    return self;
}

void transpiler_destroy(transpiler **self) {
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int transpiler_compile(transpiler *self, vec_t const *nodes) {
    (void)self;
    assert(sizeof(ast_node_h) == nodes->element_size);

    // output std header
    out_put(self, embed_std);

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

// -- statics --

int out_put(transpiler *self, char const *str) {
    return vec_copy_back_c_string(self->bytes_alloc, self->bytes, str);
}

int out_put_start(transpiler *self, char const *str) {

    int indent = self->indent_level * 4;
    if (indent < 0) indent = 0;
    while (indent--)
        if (vec_copy_back_c_string(self->bytes_alloc, self->bytes, " ")) return 1;

    return out_put(self, str);
}

static int a_toplevel(transpiler *self, ast_node const *node) {

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

static int a_body(transpiler *self, ast_node const *node) {

    switch (node->tag) {
    case ast_let:                         return a_let(self, node);
    case ast_named_function_application:  return a_fun_apply(self, node);
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
    case ast_lambda_function_application: break;
    }
    return 0;
}

static int a_fun_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);

    static char const *const std_prefix     = "std_";
    static u8 const          std_prefix_len = strlen(std_prefix);

    ast_node const          *name           = ast_pool_cat(self->pool, node->named_application.name);
    char const              *name_str       = ast_node_name_string(name);
    if (0 == strncmp(name_str, std_prefix, std_prefix_len)) {
        return a_std_apply(self, node, name_str);
    }

    // FIXME only supports std_ functions for now
    return 1;
}

static int a_string(transpiler *self, ast_node const *node) {
    assert(ast_string == node->tag);
    if (vec_copy_back_c_string(self->bytes_alloc, self->bytes, "\"")) return 1;
    if (vec_copy_back_c_string(self->bytes_alloc, self->bytes, ast_node_name_string(node))) return 1;
    if (vec_copy_back_c_string(self->bytes_alloc, self->bytes, "\"")) return 1;
    return 0;
}

static int a_std_dbg(transpiler *self, ast_node const *node) {

    vec_t const *const arguments = &node->named_application.arguments;

    // FIXME for now only one string argument is valid
    if (1 != vec_size(arguments)) return 1;

    if (out_put_start(self, "fprintf(stderr, \"%s\", ")) return 1;
    ast_node_h const *arg = vec_cat(arguments, 0);
    if (a_string(self, ast_pool_cat(self->pool, *arg))) return 1;
    if (out_put(self, ");\n")) return 1;
    return 0;
}

static int a_std_apply(transpiler *self, ast_node const *node, char const *name) {
    static char const *const std_names[] = {
      "std_dbg",
    };
    static compile_fun_t const std_funs[] = {
      a_std_dbg,
    };

    size_t i;
    for (i = 0; i != sizeof(std_names) / sizeof(char *); ++i) {
        if (0 == strcmp(std_names[i], name)) {
            return std_funs[i](self, node);
        }
    }
    return 1;
}

static int a_let(transpiler *self, ast_node const *node) {

    ast_node const *name = ast_pool_cat(self->pool, node->let.name);
    assert(name);

    if (0 == ast_node_name_strcmp(name, "main")) {

        dbg("found main\n");
        if (out_put(self, "\nint main(int argc, char* argv[]) {\n")) return 1;

        self->indent_level++;
        int res = 0;
        if ((res = a_body(self, ast_pool_cat(self->pool, node->let.body)))) return res;
        self->indent_level--;

        if (out_put(self, "\n    return 0;\n}")) return 1;
    }

    return 0;
}
