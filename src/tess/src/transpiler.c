#include "transpiler.h"
#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "hashmap.h"

#include "ast_tags.h"
#include "dbg.h"
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

typedef struct {
    char const *free_variable;
    char const *struct_name;
} free_variable_context_name;

typedef struct {
    array_header;
    free_variable_context_name *v;
} free_variable_context_name_array;

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
    int                              is_eval_in_thunk;
    free_variable_context_name_array thunk_free_variables;
    hashmap                         *functions; // char const* -> ast_node const* (let)
    // the let nodes in the program

    hashmap *lambdas; // char const* -> ast_node const*
    // named lambda hash of its ast_lamba_function is used as key for its thunk

    hashmap *apply_contexts; // u64 -> u64 (hash apply name + freevars)
    // named_function_applications which require context, hash is the name + the freevars
};

// -- embed externs --

extern char const *embed_std_c;

// -- static forwards --

typedef int (*compile_fun_t)(transpiler *, ast_node const *);

static int   a_eval(transpiler *, ast_node const *);
static int   a_if_then_else(transpiler *, ast_node const *);
static int   a_field_setter(transpiler *, ast_node const *);
static int   a_field_access(transpiler *, ast_node const *);
static int   a_intrinsic_apply(transpiler *, ast_node const *);
static int   a_fun_apply(transpiler *, ast_node const *);
static int   a_let(transpiler *, ast_node const *);
static int   a_let_in(transpiler *, ast_node const *);
static int   a_let_match_in(transpiler *, ast_node const *);
static int   a_let_struct_phase(transpiler *, ast_node const *);
static int   a_main(transpiler *, ast_node const *);
static int   a_result_type_of(transpiler *, tl_type const *);
static int   a_toplevel(transpiler *, ast_node const *);
static int   a_thunk(transpiler *, ast_node const *);
static int   a_tuple_cons(transpiler *, ast_node const *);
static int   a_user_type_definition(transpiler *, ast_node const *);

static char *next_variable(transpiler *);
static char *make_context_struct_name(allocator *alloc, tl_type *type);
static char *make_tuple_struct_name(allocator *, u64);
static char *make_tuple_struct_constructor_name(allocator *, u64);
static char *make_thunk_name(allocator *, u64);
static char *make_thunk_struct_name(allocator *, u64);
// static char       *make_let_struct_name(allocator *, ast_node const *);
static char       *make_lambda_struct_name(allocator *, u64);
static char       *make_lambda_function_name(allocator *, u64);
static char       *make_function_name(allocator *, char const *);
static char       *emit_symbol_use(transpiler *, ast_node const *);
static char const *emit_symbol_declaration(transpiler *, ast_node const *, char const *, int);
static char const *emit_type(transpiler *, tl_type *, char const *);
static char const *emit_type_arrow_ok(transpiler *, tl_type *, char const *, char const *);
static char const *emit_type_declaration(transpiler *, tl_type *, char const *, int);
static void        emit_thunk_struct(transpiler *, char const *, tl_free_variable_sized);
static void        emit_thunk_struct_init(transpiler *, char const *, char const *, tl_free_variable_sized);

static int         generate_toplevel_contexts(transpiler *, ast_node_sized);
static int         generate_toplevel_lambdas(transpiler *, ast_node_sized);
static int         generate_toplevel_prototypes(transpiler *, ast_node_sized);
static void        generate_thunks(transpiler *, ast_node **, u32);
static u32         push_free_variables(transpiler *, ast_node const *, char const *, u32 *);
static u32         push_free_variables_ext(transpiler *, tl_free_variable_sized, char const *, u32 *);
static void        pop_free_variables(transpiler *, u32);
static u64         transpiler_type_hash(tl_type const *);
static ti_function_record *lookup_function(transpiler *, char const *);
static void                collect_function_records(transpiler *, ast_node **, u32);

static char const         *pop_result(transpiler *);
static void                push_result(transpiler *, char const *);
static void                pop_and_assign(transpiler *, char const *);

