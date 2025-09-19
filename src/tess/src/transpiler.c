#include "transpiler.h"
#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "hashmap.h"

#include "ast_tags.h"
#include "dbg.h"
#include "hash.h"
#include "string_t.h"
#include "type.h"
#include "type_inference.h"
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
    allocator     *transient;
    char_array    *bytes;

    type_registry *type_registry;
    ti_inferer    *type_inferer;
    hashmap       *processed_structs;

    c_string_array results;
    // a stack of result variable names, see also next_variable

    size_t next_variable;
    int    indent_level;
    int    verbose;

    // state which affects operation of a_eval
    int             is_eval_in_thunk;
    c_string_carray thunk_free_variables;
    hashmap        *functions;      // char const* -> ast_node const* (let)
    hashmap        *lambdas;        // char const* -> ast_node const*
    hashmap        *apply_contexts; // u64 -> u64
};

// -- embed externs --

extern char const *embed_std_c;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);

static int         a_declaration(transpiler *, tl_type const *, ast_node const *, char const *);
static int         a_declaration_void_ok(transpiler *, tl_type const *, ast_node const *, char const *);
static int         a_eval(transpiler *, ast_node const *);
static int         a_if_then_else(transpiler *, ast_node const *);
static int         a_field_setter(transpiler *, ast_node const *);
static int         a_field_access(transpiler *, ast_node const *);
static int         a_intrinsic_apply(transpiler *, ast_node const *);
static int         a_fun_apply(transpiler *, ast_node const *);
static int         a_let(transpiler *, ast_node const *);
static int         a_let_prototypes(transpiler *, ast_node const *);
static int         a_let_in(transpiler *, ast_node const *);
static int         a_let_match_in(transpiler *, ast_node const *);
static int         a_let_struct_phase(transpiler *, ast_node const *);
static int         a_main(transpiler *, ast_node const *);
static int         a_result_type_of(transpiler *, tl_type const *);
static int         a_toplevel(transpiler *, ast_node const *);
static int         a_thunk(transpiler *, ast_node const *);
static int         a_tuple_cons(transpiler *, ast_node const *);
static int         a_user_type_definition(transpiler *, ast_node const *);

static char       *next_variable(transpiler *);
static char       *make_struct_name(allocator *, u64);
static char       *make_struct_constructor_name(allocator *, u64);
static char       *make_thunk_name(allocator *, u64);
static char       *make_thunk_struct_name(allocator *, u64);
static char       *make_function_name(allocator *, char const *);
static char       *emit_symbol(transpiler *, ast_node const *);
static u64         hash_name_and_vars(ast_node const *);

static void        generate_thunks(transpiler *, ast_node **, u32);
static u32         push_free_variables(transpiler *, ast_node const *, u32 *);
static u32         push_free_variables_ext(transpiler *, ast_node_sized, u32 *);
static void        pop_free_variables(transpiler *, u32);

static char const *pop_result(transpiler *);
static void        push_result(transpiler *, char const *);
static void        pop_and_assign(transpiler *, char const *);

static void        out_put_start(transpiler *, char const *);
static void        out_put(transpiler *, char const *);
static void        out_put_start_fmt(transpiler *, char const *restrict, ...)
  __attribute__((format(printf, 2, 3)));
static void out_put_fmt(transpiler *, char const *restrict, ...) __attribute__((format(printf, 2, 3)));
static void vout_put_fmt(transpiler *, char const *restrict, va_list);

static int  is_generic_function(transpiler *, ast_node const *node);

