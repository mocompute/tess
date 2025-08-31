#include "transpiler.h"
#include "alloc.h"
#include "ast.h"

#include "ast_tags.h"
#include "dbg.h"
#include "vector.h"

#include <assert.h>
#include <string.h>

struct transpiler {
    allocator *alloc;
    vector    *bytes;
    allocator *bytes_alloc;
    int        indent_level;
};

// -- embed externs --

extern char const *embed_std_c;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);
static int  a_toplevel(transpiler *, ast_node const *);
static int  a_body(transpiler *, ast_node const *);
static int  a_let(transpiler *, ast_node const *);
static int  a_fun_apply(transpiler *, ast_node const *);
static int  a_std_apply(transpiler *, ast_node const *, char const *);
static int  a_string(transpiler *, ast_node const *);

static void out_put(transpiler *, char const *);

transpiler *transpiler_create(allocator *alloc, vector *bytes, allocator *bytes_alloc) {
    assert(1 == bytes->element_size);

    transpiler *self  = alloc_calloc(alloc, 1, sizeof *self);
    self->alloc       = alloc;
    self->bytes       = bytes;
    self->bytes_alloc = bytes_alloc;
    return self;
}

void transpiler_destroy(transpiler **self) {
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int transpiler_compile(transpiler *self, struct ast_node **nodes, u32 n) {
    (void)self;

    // output std header
    out_put(self, embed_std_c);

    for (size_t i = 0; i < n; ++i) {

        int res = 0;
        if ((res = a_toplevel(self, nodes[i]))) return res;
    }

    return 0;
}

// -- statics --

void out_put(transpiler *self, char const *str) {
    vec_copy_back_c_string(self->bytes_alloc, self->bytes, str);
}

void out_put_start(transpiler *self, char const *str) {

    int indent = self->indent_level * 4;
    if (indent < 0) indent = 0;
    while (indent--) vec_copy_back_c_string(self->bytes_alloc, self->bytes, " ");

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
    case ast_user_defined_type:
        // FIXME
        break;
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
    case ast_user_defined_type:
        // FIXME should not be in body
        break;
    }
    return 0;
}

static int a_fun_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);

    static char const *const std_prefix     = "std_";
    static u8 const          std_prefix_len = strlen(std_prefix);

    ast_node const          *name           = node->named_application.name;
    char const              *name_str       = ast_node_name_string(name);
    if (0 == strncmp(name_str, std_prefix, std_prefix_len)) {
        return a_std_apply(self, node, name_str);
    }

    // FIXME only supports std_ functions for now
    return 1;
}

static int a_string(transpiler *self, ast_node const *node) {
    assert(ast_string == node->tag);
    vec_copy_back_c_string(self->bytes_alloc, self->bytes, "\"");
    vec_copy_back_c_string(self->bytes_alloc, self->bytes, ast_node_name_string(node));
    vec_copy_back_c_string(self->bytes_alloc, self->bytes, "\"");
    return 0;
}

static int a_std_dbg(transpiler *self, ast_node const *node) {

    // FIXME for now only one string argument is valid
    if (1 != node->named_application.n_arguments) return 1;

    out_put_start(self, "fprintf(stderr, \"%s\", ");
    ast_node *arg = node->named_application.arguments[0];
    if (a_string(self, arg)) return 1;
    out_put(self, ");\n");
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

    if (0 == ast_node_name_strcmp(node->let.name, "main")) {

        out_put(self, "\nint main(int argc, char* argv[]) {\n");

        self->indent_level++;
        int res = 0;
        if ((res = a_body(self, node->let.body))) return res;
        self->indent_level--;

        out_put(self, "\n    return 0;\n}");
    }

    return 0;
}