static void                out_put_start(transpiler *, char const *);
static void                out_put(transpiler *, char const *);
static void                out_put_start_fmt(transpiler *, char const *restrict, ...)
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

    self->thunk_free_variables = (free_variable_context_name_array){.alloc = self->transient};
    self->functions            = map_create(self->transient, sizeof(ti_function_record));
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
    log(self, "-- transpiler start -- program nodes follow --");
    log(self, "");
    collect_function_records(self, nodes, n);
    log(self, "");

    // output std header
    out_put(self, embed_std_c);
    out_put(self, "\n\n");

    out_put_start(self, "\n// -- begin user types -- \n\n");

    for (size_t i = 0; i < n; ++i) {
        ast_node *node = nodes[i];
        if (ast_user_type_definition == node->tag) a_user_type_definition(self, node);
    }

    out_put_start(self, "\n// -- end user types -- \n\n");

    // output generated tuple structs
    out_put_start(self, "\n// -- begin tuple structs -- \n\n");

    for (size_t i = 0; i < n; ++i) {
        int res = 0;
        if (ast_let == nodes[i]->tag)
            if ((res = a_let_struct_phase(self, nodes[i]))) return res;
    }

    out_put_start(self, "\n// -- end tuple structs -- \n\n");

    out_put_start(self, "\n// -- begin function context structs -- \n\n");
    generate_toplevel_contexts(self, (ast_node_sized){.size = n, .v = nodes});
    out_put_start(self, "\n// -- end function context structs -- \n\n");

    // output all function prototypes
    out_put_start(self, "\n// -- begin prototypes -- \n\n");
    generate_toplevel_prototypes(self, (ast_node_sized){.size = n, .v = nodes});
    out_put_start(self, "\n// -- end prototypes -- \n\n");

    // output all anonymous lambda function bodies and context structs
    out_put_start(self, "\n// -- begin anonymous lambda functions -- \n\n");
    generate_toplevel_lambdas(self, (ast_node_sized){.size = n, .v = nodes});
    out_put_start(self, "\n// -- end anonymous lambda functions -- \n\n");

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
    hashmap    *seen; // u64
} generate_thunks_ctx;

static void emit_thunk_struct_init(transpiler *self, char const *struct_name, char const *var_name,
                                   tl_free_variable_sized variables) {

    if (!variables.size) return;

    out_put_start_fmt(self, "struct %s %s = {\n", struct_name, var_name);
    self->indent_level++;
    {
        ast_node *ptr             = ast_node_create(self->transient, ast_address_of);
        ptr->address_of.target    = null;
        ptr->type                 = tl_type_create(self->transient, type_pointer);
        ptr->type->pointer.target = null;

        hashmap *seen             = hset_create(self->transient);

        for (u32 i = 0; i < variables.size; ++i) {
            char const *name_str = string_t_str(&variables.v[i].name);
            if (hset_contains(seen, name_str, strlen(name_str))) continue;
            hset_insert(&seen, name_str, strlen(name_str));

            ptr->address_of.target =
              ast_node_create_sym(self->transient, string_t_str(&variables.v[i].name));

            ptr->address_of.target->type = variables.v[i].type;
            ptr->type->pointer.target    = variables.v[i].type;

            out_put_start_fmt(self, ".%s = &(%s),\n", name_str,
                              emit_symbol_use(self, ptr->address_of.target));
        }

        if (!variables.size) out_put_start(self, ".empty = '\\0',");
    }
    self->indent_level--;
    out_put_start(self, "};\n\n");
}

static void emit_thunk_struct(transpiler *self, char const *name, tl_free_variable_sized variables) {
    // used for both thunks (if-then-else) and function contexts

    if (!variables.size) return;

    out_put_start_fmt(self, "struct %s {\n", name);
    self->indent_level++;
    {
        ast_node *ptr             = ast_node_create(self->transient, ast_address_of);
        ptr->address_of.target    = null;
        ptr->type                 = tl_type_create(self->transient, type_pointer);
        ptr->type->pointer.target = null;

        hashmap *seen             = hset_create(self->transient);
        for (u32 i = 0; i < variables.size; ++i) {
            char const *name_str = string_t_str(&variables.v[i].name);
            if (hset_contains(seen, name_str, strlen(name_str))) continue;
            hset_insert(&seen, name_str, strlen(name_str));

            ptr->address_of.target =
              ast_node_create_sym(self->transient, string_t_str(&variables.v[i].name));

            ptr->address_of.target->type = variables.v[i].type;
            ptr->type->pointer.target    = variables.v[i].type;

            out_put_start(self, "");
            out_put(self, emit_type_declaration(self, ptr->type, name_str, 0));
            out_put(self, ";\n");
        }
        if (!variables.size) out_put_start(self, "char empty;\n");
    }
    self->indent_level--;
    out_put_start(self, "};\n\n");
}