static void log(transpiler *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

transpiler *transpiler_create(allocator *alloc, char_array *bytes, type_registry *tr, ti_inferer *ti) {

    transpiler *self           = alloc_calloc(alloc, 1, sizeof *self);
    self->alloc                = alloc;
    self->strings              = arena_create(alloc, 2048);
    self->transient            = arena_create(alloc, 2048);
    self->bytes                = bytes;
    self->type_registry        = tr;
    self->type_inferer         = ti;
    self->processed_structs    = map_create(alloc, sizeof(char *));

    self->thunk_free_variables = (c_string_carray){.alloc = self->transient};
    self->functions            = map_create(self->transient, sizeof(ast_node *));
    self->lambdas              = map_create(self->transient, sizeof(ast_node *));
    self->apply_contexts       = map_create(self->transient, sizeof(u64));

    self->results              = (c_string_array){.alloc = self->strings};

    self->next_variable        = 1;
    self->indent_level         = 0;
    self->is_eval_in_thunk     = 0;
    self->verbose              = 0;

    return self;
}

void transpiler_destroy(transpiler **self) {
    map_destroy(&(*self)->apply_contexts);
    map_destroy(&(*self)->lambdas);
    map_destroy(&(*self)->functions);
    array_free((*self)->thunk_free_variables);
    array_free((*self)->results);
    map_destroy(&(*self)->processed_structs);

    arena_destroy((*self)->alloc, &(*self)->transient);
    arena_destroy((*self)->alloc, &(*self)->strings);

    alloc_free((*self)->alloc, *self);
    *self = null;
}

void transpiler_set_verbose(transpiler *self, int verbose) {
    self->verbose = verbose;
}

int transpiler_compile(transpiler *self, ast_node **nodes, u32 n) {
    (void)self;

    // build functions map
    for (u32 i = 0; i < n; ++i) {
        ast_node *node = nodes[i];
        if (ast_let == node->tag) {
            char const *name = ast_node_name_string(node->let.name);
            map_set(&self->functions, name, strlen(name), &node);
        }
    }

    // output std header
    out_put(self, embed_std_c);
    out_put(self, "\n\n");

    out_put_start(self, "\n// -- begin user types -- \n\n");

    for (size_t i = 0; i < n; ++i) {
        ast_node *node = nodes[i];
        if (ast_user_type_definition == node->tag) a_user_type_definition(self, node);
    }

    out_put_start(self, "\n// -- end user types -- \n\n");

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

    out_put_start(self, "\n// -- begin thunks -- \n\n");
    generate_thunks(self, nodes, n);
    out_put_start(self, "\n// -- end thunks -- \n\n");

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

typedef struct {
    transpiler *self;
    hashmap    *map; // u64 -> int
} generate_thunks_ctx;

static void emit_thunk_struct_init(transpiler *self, char const *struct_name, char const *var_name,
                                   ast_node_sized variables) {

    out_put_start_fmt(self, "struct %s %s = {\n", struct_name, var_name);
    self->indent_level++;
    {
        ast_node *ptr             = ast_node_create(self->transient, ast_address_of);
        ptr->address_of.target    = null;
        ptr->type                 = tl_type_create(self->transient, type_pointer);
        ptr->type->pointer.target = null;

        hashmap *seen             = map_create(self->transient, sizeof(int));

        for (u32 i = 0; i < variables.size; ++i) {
            int         one      = 1;
            char const *name_str = ast_node_name_string(variables.v[i]);
            if (map_get(seen, name_str, strlen(name_str))) continue;
            map_set(&seen, name_str, strlen(name_str), &one);

            ptr->address_of.target    = variables.v[i];
            ptr->type->pointer.target = variables.v[i]->type;

            out_put_start_fmt(self, ".%s = &(%s),\n", name_str, emit_symbol(self, variables.v[i]));
        }
    }
    self->indent_level--;
    out_put_start(self, "};\n\n");
}

static void emit_thunk_struct(transpiler *self, char const *name, ast_node_sized variables) {

    out_put_start_fmt(self, "struct %s {\n", name);
    self->indent_level++;
    {
        ast_node *ptr             = ast_node_create(self->transient, ast_address_of);
        ptr->address_of.target    = null;
        ptr->type                 = tl_type_create(self->transient, type_pointer);
        ptr->type->pointer.target = null;

        hashmap *seen             = map_create(self->transient, sizeof(int));
        for (u32 i = 0; i < variables.size; ++i) {
            int         one      = 1;
            char const *name_str = ast_node_name_string(variables.v[i]);
            if (map_get(seen, name_str, strlen(name_str))) continue;
            map_set(&seen, name_str, strlen(name_str), &one);

            ptr->address_of.target    = variables.v[i];
            ptr->type->pointer.target = variables.v[i]->type;

            out_put_start(self, "");
            a_declaration(self, ptr->type, null, name_str);
            out_put(self, ";\n");
        }
    }
    self->indent_level--;
    out_put_start(self, "};\n\n");
}

static void make_one_thunk(generate_thunks_ctx *ctx, ast_node *node) {
    transpiler *self = ctx->self;

    u64         hash = ast_node_hash(node);
    if (map_get(ctx->map, &hash, sizeof hash)) return;

    int one = 1;
    map_set(&ctx->map, &hash, sizeof hash, &one);

    // figure out the free variables in use in this node
    ast_node_sized free_variables = ti_free_variables_in(self->transient, node);

    // declare struct for thunk context
    char *struct_name = make_thunk_struct_name(self->strings, hash);
    emit_thunk_struct(self, struct_name, free_variables);

    // function declaration
    char *name = make_thunk_name(self->strings, hash);

    // return type and name
    out_put_start(self, "static ");
    a_declaration_void_ok(self, node->type, node, name);
    out_put(self, " ");

    // params
    out_put_fmt(self, "(struct %s * tl_ctx)", struct_name);

    // body
    out_put(self, " {\n");
    self->indent_level++;
    out_put_start(self, "(void)tl_ctx;\n");

    // debug
    out_put_start(self, "/* free variables: ");
    forall(i, free_variables) {
        out_put_fmt(self, "%s, ", ast_node_name_string(free_variables.v[i]));
    }
    out_put(self, " */\n");

    // add my free variables to the stack
    u32 save = push_free_variables(self, node, null);
    a_thunk(self, node);
    pop_free_variables(self, save);

    char const *body = pop_result(self);
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");
}

static void make_lambda_thunk(generate_thunks_ctx *ctx, ast_node *node) {
    transpiler                 *self = ctx->self;
    struct ast_lambda_function *v    = ast_node_lf(node);
    u64                         hash = ast_node_hash(node);

    if (map_get(ctx->map, &hash, sizeof hash)) return;
    int one = 1;
    map_set(&ctx->map, &hash, sizeof hash, &one);

    // declare struct for thunk context
    char *struct_name = make_thunk_struct_name(self->strings, hash);
    emit_thunk_struct(self, struct_name, v->free_variables);

    // function declaration
    char *name = make_thunk_name(self->strings, hash);

    // return type and name
    assert(type_arrow == node->type->tag);
    out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->transient, node));
    out_put_start(self, "static ");
    a_declaration_void_ok(self, node->type->arrow.right, null, name);
    out_put(self, " ");

    // params
    out_put_fmt(self, "(struct %s * tl_ctx", struct_name);
    for (u32 i = 0; i < v->n_parameters; ++i) {
        out_put(self, ", ");
        a_declaration(self, v->parameters[i]->type, v->parameters[i],
                      ast_node_name_string(v->parameters[i]));
    }
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;
    out_put_start(self, "(void)tl_ctx;\n");

    // debug
    out_put_start(self, "/* free variables: ");
    forall(i, v->free_variables) {
        out_put_fmt(self, "%s, ", ast_node_name_string(v->free_variables.v[i]));
    }
    out_put(self, " */\n");

    // add my free variables to the stack
    u32 save = push_free_variables(self, node, null);
    a_thunk(self, node->lambda_function.body);
    pop_free_variables(self, save);

    char const *body = pop_result(self);
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");
}

