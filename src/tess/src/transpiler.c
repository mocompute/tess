#include "transpiler.h"
#include "alloc.h"
#include "array.h"
#include "ast.h"

#include "ast_tags.h"
#include "dbg.h"
#include "mos_string.h"
#include "type.h"
#include "type_registry.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"

struct transpiler {
    allocator     *alloc;
    allocator     *strings;
    char_array    *bytes;

    type_registry *type_registry;

    c_string_array results;
    // a stack of result variable names, see also next_variable

    size_t next_variable;
    int    indent_level;

    bool   verbose;
};

// -- embed externs --

extern char const *embed_std_c;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);

static int   a_eval(transpiler *, ast_node const *);
static int   a_field_access(transpiler *, ast_node const *);
static int   a_fun_apply(transpiler *, ast_node const *);
static int   a_infix(transpiler *, ast_node const *);
static int   a_let(transpiler *, ast_node const *);
static int   a_let_in(transpiler *, ast_node const *);
static int   a_main(transpiler *, ast_node const *);
static int   a_nil_expression(transpiler *, ast_node const *);
static int   a_result_type_of(transpiler *, tl_type const *);
static int   a_toplevel(transpiler *, ast_node const *);
static int   a_user_type_definition(transpiler *, ast_node const *);

static bool  is_generic_function(ast_node const *node);
static char *next_variable(transpiler *);

static void  out_put_start(transpiler *, char const *);
static void  out_put(transpiler *, char const *);
static void  out_put_start_fmt(transpiler *, char const *restrict, ...)
  __attribute__((format(printf, 2, 3)));
static void out_put_fmt(transpiler *, char const *restrict, ...) __attribute__((format(printf, 2, 3)));
static void vout_put_fmt(transpiler *, char const *restrict, va_list);