static void make_one_thunk(generate_thunks_ctx *ctx, ast_node *node) {
    // make a thunk for an if-then-else arm

    transpiler *self = ctx->self;

    u64         hash = ast_node_hash(node);
    if (hset_contains(ctx->seen, &hash, sizeof hash)) return;

    hset_insert(&ctx->seen, &hash, sizeof hash);

    // figure out the free variables in use in this node
    tl_free_variable_sized free_variables = ti_free_variables_in(self->transient, node);

    // declare struct for thunk context
    char *struct_name = null;
    if (free_variables.size) {
        struct_name = make_thunk_struct_name(self->strings, hash);
        emit_thunk_struct(self, struct_name, free_variables);
    }

    // function declaration
    char *name = make_thunk_name(self->strings, hash);

    // return type and name
    out_put_start(self, "static ");

    out_put(self, emit_symbol_declaration(self, node, name, 1));
    out_put(self, " ");

    // params
    out_put(self, "(");
    if (free_variables.size) out_put_fmt(self, "struct %s * %s", struct_name, struct_name);
    else out_put(self, "void");
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    // add my free variables to the stack
    u32 save = push_free_variables(self, node, struct_name, null);
    a_thunk(self, node);
    pop_free_variables(self, save);

    char const *body = pop_result(self);
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");
}

static void look_for_thunks(void *ctx_, ast_node *node) {
    generate_thunks_ctx *ctx  = ctx_;
    transpiler          *self = ctx->self;

    if (ast_if_then_else == node->tag) {
        log(self, "thunk: if-then-else: %s", ast_node_to_string(self->strings, node));
        make_one_thunk(ctx, node->if_then_else.yes);
        make_one_thunk(ctx, node->if_then_else.no);
    }
}

