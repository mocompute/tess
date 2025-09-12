#include "transpiler.h"
#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "hashmap.h"

#include "ast_tags.h"
#include "dbg.h"
#include "string_t.h"
#include "type.h"
#include "type_registry.h"
#include "util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct transpiler {
    allocator     *alloc;
    allocator     *strings;
    char_array    *bytes;

    type_registry *type_registry;
    hashmap       *processed_structs;

    c_string_array results;
    // a stack of result variable names, see also next_variable

    size_t next_variable;
    int    indent_level;

    int    verbose;
};

// -- embed externs --

extern char const *embed_std_c;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);

static int a_declaration(transpiler *, tl_type const *, char const *);
static int a_eval(transpiler *, ast_node const *);
static int a_field_access(transpiler *, ast_node const *);
static int a_intrinsic_apply(transpiler *, ast_node const *);
static int a_fun_apply(transpiler *, ast_node const *);
static int a_infix(transpiler *, ast_node const *);
static int a_let(transpiler *, ast_node const *);
static int a_let_prototypes(transpiler *, ast_node const *);
static int a_let_in(transpiler *, ast_node const *);
static int a_let_match_in(transpiler *, ast_node const *);
static int a_let_struct_phase(transpiler *, ast_node const *);
static int a_main(transpiler *, ast_node const *);
// static int         a_nil_expression(transpiler *, ast_node const *);
static int         a_result_type_of(transpiler *, tl_type const *);
static int         a_toplevel(transpiler *, ast_node const *);
static int         a_tuple_cons(transpiler *, ast_node const *);
static int         a_user_type_definition(transpiler *, ast_node const *);

static char       *next_variable(transpiler *);
static char       *make_struct_name(allocator *, u64);
static char       *make_struct_constructor_name(allocator *, u64);

static char const *pop_result(transpiler *);
static void        push_result(transpiler *, char const *);
static void        pop_and_assign(transpiler *, char const *);

static void        out_put_start(transpiler *, char const *);
static void        out_put(transpiler *, char const *);
static void        out_put_start_fmt(transpiler *, char const *restrict, ...)
  __attribute__((format(printf, 2, 3)));
static void out_put_fmt(transpiler *, char const *restrict, ...) __attribute__((format(printf, 2, 3)));
static void vout_put_fmt(transpiler *, char const *restrict, va_list);

static int  is_generic_function(ast_node const *node);