static void forward_declare_thunks(void *ctx_, ast_node *node) {
    generate_thunks_ctx *ctx  = ctx_;
    transpiler          *self = ctx->self;
    if (ast_named_function_application == node->tag) {
        struct ast_named_application const *v = ast_node_named(node);

        if (v->free_variables.size) {
            log(self, "thunk: named-application requires free %u variables: '%s'", v->free_variables.size,
                ast_node_to_string(self->strings, node->named_application.name));

            u64 hash = hash_name_and_vars(node);
            if (map_contains(self->apply_contexts, &hash, sizeof hash)) return;
            map_set(&self->apply_contexts, &hash, sizeof hash, &hash);

            char const      *function_name = ast_node_name_string(v->name);
            char const      *struct_name   = make_thunk_struct_name(self->transient, hash);
            char const      *thunk_name    = make_thunk_name(self->transient, hash);

            ast_node const **found_let_node =
              map_get(self->functions, function_name, strlen(function_name));
            if (!found_let_node) fatal("function not found: '%s'", function_name);
            ast_node const *let_node = *found_let_node;
            assert(ast_let == let_node->tag);

            out_put_start_fmt(self, "struct %s;\n", struct_name);
            out_put_start(self, "static ");
            a_declaration_void_ok(self, node->type, node, thunk_name);
            out_put(self, " ");

            // params
            out_put_fmt(self, "(struct %s * tl_ctx", struct_name);
            for (u32 i = 0; i < let_node->let.n_parameters; ++i) {
                out_put(self, ", ");
                a_declaration(self, let_node->let.parameters[i]->type, null,
                              ast_node_name_string(let_node->let.parameters[i]));
            }
            out_put(self, ")");

            out_put(self, ";\n");
        }
    }

    else if (ast_lambda_function == node->tag) {
        struct ast_lambda_function *v           = ast_node_lf(node);
        u64                         hash        = ast_node_hash(node);
        char const                 *struct_name = make_thunk_struct_name(self->transient, hash);
        char const                 *thunk_name  = make_thunk_name(self->transient, hash);

        // We use this map for lambdas only here, to avoid duplicate
        // declarations. The caller erases the map after the pass is
        // finished.
        if (map_contains(self->apply_contexts, &hash, sizeof hash)) return;
        map_set(&self->apply_contexts, &hash, sizeof hash, &hash);

        out_put_start_fmt(self, "struct %s;\n", struct_name);
        out_put_start(self, "static ");
        assert(type_arrow == node->type->tag);
        a_declaration_void_ok(self, node->type->arrow.right, node, thunk_name);
        out_put(self, " ");

        // params
        out_put_fmt(self, "(struct %s * tl_ctx", struct_name);
        for (u32 i = 0; i < v->n_parameters; ++i) {
            out_put(self, ", ");
            a_declaration(self, v->parameters[i]->type, null, ast_node_name_string(v->parameters[i]));
        }
        out_put(self, ")");
        out_put(self, ";\n");
    }
}

static void look_for_thunks(void *ctx_, ast_node *node) {
    generate_thunks_ctx *ctx  = ctx_;
    transpiler          *self = ctx->self;

    if (ast_if_then_else == node->tag) {
        log(self, "thunk: if-then-else: %s", ast_node_to_string(self->strings, node));
        make_one_thunk(ctx, node->if_then_else.yes);
        make_one_thunk(ctx, node->if_then_else.no);
    } else if (ast_named_function_application == node->tag) {
        struct ast_named_application const *v = ast_node_named(node);

        if (v->free_variables.size) {
            log(self, "thunk: named-application requires free %u variables: '%s'", v->free_variables.size,
                ast_node_to_string(self->strings, node->named_application.name));

            u64 hash = hash_name_and_vars(node);
            if (map_contains(self->apply_contexts, &hash, sizeof hash)) return;
            map_set(&self->apply_contexts, &hash, sizeof hash, &hash);

            char const      *function_name = ast_node_name_string(v->name);
            char const      *struct_name   = make_thunk_struct_name(self->transient, hash);
            char const      *thunk_name    = make_thunk_name(self->transient, hash);
            ast_node const **found_let_node =
              map_get(self->functions, function_name, strlen(function_name));
            if (!found_let_node) fatal("function not found: '%s'", function_name);
            ast_node const *let_node = *found_let_node;
            assert(ast_let == let_node->tag);

            emit_thunk_struct(self, struct_name, v->free_variables);

            // return type and name
            out_put_start(self, "static ");
            a_declaration_void_ok(self, node->type, node, thunk_name);
            out_put(self, " ");

            // params
            out_put_fmt(self, "(struct %s * tl_ctx", struct_name);

            for (u32 i = 0; i < let_node->let.n_parameters; ++i) {
                out_put(self, ", ");
                a_declaration(self, let_node->let.parameters[i]->type, null,
                              ast_node_name_string(let_node->let.parameters[i]));
            }
            out_put(self, ")");

            // body
            out_put(self, " {\n");
            self->indent_level++;

            // add my free variables to the stack
            u32 save = push_free_variables_ext(self, v->free_variables, null);
            a_thunk(self, let_node->let.body);
            pop_free_variables(self, save);

            char const *body = pop_result(self);
            out_put_start_fmt(self, "return %s;", body);

            self->indent_level--;
            out_put(self, "\n}\n\n");
        }
    }
}