static void generate_thunks(transpiler *self, ast_node **nodes, u32 n) {
    generate_thunks_ctx ctx;
    ctx.self = self;
    ctx.seen = hset_create(self->transient);

    map_destroy(&self->apply_contexts);
    self->apply_contexts = map_create(self->transient, sizeof(u64));

    for (u32 i = 0; i < n; i++) {
        ast_node *node = nodes[i];
        ast_node_dfs_safe_for_recur(self->transient, &ctx, node, look_for_thunks);
    }
    hset_destroy(&ctx.seen);
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

static int a_result_type_of(transpiler *self, tl_type const *ty) {

    // FIXME duplicate of emit_type

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

        u64   hash = transpiler_type_hash(ty);
        char *name = make_tuple_struct_name(self->alloc, hash);
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
        out_put(self, emit_type_declaration(self, ty, field_name, 0));
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

    // FIXME special case the lambda
    if (ast_lambda_function == node->let_in.value->tag) {

        out_put_start(self, "");
        out_put(self, emit_symbol_declaration(self, node->let_in.name, name, 0));
        out_put_fmt(self, " = %s;\n", make_function_name(self->strings, name));

        // eval the body and leave it on the stack
        if (a_eval(self, node->let_in.body)) return 1;

    } else {
        if (a_eval(self, node->let_in.value)) return 1;
        char const *value = pop_result(self);

        // if value is a lambda function, add it to the current context
        // FIXME what is this so called lambda context anymore??
        if (ast_lambda_function == node->let_in.value->tag) {
            map_set(&self->lambdas, name, strlen(name), &node->let_in.value);
        }

        out_put_start(self, "");
        out_put(self, emit_symbol_declaration(self, node->let_in.name, name, 0));
        out_put_fmt(self, " = %s;\n", value);

        // eval the body and leave it on the stack
        if (a_eval(self, node->let_in.body)) return 1;
    }
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
        out_put(self, emit_symbol_declaration(self, ass->value, var_name, 0));
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
    tl_free_variable_sized yes_free = ti_free_variables_in(self->transient, v->yes);
    tl_free_variable_sized no_free  = ti_free_variables_in(self->transient, v->no);

    // make the context structs
    emit_thunk_struct_init(self, yes_ctx, "tl_ctx_yes_", yes_free);
    emit_thunk_struct_init(self, no_ctx, "tl_ctx_no_", no_free);

    // eval the condition
    if (a_eval(self, v->condition)) return 1;
    char const *condition = pop_result(self);

    // dispatch to thunks
    char *var = next_variable(self);
    out_put_start(self, "");
    out_put(self, emit_symbol_declaration(self, node, var, 0));
    out_put(self, ";\n");
    out_put_start_fmt(self, "if (%s) %s = %s(", condition, var, yes);
    if (yes_free.size) out_put(self, "&tl_ctx_yes_");
    out_put_fmt(self, "); else %s = %s(", var, no);
    if (no_free.size) out_put(self, "&tl_ctx_no_");
    out_put(self, ");\n");
    return 0;
}

static int a_field_access(transpiler *self, ast_node const *node) {

    struct ast_user_type_get const *v   = ast_node_utg((ast_node *)node);

    char                           *var = next_variable(self);

    out_put_start(self, "");
    out_put(self, emit_symbol_declaration(self, node, var, 0));
    if (type_pointer == v->struct_name->type->tag)
        out_put_start_fmt(self, " = %s->%s;\n", emit_symbol_use(self, v->struct_name),
                          ast_node_name_string(v->field_name));
    else
        out_put_start_fmt(self, " = %s.%s;\n", emit_symbol_use(self, v->struct_name),
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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
    out_put_start_fmt(self, " = %s;\n", value);

    // assign to struct field
    if (type_pointer == v->struct_name->type->tag)
        out_put_start_fmt(self, "%s->%s = %s;\n", emit_symbol_use(self, v->struct_name), field_name, var);
    else out_put_start_fmt(self, "%s.%s = %s;\n", emit_symbol_use(self, v->struct_name), field_name, var);

    return 0;
}

static char const *find_free_variable_context(transpiler *self, char const *name) {
    forall(i, self->thunk_free_variables) {
        if (0 == strcmp(self->thunk_free_variables.v[i].free_variable, name))
            return self->thunk_free_variables.v[i].struct_name;
    }
    return null;
}

static char *emit_symbol_use(transpiler *self, ast_node const *node) {
    // emit the symbol name, respecting thunk context and function name mangling
    char const *name = ast_node_name_string(node);

    if (tl_type_get_arrow(node->type)) {
        return make_function_name(self->strings, name);
    } else {
        if (self->is_eval_in_thunk) {
            char const *struct_name = find_free_variable_context(self, name);
            if (struct_name) {
                int   len = snprintf(null, 0, "(*(%s->%s))", struct_name, name) + 1;
                char *out = alloc_malloc(self->transient, len);
                snprintf(out, len, "(*(%s->%s))", struct_name, name);
                return out;
            } else return alloc_strdup(self->transient, name);
        } else return alloc_strdup(self->transient, name);
    }
}

static char const *emit_type_arrow_ok(transpiler *self, tl_type *type, char const *void_decl,
                                      char const *var_name_for_arrow) {
    char const *decl_type = null;
    char        buf[128];
    switch (type->tag) {
    case type_nil:    decl_type = void_decl; break;
    case type_bool:   decl_type = "int"; break;
    case type_int:    decl_type = "int64_t"; break;
    case type_float:  decl_type = "double"; break;
    case type_string: decl_type = "char *"; break; // TODO const?

    case type_tuple:
    case type_labelled_tuple:
        if (!type->array.elements.size) {
            decl_type = void_decl;
        } else {
            u64 hash = transpiler_type_hash(type);
            int len = snprintf(buf, sizeof buf, "struct %s", make_tuple_struct_name(self->transient, hash));
            buf[len]  = '\0';
            decl_type = alloc_strdup(self->transient, buf);
        }
        break;

    case type_user: {
        int len   = snprintf(buf, sizeof buf, "struct %s", type->user.name);
        buf[len]  = '\0';
        decl_type = alloc_strdup(self->transient, buf);
    } break;

    case type_type_var: {
        int len   = snprintf(buf, sizeof buf, "/* tv%u */ int", type->type_var.val);
        buf[len]  = '\0';
        decl_type = alloc_strdup(self->transient, buf);
    } break;

    case type_pointer:
        if (type_arrow == type->pointer.target->tag) {
            if (!var_name_for_arrow) fatal("logic error");
            decl_type = emit_type_arrow_ok(self, type->pointer.target, void_decl, var_name_for_arrow);
        } else {
            int len =
              snprintf(buf, sizeof buf, "%s *",
                       emit_type_arrow_ok(self, type->pointer.target, void_decl, var_name_for_arrow));
            buf[len]  = '\0';
            decl_type = alloc_strdup(self->transient, buf);
        }
        break;

    case type_any:
    case type_ellipsis:
        //
        decl_type = void_decl;
        break;

    case type_arrow: {

        if (!var_name_for_arrow) fatal("logic error");

        char const            *result_type    = emit_type(self, type->arrow.right, void_decl);
        tl_type_sized          parameters     = type->arrow.left->array.elements;
        char const            *struct_name    = null;
        tl_free_variable_sized free_variables = type->arrow.free_variables;
        char                   buf[512];

        if (free_variables.size) {
            struct_name = make_context_struct_name(self->transient, type);
        }
        int len = snprintf(buf, sizeof buf, "(");

        if (struct_name) len += snprintf(buf + len, sizeof buf - len, "struct %s *", struct_name);
        for (u32 i = 0; i < parameters.size; ++i) {
            if (struct_name || i) len += snprintf(buf + len, sizeof buf - len, ", ");
            len += snprintf(buf + len, sizeof buf - len, "%s",
                            emit_type_arrow_ok(self, parameters.v[i], void_decl, ""));
        }
        if (!parameters.size && !struct_name) len += snprintf(buf + len, sizeof buf - len, "void");
        len += snprintf(buf + len, sizeof buf - len, ")");
        buf[len]  = '\0';

        len       = snprintf(null, 0, "%s (*%s)%s", result_type, var_name_for_arrow, buf) + 1;
        decl_type = alloc_malloc(self->transient, len);
        snprintf((char *)decl_type, len, "%s (*%s)%s", result_type, var_name_for_arrow, buf);
    } break;
    }

    return decl_type;
}

static char const *emit_type(transpiler *self, tl_type *type, char const *void_decl) {
    return emit_type_arrow_ok(self, type, void_decl, null);
}

// FIXME use tl_type_get_arrow instead
static int is_arrow_or_pointer_to_arrow(tl_type *type) {
    return (type_arrow == type->tag ||
            (type_pointer == type->tag && type_arrow == type->pointer.target->tag));
}

static char const *emit_type_declaration(transpiler *self, tl_type *type, char const *var, int is_void_ok) {
    char const *void_decl = is_void_ok ? "void" : "/* void */ int";
    int         len       = 0;
    if (is_arrow_or_pointer_to_arrow(type)) {
        len = snprintf(null, 0, "%s", emit_type_arrow_ok(self, type, void_decl, var)) + 1;
    } else {
        len = snprintf(null, 0, "%s %s", emit_type(self, type, void_decl), var) + 1;
    }
    char *out = alloc_malloc(self->transient, len);

    if (is_arrow_or_pointer_to_arrow(type)) {
        len = snprintf(out, len, "%s", emit_type_arrow_ok(self, type, void_decl, var)) + 1;
    } else {
        len = snprintf(out, len, "%s %s", emit_type(self, type, void_decl), var) + 1;
    }

    return out;
}

static char const *emit_symbol_declaration(transpiler *self, ast_node const *node, char const *var,
                                           int is_void_ok) {
    // emit a declaration for variable var using the type of node
    tl_type    *type      = node->type;
    char const *void_decl = is_void_ok ? "void" : "/* void */ int";

    if (is_arrow_or_pointer_to_arrow(type)) {

        return emit_type_arrow_ok(self, type, void_decl, var);

    } else if (is_nil_result(type)) {
        int   len = snprintf(null, 0, "%s %s", void_decl, var) + 1;
        char *out = alloc_malloc(self->transient, len);
        snprintf(out, len, "%s %s", void_decl, var);
        return out;
    } else {
        char const *result_type = emit_type(self, type, void_decl);
        int         len         = snprintf(null, 0, "%s %s", result_type, var) + 1;
        char       *out         = alloc_malloc(self->transient, len);
        snprintf(out, len, "%s %s", result_type, var);
        return out;
    }
}

static int a_eval(transpiler *self, ast_node const *node) {

    if (!node || !node->type) fatal("a_eval: node or type is null");

    char *var = next_variable(self);

    out_put(self, "\n");

    if (self->verbose) out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->strings, node));

    out_put_start(self, "");
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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
        if (var) out_put_start_fmt(self, "%s = %s;\n", var, emit_symbol_use(self, node));
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
                out_put_start_fmt(self, "%s = &(%s);\n", var,
                                  emit_symbol_use(self, node->address_of.target));
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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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

    struct ast_named_application const *v        = ast_node_named((ast_node *)node);
    char const                         *fun_name = ast_node_name_string(v->name);
    // ast_node_sized                      free_variables = v->free_variables;

    int is_special_name = 0;
    if (ti_is_c_function_name(fun_name)) {
        fun_name += 2;
        is_special_name = 1;
    } else if (ti_is_generated_variable_name(fun_name)) {
        ;
    } else if (ti_is_std_function_name(fun_name)) {
        is_special_name = 1;
    } else {
        fun_name = make_function_name(self->strings, fun_name);
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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
    out_put(self, ";\n");

    // function call
    tl_type               *fun_type       = node->named_application.name->type;
    tl_free_variable_sized free_variables = {0};

    // FIXME I don't really want to do this: it's only because the ti
    // is not giving us mono types for c_malloc and c_free at the moment.
    if (!is_special_name) {
        assert(type_arrow == fun_type->tag);
        free_variables = fun_type->arrow.free_variables;
    }

    char const *struct_name = null;
    if (free_variables.size) struct_name = make_context_struct_name(self->transient, v->name->type);

    // prepare the function call context, if any
    out_put_start(self, "{\n");
    self->indent_level++;

    if (free_variables.size) emit_thunk_struct_init(self, struct_name, "tl_apply_ctx_", free_variables);

    if (is_nil_result(fun_type)) out_put_start(self, "");
    else out_put_start_fmt(self, "%s = ", var);

    // function call
    out_put_fmt(self, "%s(", fun_name);
    if (free_variables.size) out_put_fmt(self, "&tl_apply_ctx_");
    for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
        if (i || free_variables.size) out_put(self, ", ");
        char const *arg = pop_result(self); // pop previously evaluated argument
        out_put(self, arg);
    }
    out_put(self, ");\n");

    if (is_nil_result(fun_type)) out_put_start_fmt(self, "%s = 0;/* void */\n", var);

    self->indent_level--;
    out_put_start(self, "}\n");
    return 0;

    return 0;
}