static void log(transpiler *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

transpiler *transpiler_create(allocator *alloc, char_array *bytes, type_registry *tr) {

    transpiler *self        = alloc_calloc(alloc, 1, sizeof *self);
    self->alloc             = alloc;
    self->strings           = arena_create(alloc, 1024);
    self->bytes             = bytes;
    self->type_registry     = tr;
    self->processed_structs = map_create(alloc, sizeof(char *));

    self->results           = (c_string_array){.alloc = self->strings};

    self->next_variable     = 1;
    self->indent_level      = 0;
    self->verbose           = 0;

    return self;
}

void transpiler_destroy(transpiler **self) {
    map_destroy(&(*self)->processed_structs);
    arena_destroy((*self)->alloc, &(*self)->strings);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

void transpiler_set_verbose(transpiler *self, int verbose) {
    self->verbose = verbose;
}

int transpiler_compile(transpiler *self, struct ast_node **nodes, u32 n) {
    (void)self;

    // output std header
    out_put(self, embed_std_c);
    out_put(self, "\n\n");

    // output generated structs
    out_put_start(self, "\n// -- begin structs -- \n\n");

    for (size_t i = 0; i < n; ++i) {
        int res = 0;
        if (ast_let == nodes[i]->tag)
            if ((res = a_let_struct_phase(self, nodes[i]))) return res;
    }

    out_put_start(self, "\n// -- end structs -- \n\n");

    // output all function prototypes
    out_put_start(self, "\n// -- begin prototypes -- \n\n");
    for (size_t i = 0; i < n; ++i) {
        a_let_prototypes(self, nodes[i]);
    }
    out_put_start(self, "\n// -- end prototypes -- \n\n");

    // output toplevel forms
    out_put_start(self, "\n// -- begin program -- \n\n");
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

    out_put_start(self, "\n// -- end program -- \n\n");

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

    char *buf = alloc_malloc(self->strings, len);
    va_start(args, fmt);
    vsnprintf(buf, (size_t)len, fmt, args);
    va_end(args);

    array_copy(*self->bytes, buf, strlen(buf));
    alloc_free(self->strings, buf);
}

static void vout_put_fmt(transpiler *self, char const *restrict fmt, va_list args) {

    va_list args2;
    va_copy(args2, args);

    int len = vsnprintf(null, 0, fmt, args) + 1;
    if (len <= 0) fatal("out_put_fmt: invalid fmt string: %s", fmt);

    char *buf = alloc_malloc(self->strings, len);
    vsnprintf(buf, (size_t)len, fmt, args2);

    array_copy(*self->bytes, buf, strlen(buf));
    alloc_free(self->strings, buf);

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

static int is_nil_result(tl_type const *type) {
    switch (type->tag) {
    case type_nil:            return 1;
    case type_bool:
    case type_int:
    case type_float:
    case type_string:         return 0;
    case type_tuple:
    case type_labelled_tuple: return type->array.elements.size == 0;

    case type_arrow:          {
        tl_type *right = type->arrow.right;
        if (right->tag == type_nil) return 1;
        if (right->tag == type_tuple && right->array.elements.size == 0) return 1;
        return 0;
    }
    case type_user:
    case type_type_var:
    case type_pointer:
    case type_any:      return 0;
    }
}

static int a_declaration(transpiler *self, tl_type const *type, char const *var) {

    if (type->tag != type_arrow) {
        // don't emit a declaration if the return type is nil
        if (is_nil_result(type)) return 0;

        a_result_type_of(self, type);
        out_put_fmt(self, " %s", var);
    } else {
        // make a function pointer type
        tl_type *left = type->arrow.left;

        a_declaration(self, type->arrow.right, "");
        out_put_fmt(self, " (*%s) (", var);

        for (u32 i = 0; i < left->array.elements.size; ++i) {
            a_declaration(self, left->array.elements.v[i], "");
            if (i < left->array.elements.size - 1) out_put(self, ", ");
        }

        out_put(self, ") ");
    }
    return 0;
}

static int a_result_type_of(transpiler *self, tl_type const *ty) {

    if (!ty) fatal("a_result_type_of: null type");

    switch (ty->tag) {
    case type_nil:            out_put(self, "void"); break;
    case type_bool:           out_put(self, "int"); break;
    case type_int:            out_put(self, "int64_t"); break;
    case type_float:          out_put(self, "double"); break;
    case type_string:         out_put(self, "char *"); break;

    case type_labelled_tuple:
    case type_tuple:          {
        if (ty->array.elements.size == 0) {
            out_put(self, "void");
            return 0;
        };

        u64   hash = tl_type_hash(ty);
        char *name = make_struct_name(self->alloc, hash);
        out_put_fmt(self, "struct %s", name);
        alloc_free(self->alloc, name);
    } break;

    case type_any:   out_put(self, "int"); break;

    case type_arrow: fatal("logic error"); break;

    case type_user:  {
        char *type_s = tl_type_to_string(self->strings, ty);
        out_put_fmt(self, "/* %s */ struct %s", type_s, ty->user.name);
        alloc_free(self->strings, type_s);

    } break;

    case type_type_var: out_put_fmt(self, "/* tv%u */ int", ty->type_var.val); break;

    case type_pointer:
        a_result_type_of(self, ty->pointer.target);
        out_put(self, "*");
        break;
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
        a_declaration(self, ty, field_name);
        out_put(self, ";\n");
    }
    self->indent_level--;

    out_put_start(self, "};\n");

    return 0;
}

static int a_toplevel(transpiler *self, ast_node const *node) {
    if (ast_let == node->tag) return a_let(self, node);
    if (ast_user_type_definition == node->tag) return a_user_type_definition(self, node);
    return 0;
}

static int a_infix(transpiler *self, ast_node const *node) {

    tl_type const *type = node->type;

    // Eval left and right, resulting in two result variables on the
    // stack. Eval in reverse order so we can pop them in correct
    // order.
    if (a_eval(self, node->infix.right)) return 1;
    if (a_eval(self, node->infix.left)) return 1;
    char const *left  = pop_result(self);
    char const *right = pop_result(self);

    char       *var   = next_variable(self);

    out_put_start(self, "");
    if (a_declaration(self, type, var)) return 1;
    out_put_fmt(self, " = %s ", left);

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

    if (a_eval(self, node->let_in.value)) return 1;
    char const *value = pop_result(self);

    out_put_start(self, "");
    a_declaration(self, node->let_in.name->type, name);
    out_put_fmt(self, " = %s;\n", value);

    // eval the body and leave it on the stack
    if (a_eval(self, node->let_in.body)) return 1;

    return 0;
}

static int a_let_match_in(transpiler *self, ast_node const *node) {

    // let tup = (a = 1, b = 0) in
    // let (res = b) = tup in res end

    struct ast_let_match_in   *v  = ast_node_let_match_in((ast_node *)node);
    struct ast_labelled_tuple *lt = ast_node_lt(v->lt);

    // eval the value so we can access it
    if (a_eval(self, v->value)) return 1;
    char const *value = pop_result(self);

    // declare variables for each assignment
    for (u32 i = 0; i < lt->n_assignments; ++i) {
        // do a field access for the named field of the node's value
        struct ast_assignment *ass        = ast_node_assignment(lt->assignments[i]);
        char const            *var_name   = ast_node_name_string(ass->name);
        char const            *field_name = ast_node_name_string(ass->value);

        out_put_start(self, "");
        a_declaration(self, ass->value->type, var_name);
        out_put_start_fmt(self, " = %s.%s;\n", value, field_name);
    }

    // eval the body and leave it on the stack
    if (a_eval(self, v->body)) return 1;

    return 0;
}

static int a_field_access(transpiler *self, ast_node const *node) {

    struct ast_user_type_get const *v   = ast_node_utg((ast_node *)node);

    char                           *var = next_variable(self);

    out_put_start(self, "");
    a_declaration(self, node->type, var);
    if (TEST_BIT(v->flags, AST_UT_FLAG_POINTER))
        out_put_start_fmt(self, " = %s->%s;\n", ast_node_name_string(v->struct_name),
                          ast_node_name_string(v->field_name));
    else
        out_put_start_fmt(self, " = %s.%s;\n", ast_node_name_string(v->struct_name),
                          ast_node_name_string(v->field_name));

    return 0;
}

static int a_field_setter(transpiler *self, ast_node const *node) {

    struct ast_user_type_set const *v           = ast_node_uts((ast_node *)node);
    char const                     *struct_name = ast_node_name_string(v->struct_name);
    char const                     *field_name  = ast_node_name_string(v->field_name);
    char                           *var         = next_variable(self);

    // eval the value
    if (a_eval(self, v->value)) return 1;
    char const *value = pop_result(self);

    // output value is field set value
    out_put_start(self, "");
    a_declaration(self, node->type, var);
    out_put_start_fmt(self, " = %s;\n", value);

    // assign to struct field
    if (TEST_BIT(v->flags, AST_UT_FLAG_POINTER))
        out_put_start_fmt(self, "%s->%s = %s;\n", struct_name, field_name, var);
    else out_put_start_fmt(self, "%s.%s = %s;\n", struct_name, field_name, var);

    return 0;
}

// static int a_nil_expression(transpiler *self, ast_node const *node) {
//     // an expression of type nil: there is no need to capture its
//     // result
//     assert(type_nil == node->type->tag);
//     return a_eval(self, node);
// }

static int a_eval(transpiler *self, ast_node const *node) {

    if (!node || !node->type) fatal("a_eval: node or type is null");

    // FIXME what is this trying to do?
    // if (node->type->tag == type_nil) {
    //     return a_nil_expression(self, node);
    // }

    char *var = next_variable(self);

    out_put(self, "\n");

    out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->strings, node));

    out_put_start(self, "");
    a_declaration(self, node->type, var);
    out_put(self, ";\n");

    switch (node->tag) {
    case ast_assignment:
    case ast_arrow:
    case ast_eof:
    case ast_nil:        out_put_start_fmt(self, "%s = NULL;\n", var); break;
    case ast_symbol:     out_put_start_fmt(self, "%s = %s;\n", var, ast_node_name_string(node)); break;
    case ast_string:     out_put_start_fmt(self, "%s = \"%s\";\n", var, ast_node_name_string(node)); break;
    case ast_i64:        out_put_start_fmt(self, "%s = %" PRIi64 ";\n", var, node->i64.val); break;
    case ast_u64:        out_put_start_fmt(self, "%s = %" PRIu64 ";\n", var, node->u64.val); break;
    case ast_f64:        out_put_start_fmt(self, "%s = %f;\n", var, node->f64.val); break;
    case ast_bool:
        if (node->bool_.val) out_put_start_fmt(self, "%s = 1;\n", var);
        else out_put_start_fmt(self, "%s = 0;\n", var);
        break;

    case ast_address_of: {
        if (a_eval(self, node->address_of.target)) return 1;
        char const *res = pop_result(self);
        out_put_start_fmt(self, "%s = &(%s);\n", var, res);
    } break;

    case ast_dereference: {
        if (a_eval(self, node->dereference.target)) return 1;
        char const *res = pop_result(self);
        out_put_start_fmt(self, "%s = *(%s);\n", var, res);
    } break;

    case ast_begin_end: {
        struct ast_begin_end const *v = ast_node_begin_end((ast_node *)node);
        if (v->n_expressions == 0) break;
        for (u32 i = 0; i < v->n_expressions - 1; ++i) {
            out_put_start(self, "");
            if (a_eval(self, v->expressions[i])) return 1;
            out_put(self, "\n");
            (void)pop_result(self); // ignore results of all but last expression
        }
        if (a_eval(self, v->expressions[v->n_expressions - 1])) return 1;
        pop_and_assign(self, var);
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
            char const *res = pop_result(self);
            out_put(self, "\n");
            out_put_start_fmt(self, "%s.%s = %s;\n", var, lt->names.v[i], res);
        }
    } break;

    case ast_user_type_get:
        // emit object field access
        if (a_field_access(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_user_type_set:
        // emit object field setter
        if (a_field_setter(self, node)) return 1;
        pop_and_assign(self, var);

        break;

    case ast_infix:
        if (a_infix(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_labelled_tuple:
    case ast_tuple:
        if (a_tuple_cons(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_let_in:
        if (a_let_in(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_let_match_in:
        if (a_let_match_in(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_let:
        if (a_let(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application: break;

    case ast_named_function_application:  {
        if (TEST_BIT(node->named_application.flags, AST_NAMED_APP_INTRINSIC)) {
            if (a_intrinsic_apply(self, node)) return 1;
        } else {
            if (a_fun_apply(self, node)) return 1;
        }
        pop_and_assign(self, var);
    } break;

    case ast_user_type_definition: break;
    }
    return 0;
}

static int expand_value(transpiler *self, ast_node const *node) {
    switch (node->tag) {

    case ast_symbol:
        //
        out_put(self, ast_node_name_string(node));
        break;

    case ast_nil:
    case ast_address_of:
    case ast_arrow:
    case ast_assignment:
    case ast_bool:       break;

    case ast_dereference:
        out_put(self, "* ");
        expand_value(self, node->dereference.target);
        break;

    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_if_then_else:
    case ast_infix:
    case ast_let_in:
    case ast_let_match_in:
    case ast_string:
    case ast_u64:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_lambda_function:
    case ast_lambda_function_application:
    case ast_let:
    case ast_named_function_application:
    case ast_tuple:
    case ast_user_type:
        //
        out_put(self, "FIXME:expand");
        break;
    }
    return 0;
}

static int a_intrinsic_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);
    struct ast_named_application *v    = ast_node_named((ast_node *)node);

    char const                   *name = ast_node_name_string(v->name);

    // TODO intrinsics dispatch table

    if (0 == strcmp("_tl_sizeof_", name)) {
        if (v->n_arguments != 1) fatal("wrong number of arguments: '%s'", name);

        // function call result
        char *var = next_variable(self);
        out_put_start(self, "");
        a_declaration(self, node->type, var);
        out_put(self, ";\n");

        out_put_start_fmt(self, "%s = (sizeof (", var);
        expand_value(self, v->arguments[0]);
        out_put(self, "));\n");
    }

    else if (0 == strcmp("_tl_add_", name)) {
        if (v->n_arguments != 2) fatal("wrong number of arguments: '%s'", name);

        // function call result
        char *var = next_variable(self);
        out_put_start(self, "");
        a_declaration(self, node->type, var);
        out_put(self, ";\n");

        // eval the args
        a_eval(self, v->arguments[1]);
        a_eval(self, v->arguments[0]);
        char const *lhs = pop_result(self);
        char const *rhs = pop_result(self);

        out_put_start_fmt(self, "%s = %s + %s;\n", var, lhs, rhs);
    }

    else if (0 == strcmp("_tl_sub_", name)) {
        if (v->n_arguments != 2) fatal("wrong number of arguments: '%s'", name);

        // function call result
        char *var = next_variable(self);
        out_put_start(self, "");
        a_declaration(self, node->type, var);
        out_put(self, ";\n");

        // eval the args
        a_eval(self, v->arguments[1]);
        a_eval(self, v->arguments[0]);
        char const *lhs = pop_result(self);
        char const *rhs = pop_result(self);

        out_put_start_fmt(self, "%s = %s - %s;\n", var, lhs, rhs);
    }

    else
        fatal("unknown intrinsic: '%s'", name);

    return 0;
}

static int a_fun_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);

    struct ast_named_application const *v    = ast_node_named((ast_node *)node);

    char const                         *name = null;
    if (v->specialized) {
        name = string_t_str(&v->specialized->let.specialized_name);
    } else {
        // c_ and std_ etc...
        name = ast_node_name_string(v->name);

        if (0 == strncmp("c_", name, 2)) {
            name += 2; // strip off c_ prefix
        }
    }

    char *var = next_variable(self);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    i32 const n_args = v->n_arguments;
    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, v->arguments[i])) return 1;

    // function call result
    out_put_start(self, "");
    a_declaration(self, node->type, var);
    out_put(self, ";\n");

    // function call

    tl_type *fun_type = node->named_application.name->type;

    if (is_nil_result(fun_type)) out_put_start(self, "");
    else out_put_start_fmt(self, "%s = ", var);

    out_put_fmt(self, "%s(", name);
    for (i32 i = 0; i < n_args; ++i) {
        char const *arg = pop_result(self);
        out_put(self, arg);
        if (i < n_args - 1) out_put(self, ", ");
    }
    out_put(self, ");\n");

    if (is_nil_result(fun_type)) out_put_start_fmt(self, "%s = 0;/* void */\n", var);

    return 0;
}

static int a_tuple_init(transpiler *self, ast_node const *node) {

    char *var = next_variable(self);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    struct ast_tuple const *v      = ast_node_tuple((ast_node *)node);
    i32 const               n_args = v->n_elements;

    u64                     hash   = tl_type_hash(node->type);
    char                   *name   = make_struct_name(self->alloc, hash);

    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, v->elements[i])) return 1;

    // init result
    out_put_start(self, "");
    a_declaration(self, node->type, var);
    out_put(self, ";\n");

    for (u32 i = 0; i < (u32)n_args; ++i) {
        char buf[32];
        snprintf(buf, sizeof buf - 1, "x%u", i);

        char const *arg = pop_result(self);

        out_put_start_fmt(self, "%s.%s = %s;\n", var, buf, arg);
    }

    alloc_free(self->alloc, name);

    return 0;
}

static int a_labelled_tuple_init(transpiler *self, ast_node const *node) {

    char *var = next_variable(self);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    struct ast_labelled_tuple const *v      = ast_node_lt((ast_node *)node);
    i32 const                        n_args = v->n_assignments;
    u64                              hash   = tl_type_hash(node->type);
    char                            *name   = make_struct_name(self->alloc, hash);

    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, v->assignments[i]->assignment.value)) return 1;

    // init result
    out_put_start(self, "");
    a_declaration(self, node->type, var);
    out_put(self, ";\n");

    for (u32 i = 0; i < (u32)n_args; ++i) {

        char const *arg = pop_result(self);

        out_put_start_fmt(self, "%s.%s = %s;\n", var,
                          ast_node_name_string(v->assignments[i]->assignment.name), arg);
    }

    alloc_free(self->alloc, name);

    return 0;
}

static int a_tuple_cons(transpiler *self, ast_node const *node) {
    // intercept tuple init to construct tuple in place rather than
    // invoke constructor function
    if (ast_tuple == node->tag && TEST_BIT(node->tuple.flags, AST_TUPLE_FLAG_INIT)) {
        return a_tuple_init(self, node);
    }
    if (ast_labelled_tuple == node->tag && TEST_BIT(node->labelled_tuple.flags, AST_TUPLE_FLAG_INIT)) {
        return a_labelled_tuple_init(self, node);
    }

    char *name = make_struct_constructor_name(self->alloc, tl_type_hash(node->type));

    char *var  = next_variable(self);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    i32 n_args = 0;

    if (ast_tuple == node->tag) {
        struct ast_tuple const *v = ast_node_tuple((ast_node *)node);
        n_args                    = v->n_elements;
        if (n_args)
            for (i32 i = n_args - 1; i >= 0; --i)
                if (a_eval(self, v->elements[i])) return 1;
    } else {
        struct ast_labelled_tuple const *v = ast_node_lt((ast_node *)node);
        n_args                             = v->n_assignments;
        if (n_args)
            for (i32 i = n_args - 1; i >= 0; --i)
                if (a_eval(self, v->assignments[i]->assignment.value)) return 1;
    }

    // function call result
    out_put_start(self, "");
    a_declaration(self, node->type, var);
    out_put(self, ";\n");

    // function call
    out_put_start_fmt(self, "%s = %s(", var, name);

    for (i32 i = 0; i < n_args; ++i) {
        char const *arg = pop_result(self);
        out_put(self, arg);
        if (i < n_args - 1) out_put(self, ", ");
    }
    out_put(self, ");\n");

    alloc_free(self->alloc, name);

    return 0;
}

static int a_main(transpiler *self, ast_node const *node) {
    if (ast_let != node->tag) return 0;

    struct ast_let const *v = ast_node_let((ast_node *)node);

    if (0 == string_t_cmp_c(&v->name->symbol.name, "main")) {

        out_put(self, "int main(int argc, char* argv[]) {\n    (void)argc; (void)argv;\n\n");

        self->indent_level++;

        int res = 0;
        if ((res = a_eval(self, v->body))) return res;

        char const *var = pop_result(self);

        out_put(self, "\n");
        out_put_start_fmt(self, "return (int) %s;", var);

        self->indent_level--;

        out_put(self, "\n}\n");
        return 0;
    }
    return 0;
}

static char *make_struct_name(allocator *alloc, u64 hash) {
    char *name = null;
    {
#define fmt "_gen_struct_tup_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static char *make_struct_constructor_name(allocator *alloc, u64 hash) {
    char *name = null;
    {
#define fmt "_gen_make_tup_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static int a_let_struct_phase(transpiler *self, ast_node const *node) {

    if (!ast_node_is_tuple_constructor(node)) return 0;

    struct ast_let const *v    = ast_node_let((ast_node *)node);

    char const           *name = string_t_str(&v->specialized_name);
    if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    log(self, "processing struct let '%s'...", string_t_str(&v->specialized_name));

    tl_type *tuple          = v->arrow->arrow.left;
    u64      hash           = tl_type_hash(tuple);
    char    *generated_name = make_struct_name(self->alloc, hash);
    if (map_get(self->processed_structs, generated_name, strlen(generated_name))) return 0;
    map_set(&self->processed_structs, generated_name, strlen(generated_name), generated_name);

    out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->strings, node));

    out_put_start_fmt(self, "struct %s {\n", generated_name);
    self->indent_level++;

    if (type_tuple == tuple->tag) {
        struct tlt_tuple *tup = tl_type_tup(tuple);

        for (u32 i = 0; i < tup->elements.size; ++i) {
            char buf[32];
            snprintf(buf, sizeof buf - 1, "x%u", i);

            out_put_start(self, "");
            a_declaration(self, tup->elements.v[i], buf);
            out_put(self, ";\n");
        }
    } else if (type_labelled_tuple == tuple->tag) {
        struct tlt_labelled_tuple *lt = tl_type_lt(tuple);

        for (u32 i = 0; i < lt->fields.size; ++i) {

            out_put_start(self, "");
            a_declaration(self, lt->fields.v[i], lt->names.v[i]);
            out_put(self, ";\n");
        }

    } else {
        fatal("expected tuple type");
    }

    self->indent_level--;
    out_put_start(self, "};\n");

    // function declaration
    // TODO copied from a_let
    name = string_t_str(&v->specialized_name);
    if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    // return type and name
    out_put_start(self, "static ");
    a_declaration(self, v->arrow->arrow.right, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        a_declaration(self, v->parameters[i]->type, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    if (a_eval(self, v->body)) return 1;

    char const *body = pop_result(self);
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");

    alloc_free(self->alloc, generated_name);

    return 0;
}

static int a_let_prototypes(transpiler *self, ast_node const *node) {
    if (ast_let != node->tag) return 0;
    if (ast_node_is_tuple_constructor(node)) return 0; // handled by a_let_struct_phase

    struct ast_let const *v    = ast_node_let((ast_node *)node);
    char const           *name = string_t_str(&v->specialized_name);
    if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    if (0 == string_t_cmp_c(&v->name->symbol.name, "main")) {
        // skip here, let a_main process it.
        return 0;
    }

    // function declaration

    // return type and name
    out_put_start(self, "static ");
    a_declaration(self, v->arrow->arrow.right, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        a_declaration(self, v->parameters[i]->type, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ");\n");

    return 0;
}

static int a_let(transpiler *self, ast_node const *node) {

    struct ast_let const *v    = ast_node_let((ast_node *)node);

    char const           *name = string_t_str(&v->specialized_name);
    if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    if (is_generic_function(node)) {
        log(self, "skipping '%s' ('%s') because it is a generic function",
            string_t_str(&v->name->symbol.name), string_t_str(&v->specialized_name));
        return 0;
    }

    if (ast_node_is_tuple_constructor(node)) return 0; // handled by a_let_struct_phase

    if (0 == string_t_cmp_c(&v->name->symbol.name, "main")) {
        // skip here, let a_main process it.
        return 0;
    }

    log(self, "processing '%s'...", string_t_str(&v->specialized_name));

    // function declaration

    // return type and name
    out_put_start(self, "static ");
    a_declaration(self, v->arrow->arrow.right, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        a_declaration(self, v->parameters[i]->type, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    if (a_eval(self, v->body)) return 1;

    char const *body = pop_result(self);
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");

    return 0;
}

static char *next_variable(transpiler *self) {
    int   len = snprintf(null, 0, "_res%zu_", self->next_variable) + 1;

    char *out = alloc_malloc(self->strings, (u32)len);
    snprintf(out, (u32)len, "_res%zu_", self->next_variable++);
    push_result(self, out);
    return out;
}

static int is_generic_function(ast_node const *node) {
    if (ast_let != node->tag) return 0;

    tl_type *arrow = node->let.arrow;
    if (arrow->arrow.right->tag == type_type_var) return 1;

    struct tlt_array const *v = tl_type_arr(arrow->arrow.left);
    for (u32 i = 0; i < v->elements.size; ++i)
        if (type_type_var == v->elements.v[i]->tag) return 1;

    return 0;
}

static void log(transpiler *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "transpiler: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args);
    va_end(args);
}

static char const *pop_result(transpiler *self) {
    assert(self->results.size);
    return self->results.v[--self->results.size];
}

static void push_result(transpiler *self, char const *var) {
    array_push(self->results, &var);
}

static void pop_and_assign(transpiler *self, char const *var) {
    char const *res = pop_result(self);
    out_put(self, "\n");
    out_put_start_fmt(self, "%s = %s;\n", var, res);
}
