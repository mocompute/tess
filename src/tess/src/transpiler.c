#include "transpiler.h"
#include "alloc.h"
#include "ast.h"

#include "ast_tags.h"
#include "dbg.h"
#include "tess_type.h"
#include "vector.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct transpiler {
    allocator *alloc;
    allocator *strings;
    vector    *bytes;
    allocator *bytes_alloc;

    char     **results;
    u32        n_results;
    u32        cap_results;

    u32        next_variable;
    int        indent_level;
};

// -- embed externs --

extern char const *embed_std_c;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);
static int a_toplevel(transpiler *, ast_node const *);
static int a_eval(transpiler *, ast_node const *);
static int a_result_type_of(transpiler *, struct tess_type const *);
// static int a_expression(transpiler *, ast_node const *);
// static int a_body(transpiler *, ast_node const *);
static int a_let(transpiler *, ast_node const *);
static int a_fun_apply(transpiler *, ast_node const *);
// static int  a_std_apply(transpiler *, ast_node const *, char const *);
// static int  a_string(transpiler *, ast_node const *);

static char *next_variable(transpiler *);

static void  out_put_start(transpiler *, char const *);
static void  out_put(transpiler *, char const *);
static void  out_put_fmt(transpiler *, char const *restrict, ...) __attribute__((format(printf, 2, 3)));

transpiler  *transpiler_create(allocator *alloc, vector *bytes, allocator *bytes_alloc) {
    assert(1 == bytes->element_size);

    transpiler *self  = alloc_calloc(alloc, 1, sizeof *self);
    self->alloc       = alloc;
    self->strings     = alloc_arena_create(alloc, 1024);
    self->bytes       = bytes;
    self->bytes_alloc = bytes_alloc;

    self->cap_results = 16;
    self->results     = alloc_malloc(self->strings, self->cap_results * sizeof self->results[0]);
    self->n_results   = 0;
    return self;
}