static int a_tuple_init(transpiler *self, ast_node const *node) {
    char *var = next_variable(self);

    // eval arguments in reverse order, then generate function call,
    // assigning to the result variable
    struct ast_tuple const *v      = ast_node_tuple((ast_node *)node);
    i32 const               n_args = v->n_elements;

    u64                     hash   = transpiler_type_hash(node->type);
    char                   *name   = make_tuple_struct_name(self->alloc, hash);

    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, v->elements[i])) return 1;

    // init result
    out_put_start(self, "");
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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
    u64                              hash   = transpiler_type_hash(node->type);
    char                            *name   = make_tuple_struct_name(self->alloc, hash);

    if (n_args)
        for (i32 i = n_args - 1; i >= 0; --i)
            if (a_eval(self, v->assignments[i]->assignment.value)) return 1;

    // init result
    out_put_start(self, "");
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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

    char *name = make_tuple_struct_constructor_name(self->alloc, transpiler_type_hash(node->type));

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
    out_put(self, emit_symbol_declaration(self, node, var, 0));
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

static char *make_context_struct_name(allocator *alloc, tl_type *type) {
    // type is the full arrow type
    char *name = null;
    u64   hash = transpiler_type_hash(type);
    {
#define fmt "tl_gen_ctx_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static char *make_tuple_struct_name(allocator *alloc, u64 hash) {
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

static char *make_tuple_struct_constructor_name(allocator *alloc, u64 hash) {
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
    // use these only for thunks generated from if-then-else arms
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
    // use these only for thunks generated from if-then-else arms
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

static char *make_lambda_struct_name(allocator *alloc, u64 hash) {
    char *name = null;
    {
#define fmt "tl_gen_lambda_struct_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        name = alloc_malloc(alloc, (u32)len);
        snprintf(name, (u32)len, fmt, hash);
#undef fmt
    }

    return name;
}

static char *make_lambda_function_name(allocator *alloc, u64 hash) {
    char *name = null;
    {
#define fmt "tl_gen_lambda_%" PRIu64 "_"
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

    ti_function_record *rec = lookup_function(self, name);
    if (!rec) fatal("function record not found: '%s'", name);
    tl_type *tuple          = rec->type->arrow.left;
    u64      hash           = transpiler_type_hash(tuple);
    char    *generated_name = make_tuple_struct_name(self->alloc, hash);

    // TODO since tuple types are not named, the program doesn't know
    // it already has a tuple matching a particular type signature.
    if (map_get(self->processed_structs, generated_name, strlen(generated_name))) return 0;
    map_set(&self->processed_structs, generated_name, strlen(generated_name), generated_name);

    if (self->verbose) out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->strings, node));

    out_put_start_fmt(self, "struct %s {\n", generated_name);
    self->indent_level++;

    if (type_tuple == tuple->tag) {
        struct tlt_tuple *tup = tl_type_tup(tuple);

        for (u32 i = 0; i < tup->elements.size; ++i) {
            char buf[32];
            snprintf(buf, sizeof buf - 1, "x%u", i);

            out_put_start(self, "");
            out_put(self, emit_type_declaration(self, tup->elements.v[i], buf, 0));
            out_put(self, ";\n");
        }
    } else if (type_labelled_tuple == tuple->tag) {
        struct tlt_labelled_tuple *lt = tl_type_lt(tuple);

        for (u32 i = 0; i < lt->fields.size; ++i) {

            out_put_start(self, "");
            out_put(self, emit_type_declaration(self, lt->fields.v[i], lt->names.v[i], 0));
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
    out_put(self, emit_type_declaration(self, rec->type->arrow.right, name, 0));
    out_put(self, " ");

    // params
    out_put(self, "(");
    for (u32 i = 0; i < v->n_parameters; ++i) {
        out_put(self,
                emit_symbol_declaration(self, v->parameters[i], ast_node_name_string(v->parameters[i]), 0));
        // a_declaration(self, v->parameters[i]->type, null, ast_node_name_string(v->parameters[i]));
        if (i < v->n_parameters - 1) out_put(self, ", ");
    }
    if (!v->n_parameters) out_put(self, "void");
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

static void generate_one_toplevel_context(void *ctx, ast_node *node) {
    if (ast_let != node->tag) return;
    transpiler *self = ctx;

    // FIXME: we're going to also handle tuple constructors here,
    // basically every let node should be the same. Not sure about
    // this, we could leave tuples in a separate stage.

    struct ast_let const *v    = ast_node_let(node);
    char const           *name = string_t_str(&v->name->symbol.name);
    ti_function_record   *rec  = lookup_function(self, name);
    if (!rec) fatal("function record not found: '%s'", name);

    // determine if a free variable context is required. If so, output
    // the context struct definition.
    assert(type_arrow == v->name->type->tag);
    tl_free_variable_sized free_variables = v->name->type->arrow.free_variables;

    if (free_variables.size) {
        char const *struct_name = make_context_struct_name(self->strings, v->name->type);
        out_put(self, "\n");
        emit_thunk_struct(self, struct_name, free_variables);
    }
}

static int generate_toplevel_contexts(transpiler *self, ast_node_sized nodes) {

    forall(i, nodes) {
        generate_one_toplevel_context(self, nodes.v[i]);
    }

    return 0;
}

static void generate_one_toplevel_lambda(void *ctx, ast_node *node) {
    if (ast_lambda_function != node->tag) return;
    transpiler                       *self     = ctx;
    struct ast_lambda_function const *v        = ast_node_lf(node);
    u64                               hash     = ast_node_hash(node);
    char const                       *fun_name = make_lambda_function_name(self->transient, hash);

    // determine if a free variable context is required. If so, output
    // the context struct definition.
    assert(type_arrow == node->type->tag);
    tl_free_variable_sized free_variables = node->type->arrow.free_variables;

    char const            *struct_name    = null;
    if (free_variables.size) {
        struct_name = make_lambda_struct_name(self->strings, hash);
        out_put(self, "\n");
        emit_thunk_struct(self, struct_name, free_variables);
    }

    // return type and name
    if (self->verbose) out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->transient, node));
    out_put_start(self, "static ");

    out_put(self, emit_type_declaration(self, node->type->arrow.right, fun_name, 1));
    out_put(self, " ");

    // params
    out_put(self, "(");
    if (free_variables.size) out_put_fmt(self, "struct %s * %s", struct_name, struct_name);
    for (u32 i = 0; i < v->n_parameters; ++i) {
        if (i || free_variables.size) out_put(self, ", ");
        out_put(self,
                emit_symbol_declaration(self, v->parameters[i], ast_node_name_string(v->parameters[i]), 0));
    }
    if (!free_variables.size && !v->n_parameters) out_put(self, "void");
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    // add my free variables to the stack
    u32 save = push_free_variables_ext(self, free_variables, struct_name, null);
    a_thunk(self, v->body);
    pop_free_variables(self, save);

    char const *body = pop_result(self);
    out_put_start_fmt(self, "return %s;", body);

    self->indent_level--;
    out_put(self, "\n}\n\n");
}

static int generate_toplevel_lambdas(transpiler *self, ast_node_sized nodes) {

    forall(i, nodes) {
        generate_one_toplevel_lambda(self, nodes.v[i]);
    }

    return 0;
}

static int generate_one_toplevel_prototype(transpiler *self, ast_node const *node) {
    if (ast_let != node->tag) return 0;
    if (ast_node_is_tuple_constructor(node)) return 0; // handled by a_let_struct_phase

    // don't emit generic prototypes: they are generic because they
    // are not used by the program.
    if (is_generic_function(self, node)) return 0;

    if (BIT_TEST(node->let.flags, AST_LET_FLAG_INTRINSIC)) return 0;

    struct ast_let const *v    = ast_node_let((ast_node *)node);
    char const           *name = string_t_str(&v->name->symbol.name);

    assert(strlen(name));

    if (0 == string_t_cmp_c(&v->name->symbol.name, "main")) {
        // skip here, let a_main process it.
        return 0;
    }

    ti_function_record *rec = lookup_function(self, name);
    if (!rec) fatal("function record not found: '%s'", name);

    // mangle the name
    name = make_function_name(self->strings, name);

    // determine if a free variable context is required. If so, output
    // the context struct definition.
    assert(type_arrow == v->name->type->tag);
    tl_free_variable_sized free_variables = v->name->type->arrow.free_variables;
    assert(free_variables.size == v->name->type->arrow.free_variables.size);
    char *struct_name = null;
    if (free_variables.size) {
        struct_name = make_context_struct_name(self->strings, v->name->type);
        // out_put(self, "\n");
        // emit_thunk_struct(self, struct_name, free_variables);
    }

    // function declaration

    // return type and name
    if (self->verbose) out_put_start_fmt(self, "/* %s */\n", ast_node_to_string(self->transient, node));
    out_put_start(self, "static ");
    out_put(self, emit_type_declaration(self, rec->type->arrow.right, name, 1));
    out_put(self, " ");

    // params
    out_put(self, "(");
    if (free_variables.size) out_put_fmt(self, "struct %s *", struct_name);
    for (u32 i = 0; i < v->n_parameters; ++i) {
        if (i || struct_name) out_put(self, ", ");
        out_put(self,
                emit_symbol_declaration(self, v->parameters[i], ast_node_name_string(v->parameters[i]), 0));
    }
    if (!free_variables.size && !v->n_parameters) out_put(self, "void");
    out_put(self, ");\n");

    return 0;
}

static int generate_toplevel_prototypes(transpiler *self, ast_node_sized nodes) {

    forall(i, nodes) {
        generate_one_toplevel_prototype(self, nodes.v[i]);
    }

    return 0;
}

static int a_let(transpiler *self, ast_node const *node) {
    map_reset(self->lambdas);

    if (BIT_TEST(node->let.flags, AST_LET_FLAG_INTRINSIC)) {
        log(self, "skipping '%s' because it is an intrinsic function",
            ast_node_name_string(node->let.name));
        return 0;
    }

    if (ast_node_is_tuple_constructor(node)) return 0; // handled by a_let_struct_phase
    if (0 == string_t_cmp_c(&node->let.name->symbol.name, "main"))
        return 0; // skip here, let a_main process it.

    struct ast_let const *v        = ast_node_let((ast_node *)node);
    char const           *name     = string_t_str(&v->name->symbol.name);
    tl_type              *fun_type = v->name->type;
    assert(type_arrow == fun_type->tag);

    tl_free_variable_sized free_variables = fun_type->arrow.free_variables;
    char                  *struct_name    = null;
    if (free_variables.size) struct_name = make_context_struct_name(self->strings, v->name->type);

    if (is_generic_function(self, node))
        fatal("cannot process generic function '%s' ", string_t_str(&v->name->symbol.name));

    log(self, "processing '%s'...", name);

    ti_function_record *rec = lookup_function(self, name);
    if (!rec) fatal("function record not found: '%s'", name);

    // mangle the name
    name = make_function_name(self->strings, name);

    // function declaration

    // return type and name
    out_put_start(self, "static ");
    out_put(self, emit_type_declaration(self, rec->type->arrow.right, name, 1));
    out_put(self, " ");

    // params
    out_put(self, "(");
    if (free_variables.size) out_put_fmt(self, "struct %s * %s", struct_name, struct_name);
    for (u32 i = 0; i < v->n_parameters; ++i) {
        if (i || struct_name) out_put(self, ", ");
        out_put(self,
                emit_symbol_declaration(self, v->parameters[i], ast_node_name_string(v->parameters[i]), 0));
    }
    if (!free_variables.size && !v->n_parameters) out_put(self, "void");
    out_put(self, ")");

    // body
    out_put(self, " {\n");
    self->indent_level++;

    u32 save = push_free_variables_ext(self, free_variables, struct_name, null);
    a_thunk(self, v->body);
    pop_free_variables(self, save);

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

    ti_function_record *rec = lookup_function(self, ast_node_name_string(node->let.name));
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
    if (!self->results.size) {
        log(self, "logic error: empty result stack"); // FIXME
        return "empty_result";
    }
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

static u32 push_free_variables_ext(transpiler *self, tl_free_variable_sized free_variables,
                                   char const *struct_name, u32 *count) {
    u32 save = self->thunk_free_variables.size;

    forall(i, free_variables) {
        free_variable_context_name val = {.free_variable = string_t_str(&free_variables.v[i].name),
                                          .struct_name   = struct_name};
        array_push(self->thunk_free_variables, &val);
    }

    if (count) *count = self->thunk_free_variables.size - save;
    return save;
}

static u32 push_free_variables(transpiler *self, ast_node const *node, char const *struct_name,
                               u32 *count) {
    return push_free_variables_ext(self, ti_free_variables_in(self->transient, node), struct_name, count);
}

static void pop_free_variables(transpiler *self, u32 save) {
    self->thunk_free_variables.size = save;
}

static u64 transpiler_type_hash(tl_type const *type) {
    return tl_type_hash_ext(type, 0, 0);
}

static ti_function_record *lookup_function(transpiler *self, char const *name) {
    return map_get(self->functions, name, strlen(name));
}

static void collect_function_records(transpiler *self, ast_node **nodes, u32 n) {
    for (u32 i = 0; i < n; ++i) {
        ast_node *node = nodes[i];
        if (ast_let == node->tag) {
            ti_function_record rec = {
              .name = ast_node_name_string(node->let.name), .node = node, .type = node->let.name->type};
            map_set(&self->functions, rec.name, strlen(rec.name), &rec);
            log(self, "%s", ast_node_to_string(self->transient, node));
        }
    }
}