static void generate_thunks(transpiler *self, ast_node **nodes, u32 n) {
    generate_thunks_ctx ctx;
    ctx.self = self;
    ctx.map  = map_create(self->transient, sizeof(int));

    for (u32 i = 0; i < n; i++) {
        ast_node_dfs_safe_for_recur(self->transient, &ctx, nodes[i], forward_declare_thunks);
    }

    out_put(self, "\n\n");

    map_destroy(&self->apply_contexts);
    self->apply_contexts = map_create(self->transient, sizeof(u64));

    for (u32 i = 0; i < n; i++) {
        ast_node *node = nodes[i];
        ast_node_dfs_safe_for_recur(self->transient, &ctx, node, look_for_thunks);

        // lambdas are all promoted to toplevel nodes by ti_inferer
        if (ast_lambda_function == node->tag) {
            log(self, "thunk: lambda-function: %s", ast_node_to_string(self->strings, node));
            make_lambda_thunk(&ctx, node);
        }
    }
    map_destroy(&ctx.map);
}

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
    case type_nil:
    case type_ellipsis:       return 1;

    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_user:
    case type_type_var:
    case type_pointer:
    case type_any:            return 0;

    case type_tuple:
    case type_labelled_tuple: return type->array.elements.size == 0;

    case type_arrow:          {
        tl_type *right = type->arrow.right;
        if (right->tag == type_nil) return 1;
        if (right->tag == type_tuple && right->array.elements.size == 0) return 1;
        return 0;
    }
    }
}

static int a_declaration_impl(transpiler *self, tl_type const *type, ast_node const *node, char const *var,
                              char const *void_decl) {

    if (type->tag != type_arrow) {
        if (is_nil_result(type)) {
            out_put_fmt(self, "%s %s", void_decl, var);
        } else {
            a_result_type_of(self, type);
            out_put_fmt(self, " %s", var);
        }
    } else {
        // make a function pointer type
        tl_type *left = type->arrow.left;

        a_declaration_void_ok(self, type->arrow.right, null, "");
        out_put_fmt(self, " (*%s) (", var);

        if (node && BIT_TEST(type->arrow.flags, TL_TYPE_ARROW_LAMBDA)) {
            // for a thunk, need to add the context struct pointer here
            assert(ast_lambda_function == node->tag);
            u64   hash        = ast_node_hash(node);
            char *struct_name = make_thunk_struct_name(self->strings, hash);

            out_put_fmt(self, "struct %s * tl_ctx", struct_name);

            for (u32 i = 0; i < left->array.elements.size; ++i) {
                out_put(self, ", ");
                a_declaration(self, left->array.elements.v[i], null, "");
            }

        } else {
            for (u32 i = 0; i < left->array.elements.size; ++i) {
                a_declaration(self, left->array.elements.v[i], null, "");
                if (i < left->array.elements.size - 1) out_put(self, ", ");
            }
        }

        out_put(self, ") ");
    }
    return 0;
}

static int a_declaration(transpiler *self, tl_type const *type, ast_node const *node, char const *var) {
    return a_declaration_impl(self, type, node, var, "/* void */ int");
}

static int a_declaration_void_ok(transpiler *self, tl_type const *type, ast_node const *node,
                                 char const *var) {
    return a_declaration_impl(self, type, node, var, "void");
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

    case type_any:
    case type_ellipsis: out_put(self, "void"); break;

    case type_arrow:    a_result_type_of(self, ty->arrow.right); break;

    case type_user:     {
        out_put_fmt(self, "struct %s", ty->user.name);

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
        a_declaration(self, ty, null, field_name);
        out_put(self, ";\n");
    }
    self->indent_level--;

    out_put_start(self, "};\n");

    return 0;
}

static int a_toplevel(transpiler *self, ast_node const *node) {
    if (ast_let == node->tag) return a_let(self, node);
    return 0;
}

static int a_let_in(transpiler *self, ast_node const *node) {

    // let a = 1 in a + 2 end => resN = 3

    char const *name = ast_node_name_string(node->let_in.name);

    if (a_eval(self, node->let_in.value)) return 1;
    char const *value = pop_result(self);

    // if value is a lambda function, add it to the current context
    if (ast_lambda_function == node->let_in.value->tag) {
        map_set(&self->lambdas, name, strlen(name), &node->let_in.value);
    }

    out_put_start(self, "");
    a_declaration(self, node->let_in.name->type, node->let_in.value, name); // give value to declaration
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
        char const            *field_name = ast_node_name_original(ass->value);

        out_put_start(self, "");
        a_declaration(self, ass->value->type, ass->value, var_name);
        out_put_start_fmt(self, " = %s.%s;\n", value, field_name);
    }

    // eval the body and leave it on the stack
    if (a_eval(self, v->body)) return 1;

    return 0;
}

static int a_thunk(transpiler *self, ast_node const *node) {
    // a function which evaluates itself and returns its value, used
    // to defer evaluation such as with short-circuit conditionals.

    self->is_eval_in_thunk = 1;
    int res                = a_eval(self, node);
    self->is_eval_in_thunk = 0;
    return res;
}

static int a_if_then_else(transpiler *self, ast_node const *node) {
    // if cond then yes else no
    struct ast_if_then_else *v = ast_node_ifthen((ast_node *)node);

    // get the thunk names for each arm
    u64   yes_hash = ast_node_hash(v->yes);
    u64   no_hash  = ast_node_hash(v->no);
    char *yes      = make_thunk_name(self->strings, yes_hash);
    char *yes_ctx  = make_thunk_struct_name(self->strings, yes_hash);
    char *no       = make_thunk_name(self->strings, no_hash);
    char *no_ctx   = make_thunk_struct_name(self->strings, no_hash);

    // get the free variables in each arm
    ast_node_sized yes_free = ti_free_variables_in(self->transient, v->yes);
    ast_node_sized no_free  = ti_free_variables_in(self->transient, v->no);

    // make the context structs
    emit_thunk_struct_init(self, yes_ctx, "tl_ctx_yes_", yes_free);
    emit_thunk_struct_init(self, no_ctx, "tl_ctx_no_", no_free);

    // eval the condition
    if (a_eval(self, v->condition)) return 1;
    char const *condition = pop_result(self);

    // dispatch to thunks
    char *var = next_variable(self);
    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");
    out_put_start_fmt(self, "if (%s) %s = %s(&tl_ctx_yes_); else %s = %s(&(tl_ctx_no_));\n", condition, var,
                      yes, var, no);
    return 0;
}