void transpiler_destroy(transpiler **self) {
    alloc_arena_destroy((*self)->alloc, &(*self)->strings);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int transpiler_compile(transpiler *self, struct ast_node **nodes, u32 n) {
    (void)self;

    // output std header
    out_put(self, embed_std_c);
    out_put(self, "\n\n");

    for (size_t i = 0; i < n; ++i) {

        int res = 0;
        if ((res = a_toplevel(self, nodes[i]))) return res;
    }

    vec_push_back_byte(self->bytes_alloc, self->bytes, '\0');
    return 0;
}

// -- statics --

static void out_put(transpiler *self, char const *str) {
    vec_copy_back_c_string(self->bytes_alloc, self->bytes, str);
}

static void out_put_fmt(transpiler *self, char const *restrict fmt, ...) {
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(null, 0, fmt, args) + 1;
    va_end(args);
    if (len <= 0) fatal("out_put_fmt: invalid fmt string: %s", fmt);

    char buf[len];
    va_start(args, fmt);
    vsnprintf(buf, (size_t)len, fmt, args);
    va_end(args);

    vec_copy_back_c_string(self->bytes_alloc, self->bytes, buf);
}

static void out_put_start(transpiler *self, char const *str) {

    int indent = self->indent_level * 4;
    if (indent < 0) indent = 0;
    while (indent--) vec_copy_back_c_string(self->bytes_alloc, self->bytes, " ");

    return out_put(self, str);
}

static int a_result_type_of(transpiler *self, struct tess_type const *ty) {

    if (!ty) fatal("a_result_type_of: null type");

    switch (ty->tag) {
    case type_nil:    out_put(self, "void"); break;
    case type_bool:   out_put(self, "bool"); break;
    case type_int:    out_put(self, "int64_t"); break;
    case type_float:  out_put(self, "double"); break;
    case type_string: out_put(self, "char *"); break;
    case type_tuple:  out_put(self, "FIXME"); break;
    case type_any:    out_put(self, "void*"); break;
    case type_arrow:  return a_result_type_of(self, ty->right);
    case type_user:   {
        char *name = tess_type_to_string(self->strings, ty);
        out_put(self, name);
        alloc_free(self->strings, name);

    } break;

    case type_type_var:
        out_put(self, "void*"); // FIXME
        break;
    }

    return 0;
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

static int a_infix(transpiler *self, ast_node const *node) {

    struct tess_type const *type = node->type;

    // Eval left and right, resulting in two result variables on the
    // stack. Eval in reverse order so we can pop them in correct
    // order.
    if (a_eval(self, node->infix.right)) return 1;
    if (a_eval(self, node->infix.left)) return 1;
    char *left  = self->results[--self->n_results];
    char *right = self->results[--self->n_results];

    char *var   = next_variable(self);
    alloc_push_back(self->strings, &self->results, &self->n_results, &self->cap_results, &var);

    out_put_start(self, "");
    if (a_result_type_of(self, type)) return 1;
    out_put_fmt(self, " %s = %s ", var, left);

    switch (node->infix.op) {
    case ast_op_addition:           out_put(self, " + "); break;
    case ast_op_subtraction:        out_put(self, " - "); break;
    case ast_op_multiplication:     out_put(self, " * "); break;
    case ast_op_division:           out_put(self, " / "); break;
    case ast_op_less_than:          out_put(self, " < "); break;
    case ast_op_less_than_equal:    out_put(self, " <= "); break;
    case ast_op_equal:              out_put(self, " == "); break;
    case ast_op_not_equal:          out_put(self, " != "); break;
    case ast_op_greater_than_equal: out_put(self, " >= "); break;
    case ast_op_greater_than:       out_put(self, " > "); break;
    case ast_op_sentinel:           out_put(self, " FIXME "); break;
    }

    out_put_fmt(self, "%s;\n", right);

    return 0;
}

static int a_let_in(transpiler *self, ast_node const *node) {

    // let a = 1 in a + 2 end => resN = 3

    char const *name = mos_string_str(&node->let_in.name->symbol.name);

    if (a_eval(self, node->let_in.value)) return 1;
    char *value = self->results[--self->n_results];

    out_put_start(self, "");
    a_result_type_of(self, node->let_in.name->type);
    out_put_fmt(self, " %s = %s;\n", name, value);

    a_eval(self, node->let_in.body);
    char *body = self->results[--self->n_results];

    char *var  = next_variable(self);
    alloc_push_back(self->strings, &self->results, &self->n_results, &self->cap_results, &var);

    out_put_start(self, "");
    a_result_type_of(self, node->let_in.name->type);
    out_put_fmt(self, " %s = %s;\n", var, body);

    return 0;
}

static int a_nil_expression(transpiler *self, ast_node const *node) {
    // an expression of type nil: there is no need to capture its
    // result
    assert(type_nil == node->type->tag);
    return a_eval(self, node);
}

static int a_eval(transpiler *self, ast_node const *node) {

    if (node->type->tag == type_nil) {
        return a_nil_expression(self, node);
    }

    char *var = next_variable(self);
    alloc_push_back(self->strings, &self->results, &self->n_results, &self->cap_results, &var);

    out_put_start(self, "");
    a_result_type_of(self, node->type);
    out_put_fmt(self, " %s;\n", var);

    switch (node->tag) {
    case ast_eof:
    case ast_nil:
        out_put_start(self, "");
        out_put_fmt(self, "%s = NULL;\n", var);
        break;

    case ast_symbol:
        out_put_start(self, "");
        out_put_fmt(self, "%s = %s;\n", var, mos_string_str(&node->symbol.name));
        break;

    case ast_string:
        out_put_start(self, "");
        out_put_fmt(self, "%s = \"%s\";\n", var, mos_string_str(&node->symbol.name));
        break;

    case ast_i64:
        out_put_start(self, "");
        out_put_fmt(self, "%s = %" PRIi64 ";\n", var, node->i64.val);
        break;
    case ast_u64:
        out_put_start(self, "");
        out_put_fmt(self, "%s = %" PRIu64 ";\n", var, node->u64.val);
        break;
    case ast_f64:
        out_put_start(self, "");
        out_put_fmt(self, "%s = %f;\n", var, node->f64.val);
        break;
    case ast_bool:
        out_put_start(self, "");
        if (node->bool_.val) out_put_fmt(self, "%s = true;\n", var);
        else out_put_fmt(self, "%s = false;\n", var);
        break;

    case ast_infix: {
        if (a_infix(self, node)) return 1;
        char *res = self->results[--self->n_results];
        out_put_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_tuple:  break;

    case ast_let_in: {
        if (a_let_in(self, node)) return 1;
        char *res = self->results[--self->n_results];
        out_put_start(self, "");
        out_put_fmt(self, "%s = %s;\n", var, res);

    } break;

    case ast_let: {
        if (a_let(self, node)) return 1;
        char *res = self->results[--self->n_results];
        out_put_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application: break;

    case ast_named_function_application:  {
        if (a_fun_apply(self, node)) return 1;
        char *res = self->results[--self->n_results];
        out_put_start(self, "");
        out_put_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_user_defined_type: break;
    }
    return 0;
}

// static int a_eval(transpiler *self, ast_node const *node) {

//     switch (node->tag) {
//     case ast_eof:
//     case ast_nil:    out_put(self, "NULL"); break;

//     case ast_symbol: out_put_fmt(self, "%s", mos_string_str(&node->symbol.name)); break;
//     case ast_string:
//         out_put(self, "\"");
//         out_put_fmt(self, "%s", mos_string_str(&node->symbol.name));
//         out_put(self, "\"");
//         break;

//     case ast_i64: out_put_fmt(self, "%" PRIi64, node->i64.val); break;
//     case ast_u64: out_put_fmt(self, "%" PRIu64, node->u64.val); break;
//     case ast_f64: out_put_fmt(self, "%f", node->f64.val); break;
//     case ast_bool:
//         if (node->bool_.val) out_put(self, "true");
//         else out_put(self, "false");
//         break;
//     case ast_infix:                       return a_infix(self, node);
//     case ast_tuple:                       break;
//     case ast_let_in:                      return a_let_in(self, node);
//     case ast_let:                         return a_let(self, node);
//     case ast_if_then_else:
//     case ast_lambda_function:
//     case ast_function_declaration:
//     case ast_lambda_declaration:
//     case ast_lambda_function_application: break;
//     case ast_named_function_application:  return a_fun_apply(self, node);

//     case ast_user_defined_type:           break;
//     }
//     return 0;
// }

// static int a_body(transpiler *self, ast_node const *node) {

//     switch (node->tag) {
//     case ast_let:
//     case ast_named_function_application:
//     case ast_eof:
//     case ast_nil:
//     case ast_bool:
//     case ast_symbol:
//     case ast_i64:
//     case ast_u64:
//     case ast_f64:
//     case ast_string:
//     case ast_infix:
//     case ast_tuple:
//     case ast_let_in:
//     case ast_if_then_else:
//     case ast_lambda_function:
//     case ast_function_declaration:
//     case ast_lambda_declaration:
//     case ast_lambda_function_application:
//         out_put_start(self, "return ");
//         a_eval(self, node);
//         out_put(self, ";");
//         break;

//     case ast_user_defined_type:
//         // FIXME should not be in body
//         break;
//     }
//     return 0;
// }

// static int a_main_body(transpiler *self, ast_node const *node) {

//     switch (node->tag) {
//     case ast_let:
//     case ast_named_function_application:
//     case ast_eof:
//     case ast_nil:
//     case ast_bool:
//     case ast_symbol:
//     case ast_i64:
//     case ast_u64:
//     case ast_f64:
//     case ast_string:
//     case ast_infix:
//     case ast_tuple:
//     case ast_let_in:
//     case ast_if_then_else:
//     case ast_lambda_function:
//     case ast_function_declaration:
//     case ast_lambda_declaration:
//     case ast_lambda_function_application:
//         out_put_start(self, "return (int) (");
//         a_eval(self, node);
//         out_put(self, ")");
//         out_put(self, ";");
//         break;

//     case ast_user_defined_type:
//         // FIXME should not be in body
//         break;
//     }
//     return 0;
// }

static int a_fun_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);

    ast_node const *name     = node->named_application.name;
    char const     *name_str = ast_node_name_string(name);

    char           *var      = next_variable(self);
    alloc_push_back(self->strings, &self->results, &self->n_results, &self->cap_results, &var);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    i32 const n_args = node->named_application.n_arguments;
    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, node->named_application.arguments[i])) return 1;

    out_put_start(self, "");
    a_result_type_of(self, node->type);
    out_put_fmt(self, " %s;\n", var);

    out_put_start(self, "");
    out_put_fmt(self, "%s = %s(", var, name_str);

    for (i32 i = 0; i < n_args; ++i) {
        char *arg = self->results[--self->n_results];
        out_put(self, arg);
        if (i < n_args - 1) out_put(self, ", ");
    }
    out_put(self, ");\n");

    return 0;
}

// static int a_string(transpiler *self, ast_node const *node) {
//     assert(ast_string == node->tag);
//     vec_copy_back_c_string(self->bytes_alloc, self->bytes, "\"");
//     vec_copy_back_c_string(self->bytes_alloc, self->bytes, ast_node_name_string(node));
//     vec_copy_back_c_string(self->bytes_alloc, self->bytes, "\"");
//     return 0;
// }

// static int a_std_dbg(transpiler *self, ast_node const *node) {

//     // FIXME for now only one string argument is valid
//     if (1 != node->named_application.n_arguments) return 1;

//     out_put(self, "(fprintf(stderr, \"%s\", ");
//     ast_node *arg = node->named_application.arguments[0];
//     if (a_string(self, arg)) return 1;
//     out_put(self, "), 0)");
//     return 0;
// }

// static int a_std_apply(transpiler *self, ast_node const *node, char const *name) {
//     static char const *const std_names[] = {
//       "std_dbg",
//     };
//     static compile_fun_t const std_funs[] = {
//       a_std_dbg,
//     };

//     size_t i;
//     for (i = 0; i != sizeof(std_names) / sizeof(char *); ++i) {
//         if (0 == strcmp(std_names[i], name)) {
//             return std_funs[i](self, node);
//         }
//     }
//     return 1;
// }

static int a_let(transpiler *self, ast_node const *node) {

    if (0 == ast_node_name_strcmp(node->let.name, "main")) {

        out_put(self, "int main(int argc, char* argv[]) {\n    (void)argc; (void)argv;\n\n");

        self->indent_level++;

        int res = 0;
        if ((res = a_eval(self, node->let.body))) return res;

        char *var = self->results[--self->n_results];

        out_put_start(self, "");
        out_put_fmt(self, "return (int) %s;", var);

        self->indent_level--;

        out_put(self, "\n}\n");
        return 0;
    }

    // function declaration

    // return type
    out_put_start(self, "static ");
    if (a_result_type_of(self, node->let.name->type)) return 1;
    out_put(self, " ");

    // name
    out_put(self, mos_string_str(&node->let.name->symbol.name));
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < node->let.n_parameters; ++i) {
        if (a_result_type_of(self, node->let.parameters[i]->type)) return 1;
        out_put(self, " ");
        out_put(self, mos_string_str(&node->let.parameters[i]->symbol.name));
        if (i < node->let.n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;
    if (a_eval(self, node->let.body)) return 1;
    self->indent_level--;
    char *body = self->results[--self->n_results];
    out_put_start(self, "");
    out_put_fmt(self, "return %s;", body);
    out_put(self, "\n}\n\n");

    return 0;
}

static char *next_variable(transpiler *self) {
    int   len = snprintf(null, 0, "_res%u_", self->next_variable) + 1;

    char *out = alloc_malloc(self->strings, (u32)len);
    snprintf(out, (u32)len, "_res%u_", self->next_variable++);
    return out;
}