static bool is_generic_function(ast_node const *node);
static void log(transpiler *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

transpiler *transpiler_create(allocator *alloc, char_array *bytes, type_registry *tr) {

    transpiler *self    = alloc_calloc(alloc, 1, sizeof *self);
    self->alloc         = alloc;
    self->strings       = arena_create(alloc, 1024);
    self->bytes         = bytes;
    self->type_registry = tr;

    self->results       = (c_string_array){.alloc = self->strings};

    self->verbose       = false;

    return self;
}

void transpiler_destroy(transpiler **self) {
    arena_destroy((*self)->alloc, &(*self)->strings);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

void transpiler_set_verbose(transpiler *self, bool verbose) {
    self->verbose = verbose;
}

int transpiler_compile(transpiler *self, struct ast_node **nodes, u32 n) {
    (void)self;

    // output std header
    out_put(self, embed_std_c);
    out_put(self, "\n\n");

    // output toplevel forms
    for (size_t i = 0; i < n; ++i) {
        int res = 0;

        log(self, "compile: %s", ast_node_to_string(self->strings, nodes[i]));

        if ((res = a_toplevel(self, nodes[i]))) return res;
    }

    // output main function
    for (size_t i = 0; i < n; ++i) {
        int res = 0;
        if ((res = a_main(self, nodes[i]))) return res;
    }

    array_push_val(*self->bytes, '\0');

    return 0;
}

// -- statics --

static void out_put(transpiler *self, char const *str) {
    array_copy(*self->bytes, str, strlen(str));
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

    array_copy(*self->bytes, buf, strlen(buf));
}

static void vout_put_fmt(transpiler *self, char const *restrict fmt, va_list args) {

    va_list args2;
    va_copy(args2, args);

    int len = vsnprintf(null, 0, fmt, args) + 1;
    if (len <= 0) fatal("out_put_fmt: invalid fmt string: %s", fmt);

    char buf[len];
    vsnprintf(buf, (size_t)len, fmt, args2);

    array_copy(*self->bytes, buf, strlen(buf));

    va_end(args2);
}

static void out_put_start(transpiler *self, char const *str) {

    int indent = self->indent_level * 4;
    if (indent < 0) indent = 0;
    while (indent--) array_push_val(*self->bytes, ' ');

    return out_put(self, str);
}

static void out_put_start_fmt(transpiler *self, char const *restrict fmt, ...) {
    out_put_start(self, "");
    va_list args;

    va_start(args, fmt);
    vout_put_fmt(self, fmt, args);
    va_end(args);
}

static int a_result_type_of(transpiler *self, tl_type const *ty) {

    if (!ty) fatal("a_result_type_of: null type");

    switch (ty->tag) {
    case type_nil:            out_put(self, "void"); break;
    case type_bool:           out_put(self, "bool"); break;
    case type_int:            out_put(self, "int64_t"); break;
    case type_float:          out_put(self, "double"); break;
    case type_string:         out_put(self, "char *"); break;
    case type_tuple:          out_put(self, "FIXME"); break;
    case type_labelled_tuple: out_put(self, "FIXME"); break;
    case type_any:            out_put(self, "int"); break;
    case type_arrow:          return a_result_type_of(self, ty->arrow.right);
    case type_user:           {
        char *type_s = tl_type_to_string(self->strings, ty);
        out_put_fmt(self, "/* %s */ struct %s", type_s, ty->user.name);
        alloc_free(self->strings, type_s);

    } break;

    case type_type_var: out_put_fmt(self, "/* tv%u */ int", ty->type_var.val); break;
    }

    return 0;
}

static int a_user_type_definition(transpiler *self, ast_node const *node) {
    char const *name     = ast_node_name_string(node->user_type_def.name);

    u32 const   n_fields = node->user_type_def.n_fields;

    out_put_start_fmt(self, "struct %s {\n", name);

    self->indent_level++;
    for (u32 i = 0; i < n_fields; ++i) {
        tl_type    *ty         = node->user_type_def.field_types[i];
        char const *field_name = ast_node_name_string(node->user_type_def.field_names[i]);

        out_put_start(self, "");
        a_result_type_of(self, ty);
        out_put_fmt(self, " %s;\n", field_name);
    }
    self->indent_level--;

    out_put_start(self, "};\n");

    return 0;
}

static int a_toplevel(transpiler *self, ast_node const *node) {

    switch (node->tag) {
    case ast_let:                         return a_let(self, node);
    case ast_assignment:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_labelled_tuple:
    case ast_tuple:
    case ast_let_in:
    case ast_let_match_in:
    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_begin_end:
    case ast_user_type:
    case ast_user_type_get:
    case ast_user_type_set:               break;
    case ast_user_type_definition:        return a_user_type_definition(self, node); break;
    }
    return 0;
}

static int a_infix(transpiler *self, ast_node const *node) {

    tl_type const *type = node->type;

    // Eval left and right, resulting in two result variables on the
    // stack. Eval in reverse order so we can pop them in correct
    // order.
    if (a_eval(self, node->infix.right)) return 1;
    if (a_eval(self, node->infix.left)) return 1;
    char *left  = self->results.v[--self->results.size];
    char *right = self->results.v[--self->results.size];

    char *var   = next_variable(self);
    array_push(self->results, &var);

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

    char const *name = ast_node_name_string(node->let_in.name);
    log(self, "let_in: name = '%s'", name);

    if (a_eval(self, node->let_in.value)) return 1;
    char *value = self->results.v[--self->results.size];

    out_put_start(self, "");
    a_result_type_of(self, node->let_in.name->type);
    out_put_fmt(self, " %s = %s;\n", name, value);

    a_eval(self, node->let_in.body);
    char *body = self->results.v[--self->results.size];

    char *var  = next_variable(self);
    array_push(self->results, &var);

    out_put(self, "\n");
    out_put_start(self, "");
    a_result_type_of(self, node->let_in.body->type);
    out_put_fmt(self, " %s = %s;\n", var, body);

    return 0;
}

static int a_field_access(transpiler *self, ast_node const *node) {

    struct ast_user_type_get const *v = ast_node_utg((ast_node *)node);
    log(self, "field_access: '%s' . '%s'", ast_node_name_string(v->struct_name),
        ast_node_name_string(v->field_name));

    char *var = next_variable(self);
    array_push(self->results, &var);

    out_put_start(self, "");
    a_result_type_of(self, node->type);
    out_put_start_fmt(self, "%s = %s.%s;\n", var, ast_node_name_string(v->struct_name),
                      ast_node_name_string(v->field_name));

    return 0;
}

static int a_field_setter(transpiler *self, ast_node const *node) {

    struct ast_user_type_set const *v = ast_node_uts((ast_node *)node);
    log(self, "field_setter: '%s' . '%s'", ast_node_name_string(v->struct_name),
        ast_node_name_string(v->field_name));

    char const *struct_name = ast_node_name_string(v->struct_name);
    char const *field_name  = ast_node_name_string(v->field_name);
    char       *var         = next_variable(self);
    array_push(self->results, &var);

    // eval the value
    if (a_eval(self, v->value)) return 1;
    char *value = self->results.v[--self->results.size];

    // output value is field set value
    out_put_start(self, "");
    a_result_type_of(self, node->type);
    out_put_start_fmt(self, "%s = %s;\n", var, value);

    // assign to struct field
    out_put_start_fmt(self, "%s.%s = %s;\n", struct_name, field_name, var);

    return 0;
}

static int a_nil_expression(transpiler *self, ast_node const *node) {
    // an expression of type nil: there is no need to capture its
    // result
    assert(type_nil == node->type->tag);
    return a_eval(self, node);
}

static int a_eval(transpiler *self, ast_node const *node) {

    if (!node || !node->type) fatal("a_eval: node or type is null");

    if (node->type->tag == type_nil) {
        return a_nil_expression(self, node);
    }

    char *var = next_variable(self);
    array_push(self->results, &var);

    out_put(self, "\n");

    out_put_start_fmt(self, "/* %s */\n", ast_tag_to_string(node->tag));

    out_put_start(self, "");
    a_result_type_of(self, node->type);
    out_put_fmt(self, " %s;\n", var);

    switch (node->tag) {
    case ast_assignment:
    case ast_eof:
    case ast_nil:        out_put_start_fmt(self, "%s = NULL;\n", var); break;
    case ast_symbol:     out_put_start_fmt(self, "%s = %s;\n", var, ast_node_name_string(node)); break;
    case ast_string:     out_put_start_fmt(self, "%s = \"%s\";\n", var, ast_node_name_string(node)); break;
    case ast_i64:        out_put_start_fmt(self, "%s = %" PRIi64 ";\n", var, node->i64.val); break;
    case ast_u64:        out_put_start_fmt(self, "%s = %" PRIu64 ";\n", var, node->u64.val); break;
    case ast_f64:        out_put_start_fmt(self, "%s = %f;\n", var, node->f64.val); break;
    case ast_bool:
        if (node->bool_.val) out_put_start_fmt(self, "%s = true;\n", var);
        else out_put_start_fmt(self, "%s = false;\n", var);
        break;

    case ast_begin_end: {
        struct ast_begin_end const *v = ast_node_begin_end((ast_node *)node);
        if (v->n_expressions == 0) break;
        for (u32 i = 0; i < v->n_expressions - 1; ++i) {
            out_put_start(self, "");
            if (a_eval(self, v->expressions[i])) return 1;
            out_put(self, "\n");
            --self->results.size; // ignore results of all but last expression
        }
        if (a_eval(self, v->expressions[v->n_expressions - 1])) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_user_type: {
        // eval each field in the user_type and assign to its matching struct field
        struct ast_user_type const *v = ast_node_ut((ast_node *)node);

        tl_type **type = type_registry_find_name(self->type_registry, ast_node_name_string(v->name));
        if (!type) fatal("a_eval: type '%s' not found in registry", ast_node_name_string(v->name));

        struct tlt_user const           *usertype = tl_type_user(*type);
        struct tlt_labelled_tuple const *lt       = tl_type_lt(usertype->labelled_tuple);

        for (u16 i = 0; i < v->n_fields; ++i) {
            if (a_eval(self, v->fields[i])) return 1;
            char *res = self->results.v[--self->results.size];
            out_put(self, "\n");
            out_put_start_fmt(self, "%s.%s = %s;\n", var, lt->names.v[i], res);
        }
    } break;

    case ast_user_type_get: {
        // emit object field access
        if (a_field_access(self, node)) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);

    } break;

    case ast_user_type_set: {
        // emit object field setter
        if (a_field_setter(self, node)) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);

    } break;

    case ast_infix: {
        if (a_infix(self, node)) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_labelled_tuple:
    case ast_tuple:
        // FIXME
        break;

    case ast_let_in: {
        if (a_let_in(self, node)) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);

    } break;

    case ast_let_match_in:
        // FIXME
        break;

    case ast_let: {
        if (a_let(self, node)) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application: break;

    case ast_named_function_application:  {
        if (a_fun_apply(self, node)) return 1;
        char *res = self->results.v[--self->results.size];
        out_put(self, "\n");
        out_put_start_fmt(self, "%s = %s;\n", var, res);
    } break;

    case ast_user_type_definition: break;
    }
    return 0;
}

static int a_fun_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);

    struct ast_named_application const *v    = ast_node_named((ast_node *)node);

    char const                         *name = null;
    if (v->specialized) {
        name = mos_string_str(&v->specialized->let.specialized_name);
    } else {
        // c_ and std_ etc...
        name = mos_string_str(&v->name);
    }

    char *var = next_variable(self);
    array_push(self->results, &var);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    i32 const n_args = v->n_arguments;
    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, v->arguments[i])) return 1;

    out_put_start(self, "");
    a_result_type_of(self, node->type);
    out_put_fmt(self, " %s;\n", var);

    out_put_start_fmt(self, "%s = %s(", var, name);

    for (i32 i = 0; i < n_args; ++i) {
        char *arg = self->results.v[--self->results.size];
        out_put(self, arg);
        if (i < n_args - 1) out_put(self, ", ");
    }
    out_put(self, ");\n");

    return 0;
}