static int a_field_access(transpiler *self, ast_node const *node) {

    struct ast_user_type_get const *v   = ast_node_utg((ast_node *)node);

    char                           *var = next_variable(self);

    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    if (type_pointer == v->struct_name->type->tag)
        out_put_start_fmt(self, " = %s->%s;\n", emit_symbol(self, v->struct_name),
                          ast_node_name_string(v->field_name));
    else
        out_put_start_fmt(self, " = %s.%s;\n", emit_symbol(self, v->struct_name),
                          ast_node_name_string(v->field_name));

    return 0;
}

static int a_field_setter(transpiler *self, ast_node const *node) {

    struct ast_user_type_set const *v          = ast_node_uts((ast_node *)node);
    char const                     *field_name = ast_node_name_string(v->field_name);
    char                           *var        = next_variable(self);

    // eval the value
    if (a_eval(self, v->value)) return 1;
    char const *value = pop_result(self);

    // output value is field set value
    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put_start_fmt(self, " = %s;\n", value);

    // assign to struct field
    if (type_pointer == v->struct_name->type->tag)
        out_put_start_fmt(self, "%s->%s = %s;\n", emit_symbol(self, v->struct_name), field_name, var);
    else out_put_start_fmt(self, "%s.%s = %s;\n", emit_symbol(self, v->struct_name), field_name, var);

    return 0;
}

static char *emit_symbol(transpiler *self, ast_node const *node) {
    // emit the symbol name, respecting thunk context and function name mangling
    char const *name = ast_node_name_string(node);

    if (type_arrow == node->type->tag) {
        return make_function_name(self->strings, name);
    } else {
        if (!self->is_eval_in_thunk || !array_contains(self->thunk_free_variables, &name))
            return alloc_strdup(self->transient, name);
        else {
            int   len = snprintf(null, 0, "(*(tl_ctx->%s))", name) + 1;
            char *out = alloc_malloc(self->transient, len);
            snprintf(out, len, "(*(tl_ctx->%s))", name);
            return out;
        }
    }
}

static int a_eval(transpiler *self, ast_node const *node) {

    if (!node || !node->type) fatal("a_eval: node or type is null");

    char *var = next_variable(self);

    out_put(self, "\n");

    out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->strings, node));

    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");

    if (is_nil_result(node->type)) var = null; // abandon the result var after we declare it

    switch (node->tag) {
    case ast_assignment:
    case ast_arrow:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
        if (var) out_put_start_fmt(self, "%s = NULL;\n", var);
        break;
    case ast_symbol:
        if (var) out_put_start_fmt(self, "%s = %s;\n", var, emit_symbol(self, node));
        break;

    case ast_string:
        if (var) out_put_start_fmt(self, "%s = \"%s\";\n", var, ast_node_name_string(node));
        break;
    case ast_i64:
        if (var) out_put_start_fmt(self, "%s = %" PRIi64 ";\n", var, node->i64.val);
        break;
    case ast_u64:
        if (var) out_put_start_fmt(self, "%s = %" PRIu64 ";\n", var, node->u64.val);
        break;
    case ast_f64:
        if (var) out_put_start_fmt(self, "%s = %f;\n", var, node->f64.val);
        break;
    case ast_bool:
        if (var) {
            if (node->bool_.val) out_put_start_fmt(self, "%s = 1;\n", var);
            else out_put_start_fmt(self, "%s = 0;\n", var);
        }
        break;

    case ast_address_of: {
        if (ast_symbol == node->address_of.target->tag) {
            log(self, "taking address of '%s'", ast_node_to_string(self->strings, node->address_of.target));
            if (var)
                out_put_start_fmt(self, "%s = &(%s);\n", var, emit_symbol(self, node->address_of.target));
        } else {
            if (a_eval(self, node->address_of.target)) return 1;
            char const *res = pop_result(self);
            if (var) out_put_start_fmt(self, "%s = &(%s);\n", var, res);
        }
    } break;

    case ast_dereference: {
        if (a_eval(self, node->dereference.target)) return 1;
        char const *ptr = pop_result(self);
        if (var) out_put_start_fmt(self, "%s = *(%s);\n", var, ptr);
    } break;

    case ast_dereference_assign: {
        if (a_eval(self, node->dereference_assign.target)) return 1;
        char const *ptr = pop_result(self);
        if (a_eval(self, node->dereference_assign.value)) return 1;
        char const *value = pop_result(self);

        out_put_start_fmt(self, "*(%s) = %s;\n", ptr, value);
        if (var) out_put_start_fmt(self, "%s = %s;\n", var, value);
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
            if (var) out_put_start_fmt(self, "%s.%s = %s;\n", var, lt->names.v[i], res);
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
        if (a_if_then_else(self, node)) return 1;
        pop_and_assign(self, var);
        break;

    case ast_lambda_function: {

        u64   hash = ast_node_hash(node);
        char *name = make_thunk_name(self->strings, hash);
        if (var) out_put_start_fmt(self, "%s = %s;\n", var, name);

    } break;

    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application: break;

    case ast_named_function_application:  {
        if (BIT_TEST(node->named_application.flags, AST_NAMED_APP_INTRINSIC)) {
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

    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_if_then_else:
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
        out_put_fmt(self, "FIXME:expand /* %s */", ast_node_to_string(self->transient, node));
        break;
    }
    return 0;
}

static int tl_unary_op(transpiler *self, ast_node const *node, void *op) {
    struct ast_named_application *v    = ast_node_named((ast_node *)node);
    char const                   *name = ast_node_name_string(v->name);
    if (v->n_arguments != 1) fatal("wrong number of arguments: '%s'", name);

    // function call result
    char *var = next_variable(self);
    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");

    // eval the args
    a_eval(self, v->arguments[0]);
    char const *arg = pop_result(self);

    out_put_start_fmt(self, "%s = %s(%s);\n", var, (char const *)op, arg);
    return 0;
}

static int tl_binary_op(transpiler *self, ast_node const *node, void *op) {
    struct ast_named_application *v    = ast_node_named((ast_node *)node);
    char const                   *name = ast_node_name_string(v->name);
    if (v->n_arguments != 2) fatal("wrong number of arguments: '%s'", name);

    // function call result
    char *var = next_variable(self);
    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");

    // eval the args
    a_eval(self, v->arguments[1]);
    a_eval(self, v->arguments[0]);
    char const *lhs = pop_result(self);
    char const *rhs = pop_result(self);

    out_put_start_fmt(self, "%s = %s %s %s;\n", var, lhs, (char const *)op, rhs);
    return 0;
}

static int tl_sizeof(transpiler *self, ast_node const *node, void *extra) {
    (void)extra;
    struct ast_named_application *v    = ast_node_named((ast_node *)node);
    char const                   *name = ast_node_name_string(v->name);
    if (v->n_arguments != 1) fatal("wrong number of arguments: '%s'", name);

    // function call result
    char *var = next_variable(self);
    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");

    out_put_start_fmt(self, "%s = (sizeof (", var);
    expand_value(self, v->arguments[0]);
    out_put(self, "));\n");
    return 0;
}

static int tl_sizeoft(transpiler *self, ast_node const *node, void *extra) {
    (void)extra;
    struct ast_named_application *v    = ast_node_named((ast_node *)node);
    char const                   *name = ast_node_name_string(v->name);
    if (v->n_arguments != 1) fatal("wrong number of arguments: '%s'", name);

    char *var = next_variable(self);
    out_put_start(self, "");
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");

    // Emit the type of the argument
    out_put_start_fmt(self, "%s = (sizeof (", var);
    a_result_type_of(self, v->arguments[0]->type);
    out_put(self, "));\n");
    return 0;
}

static int tl_and(transpiler *self, ast_node const *node, void *extra) {
    (void)extra;
    struct ast_named_application *v = ast_node_named((ast_node *)node);

    //
    char *result = next_variable(self);
    char  done_label[64];
    snprintf(done_label, sizeof done_label, "%s_done", result);
    out_put_start_fmt(self, "int %s = 0;\n", result);
    out_put_start(self, "{\n");
    self->indent_level++;

    for (u32 i = 0; i < v->n_arguments; ++i) {
        // if ! arg goto done
        a_eval(self, v->arguments[i]);
        char const *arg = pop_result(self);
        out_put_start_fmt(self, "if (!(%s)) goto %s;\n", arg, done_label);
    }

    // all args are true
    out_put(self, "\n");
    out_put_start_fmt(self, "%s = 1;\n", result);

    self->indent_level--;
    out_put(self, "\n");
    out_put_start_fmt(self, "%s:;", done_label);
    out_put(self, "\n");
    out_put_start(self, "}\n");
    return 0;
}

static int tl_or(transpiler *self, ast_node const *node, void *extra) {
    (void)extra;
    struct ast_named_application *v = ast_node_named((ast_node *)node);

    //
    char *result = next_variable(self);
    char  done_label[64];
    snprintf(done_label, sizeof done_label, "%s_done", result);
    out_put_start_fmt(self, "int %s = 1;\n", result);
    out_put_start(self, "{\n");
    self->indent_level++;

    for (u32 i = 0; i < v->n_arguments; ++i) {
        // if arg goto done
        a_eval(self, v->arguments[i]);
        char const *arg = pop_result(self);
        out_put_start_fmt(self, "if (%s) goto %s;\n", arg, done_label);
    }

    // none of the args are true
    out_put(self, "\n");
    out_put_start_fmt(self, "%s = 0;\n", result);

    self->indent_level--;
    out_put(self, "\n");
    out_put_start_fmt(self, "%s:;", done_label);
    out_put(self, "\n");
    out_put_start(self, "}\n");
    return 0;
}

static int a_intrinsic_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);
    struct ast_named_application *v    = ast_node_named((ast_node *)node);
    char const                   *name = ast_node_name_string(v->name);

    struct dispatch {
        char const *name;
        int (*fun)(transpiler *, ast_node const *, void *extra);
        void *extra;
    };

    static const struct dispatch table[] = {
      {"_tl_sizeof_", tl_sizeof, null},
      {"_tl_sizeoft_", tl_sizeoft, null},

      {"_tl_add_", tl_binary_op, "+"},
      {"_tl_sub_", tl_binary_op, "-"},
      {"_tl_mod_", tl_binary_op, "%"},
      {"_tl_mul_", tl_binary_op, "*"},
      {"_tl_div_", tl_binary_op, "/"},

      {"_tl_lt_", tl_binary_op, "<"},
      {"_tl_lte_", tl_binary_op, "<="},
      {"_tl_eq_", tl_binary_op, "=="},
      {"_tl_neq_", tl_binary_op, "!="},
      {"_tl_gte_", tl_binary_op, ">="},
      {"_tl_gt_", tl_binary_op, ">"},

      {"_tl_and_", tl_and, null},
      {"_tl_or_", tl_or, null},

      {"_tl_band_", tl_binary_op, "&"},
      {"_tl_bor_", tl_binary_op, "|"},
      {"_tl_bxor_", tl_binary_op, "^"},

      {"_tl_bsl_", tl_binary_op, "<<"},
      {"_tl_bsr_", tl_binary_op, ">>"},

      {"_tl_bcomp_", tl_unary_op, "~"},

      {"", null, null},
    };

    struct dispatch const *p = table;
    for (; p && p->name[0]; ++p)
        if (0 == strcmp(p->name, name)) return p->fun(self, node, p->extra);

    fatal("unknown intrinsic: '%s'", name);

    return 1;
}