static int a_main(transpiler *self, ast_node const *node) {
    if (ast_let != node->tag) return 0;

    struct ast_let const *v = ast_node_let((ast_node *)node);

    if (0 == mos_string_cmp_c(&v->name, "main")) {

        out_put(self, "int main(int argc, char* argv[]) {\n    (void)argc; (void)argv;\n\n");

        self->indent_level++;

        int res = 0;
        if ((res = a_eval(self, v->body))) return res;

        char *var = self->results.v[--self->results.size];

        out_put(self, "\n");
        out_put_start_fmt(self, "return (int) %s;", var);

        self->indent_level--;

        out_put(self, "\n}\n");
        return 0;
    }
    return 0;
}

static int a_let(transpiler *self, ast_node const *node) {

    struct ast_let const *v    = ast_node_let((ast_node *)node);

    char const           *name = mos_string_str(&v->specialized_name);
    if (0 == strlen(name)) name = mos_string_str(&v->name);

    if (is_generic_function(node)) {
        log(self, "skipping '%s' ('%s') because it is a generic function", mos_string_str(&v->name),
            mos_string_str(&v->specialized_name));
        return 0;
    }

    // don't emit generic template functions
    if (!ast_node_is_specialized(node)) {
        log(self, "skipping '%s' because it is not specialized", mos_string_str(&v->name));
        return 0;
    }

    if (0 == mos_string_cmp_c(&v->name, "main")) {
        // skip here, let a_main process it.
        return 0;
    }

    log(self, "processing '%s'...", mos_string_str(&v->specialized_name));

    // function declaration

    // return type
    out_put_start(self, "static ");
    if (a_result_type_of(self, v->arrow)) return 1;
    out_put(self, " ");

    // name
    out_put(self, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        if (a_result_type_of(self, v->parameters[i]->type)) return 1;
        out_put(self, " ");
        out_put(self, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    if (a_eval(self, v->body)) return 1;

    char *body = self->results.v[--self->results.size];
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");

    return 0;
}

static char *next_variable(transpiler *self) {
    int   len = snprintf(null, 0, "_res%zu_", self->next_variable) + 1;

    char *out = alloc_malloc(self->strings, (u32)len);
    snprintf(out, (u32)len, "_res%zu_", self->next_variable++);
    return out;
}

static bool is_generic_function(ast_node const *node) {
    if (ast_let != node->tag) return false;

    tl_type *arrow = node->let.arrow;
    if (arrow->arrow.right->tag == type_type_var) return true;

    struct tlt_tuple const *v = tl_type_tup(arrow->arrow.left);
    for (u32 i = 0; i < v->elements.size; ++i)
        if (type_type_var == v->elements.v[i]->tag) return true;

    return false;
}

void log(transpiler *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "transpiler: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}

#pragma clang diagnostic pop