static int a_fun_apply(transpiler *self, ast_node const *node) {
    assert(ast_named_function_application == node->tag);

    struct ast_named_application const *v              = ast_node_named((ast_node *)node);
    char const                         *name           = ast_node_name_string(v->name);
    ast_node_sized                      free_variables = v->free_variables;

    if (0 == strncmp("c_", name, 2)) {
        name += 2;
    } else if (0 == strncmp("_", name, 1)) {
        ;
    } else if (0 == strncmp("std_", name, 4)) {
        ;
    } else {
        name = make_function_name(self->strings, name);
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
    a_declaration(self, node->type, node, var);
    out_put(self, ";\n");

    // function call
    tl_type   *fun_type = node->named_application.name->type;

    ast_node **found    = map_get(self->lambdas, name, strlen(name));
    if (found) {
        ast_node *lambda = *found;
        assert(ast_lambda_function == lambda->tag);

        u64         hash        = ast_node_hash(lambda);
        char const *struct_name = make_thunk_struct_name(self->strings, hash);

        out_put_start(self, "{\n");
        self->indent_level++;

        // free variables
        out_put_start(self, "/* parent free variables: ");
        forall(i, free_variables) {
            out_put_fmt(self, "%s, ", ast_node_name_string(free_variables.v[i]));
        }
        out_put(self, " */\n");

        out_put_start(self, "/* lambda free variables: ");
        forall(i, lambda->lambda_function.free_variables) {
            out_put_fmt(self, "%s, ", ast_node_name_string(lambda->lambda_function.free_variables.v[i]));
        }
        out_put(self, " */\n");

        emit_thunk_struct_init(self, struct_name, "tl_apply_ctx_", lambda->lambda_function.free_variables);

        if (is_nil_result(fun_type)) out_put_start(self, "");
        else out_put_start_fmt(self, "%s = ", var);

        out_put_fmt(self, "%s(&tl_apply_ctx_, ", name);

        for (i32 i = 0; i < n_args; ++i) {
            char const *arg = pop_result(self);
            out_put(self, arg);
            if (i < n_args - 1) out_put(self, ", ");
        }
        out_put(self, ");\n");

        if (is_nil_result(fun_type)) out_put_start_fmt(self, "%s = 0;/* void */\n", var);

        self->indent_level--;
        out_put_start(self, "}\n");
        return 0;
    }

    // apply with context
    u64 hash = hash_name_and_vars(node);
    if (map_contains(self->apply_contexts, &hash, sizeof hash)) {
        char const *struct_name = make_thunk_struct_name(self->transient, hash);
        char const *thunk_name  = make_thunk_name(self->transient, hash);

        out_put_start(self, "{\n");
        self->indent_level++;

        // free variables
        out_put_start(self, "/* parent free variables: ");
        forall(i, free_variables) {
            out_put_fmt(self, "%s, ", ast_node_name_string(free_variables.v[i]));
        }
        out_put(self, " */\n");

        out_put_start(self, "/* lambda free variables: ");
        forall(i, v->free_variables) {
            out_put_fmt(self, "%s, ", ast_node_name_string(v->free_variables.v[i]));
        }
        out_put(self, " */\n");

        emit_thunk_struct_init(self, struct_name, "tl_apply_ctx_", v->free_variables);

        if (is_nil_result(fun_type)) out_put_start(self, "");
        else out_put_start_fmt(self, "%s = ", var);

        out_put_fmt(self, "%s(&tl_apply_ctx_, ", thunk_name);

        for (i32 i = 0; i < n_args; ++i) {
            char const *arg = pop_result(self);
            out_put(self, arg);
            if (i < n_args - 1) out_put(self, ", ");
        }
        out_put(self, ");\n");

        if (is_nil_result(fun_type)) out_put_start_fmt(self, "%s = 0;/* void */\n", var);

        self->indent_level--;
        out_put_start(self, "}\n");
        return 0;

    } else {

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
    }

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
    a_declaration(self, node->type, node, var);
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
    a_declaration(self, node->type, node, var);
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
    if (ast_tuple == node->tag && BIT_TEST(node->tuple.flags, AST_TUPLE_FLAG_INIT)) {
        return a_tuple_init(self, node);
    }
    if (ast_labelled_tuple == node->tag && BIT_TEST(node->labelled_tuple.flags, AST_TUPLE_FLAG_INIT)) {
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
    a_declaration(self, node->type, node, var);
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
#define fmt "tl_gen_struct_tup_%" PRIu64 "_"
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
#define fmt "tl_gen_make_tup_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static char *make_thunk_name(allocator *alloc, u64 hash) {
    char *name = null;
    {
#define fmt "tl_gen_thunk_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static char *make_thunk_struct_name(allocator *alloc, u64 hash) {
    char *name = null;
    {
#define fmt "tl_gen_thunk_struct_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static char *make_function_name(allocator *alloc, char const *base) {
    char *name = null;
    {
#define fmt "tl_fun_%s"
        int len = snprintf(null, 0, fmt, base) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, base);
#undef fmt
    }

    return name;
}

static int a_let_struct_phase(transpiler *self, ast_node const *node) {
    // Only process tuple constructor functions
    if (!ast_node_is_tuple_constructor(node)) return 0;

    struct ast_let const *v    = ast_node_let((ast_node *)node);

    char const           *name = string_t_str(&v->name->symbol.name);
    // if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    log(self, "processing struct let '%s'...", name);

    ti_function_record *rec = ti_lookup_function(self->type_inferer, name);
    if (!rec) fatal("function record not found: '%s'", name);
    tl_type *tuple          = rec->type->arrow.left;
    u64      hash           = tl_type_hash(tuple);
    char    *generated_name = make_struct_name(self->alloc, hash);

    // TODO since tuple types are not named, the program doesn't know
    // it already has a tuple matching a particular type signature.
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
            a_declaration(self, tup->elements.v[i], null, buf);
            out_put(self, ";\n");
        }
    } else if (type_labelled_tuple == tuple->tag) {
        struct tlt_labelled_tuple *lt = tl_type_lt(tuple);

        for (u32 i = 0; i < lt->fields.size; ++i) {

            out_put_start(self, "");
            a_declaration(self, lt->fields.v[i], null, lt->names.v[i]);
            out_put(self, ";\n");
        }

    } else {
        fatal("expected tuple type");
    }

    self->indent_level--;
    out_put_start(self, "};\n");

    // function declaration
    // TODO copied from a_let
    name = string_t_str(&v->name->symbol.name);
    if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    // return type and name
    out_put_start(self, "static ");
    a_declaration(self, rec->type->arrow.right, node, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        a_declaration(self, v->parameters[i]->type, null, ast_node_name_string(v->parameters[i]));
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

    // don't emit generic prototypes: they are generic because they
    // are not used by the program.
    if (is_generic_function(self, node)) return 0;

    if (BIT_TEST(node->let.flags, AST_LET_FLAG_INTRINSIC)) return 0;

    struct ast_let const *v    = ast_node_let((ast_node *)node);
    char const           *name = string_t_str(&v->name->symbol.name);
    // if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);
    assert(strlen(name));

    if (0 == string_t_cmp_c(&v->name->symbol.name, "main")) {
        // skip here, let a_main process it.
        return 0;
    }

    ti_function_record *rec = ti_lookup_function(self->type_inferer, name);
    if (!rec) fatal("function record not found: '%s'", name);

    // mangle the name
    name = make_function_name(self->strings, name);

    // function declaration

    // return type and name
    out_put_start(self, "static ");
    a_declaration_void_ok(self, rec->type->arrow.right, node, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        a_declaration(self, v->parameters[i]->type, null, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ");\n");

    return 0;
}

static int a_let(transpiler *self, ast_node const *node) {
    map_reset(self->lambdas);

    if (BIT_TEST(node->let.flags, AST_LET_FLAG_INTRINSIC)) {
        log(self, "skipping '%s' because it is an intrinsic function",
            ast_node_name_string(node->let.name));
        return 0;
    }

    struct ast_let const *v    = ast_node_let((ast_node *)node);
    char const           *name = string_t_str(&v->name->symbol.name);
    // if (0 == strlen(name)) name = string_t_str(&v->name->symbol.name);

    if (ast_node_is_tuple_constructor(node)) return 0; // handled by a_let_struct_phase

    if (0 == string_t_cmp_c(&v->name->symbol.name, "main")) {
        // skip here, let a_main process it.
        return 0;
    }

    if (is_generic_function(self, node)) {
        fatal("cannot process generic function '%s' ", string_t_str(&v->name->symbol.name));
    }

    log(self, "processing '%s'...", name);

    ti_function_record *rec = ti_lookup_function(self->type_inferer, name);
    if (!rec) fatal("function record not found: '%s'", name);

    // mangle the name
    name = make_function_name(self->strings, name);

    // function declaration

    // return type and name
    out_put_start(self, "static ");
    a_declaration_void_ok(self, rec->type->arrow.right, node, name);
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        a_declaration(self, v->parameters[i]->type, null, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    if (a_eval(self, v->body)) return 1;

    char const *body = pop_result(self);
    if (!is_nil_result(rec->type->arrow.right)) {
        out_put_start_fmt(self, "return %s;", body);
    }

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

static int is_generic_function(transpiler *self, ast_node const *node) {
    if (ast_let != node->tag) return 0;

    ti_function_record *rec = ti_lookup_function(self->type_inferer, ast_node_name_string(node->let.name));
    if (!rec) fatal("function record not found: '%s'", ast_node_name_string(node->let.name));

    tl_type *arrow = rec->type;
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
    if (var) out_put_start_fmt(self, "%s = %s;\n", var, res);
}

static u32 push_free_variables_ext(transpiler *self, ast_node_sized free_variables, u32 *count) {
    u32 save = self->thunk_free_variables.size;

    forall(i, free_variables) {
        array_push_val(self->thunk_free_variables, ast_node_name_string(free_variables.v[i]));
    }

    if (count) *count = self->thunk_free_variables.size - save;
    return save;
}

static u32 push_free_variables(transpiler *self, ast_node const *node, u32 *count) {
    return push_free_variables_ext(self, ti_free_variables_in(self->transient, node), count);
}

static void pop_free_variables(transpiler *self, u32 save) {
    self->thunk_free_variables.size = save;
}

static u64 hash_name_and_vars(ast_node const *node) {
    assert(ast_named_function_application == node->tag);
    struct ast_named_application const *v    = ast_node_named((ast_node *)node);
    char const                         *name = ast_node_name_string(v->name);
    u64                                 hash = hash64((void *)name, strlen(name));

    forall(i, v->free_variables) {
        ast_node const *arg = v->free_variables.v[i];
        assert(ast_symbol == arg->tag);
        char const *str = ast_node_name_string(arg);
        hash            = hash64_combine(hash, (void *)str, strlen(str));
    }

    return hash;
}
