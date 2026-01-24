#include "infer.h"
#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "error.h"
#include "parser.h"
#include "str.h"
#include "type.h"

#include "ast.h"
#include "hashmap.h"

#include "type_registry.h"
#include "types.h"

#include <stdarg.h>
#include <stdio.h>

#define DEBUG_RESOLVE   0
#define DEBUG_RENAME    0
#define DEBUG_CONSTRAIN 0

typedef struct {
    enum tl_error_tag tag;
    ast_node const   *node;
    str               message;
} tl_infer_error;

defarray(tl_infer_error_array, tl_infer_error);

struct tl_infer {
    allocator           *transient;
    allocator           *arena;

    tl_infer_opts        opts;

    tl_type_registry    *registry;
    tl_type_env         *env;
    tl_type_subs        *subs;

    ast_node_array       synthesized_nodes;

    hashmap             *toplevels;      // str => ast_node*
    hashmap             *instances;      // u64 hash => str specialised name in env
    hashmap             *instance_names; // str set
    str_array            hash_includes;
    tl_infer_error_array errors;

    // Context for single-pass parsing of user type definitions
    tl_type_registry_parse_type_ctx type_parse_ctx;

    u32                             next_var_name;
    u32                             next_instantiation;

    int                             verbose;
    int                             verbose_ast;
    int                             indent_level;

    int is_constrain_ignore_error; // non-zero if no error should be reported during unification
};

typedef struct {
    u64 name_hash;
    u64 type_hash;
} name_and_type;

typedef enum {
    npos_toplevel,
    npos_formal_parameter,
    npos_function_argument,
    npos_let_in_lhs,
    npos_let_in_rhs,
    npos_assign_lhs,
    npos_assign_rhs,
    npos_operand,
    npos_field_name,
} node_position;

typedef struct {
    hashmap      *lexical_names;  // exists only during traverse_ast: hset str
    hashmap      *type_arguments; // map str -> tl_monotype*: arguments which are type literals
    void         *user;
    tl_monotype  *result_type; // result type of current function being traversed
    node_position node_pos;    // set by traverse_ast based on parent node
    int           is_field_name;
    int           is_annotation;
} traverse_ctx;

typedef int (*traverse_cb)(tl_infer *, traverse_ctx *, ast_node *);

typedef struct {
    hashmap *lex;
    int      is_field;
} rename_variables_ctx;

static int resolve_node(tl_infer *, ast_node *sym, traverse_ctx *ctx, node_position pos);

// ============================================================================
// Public API
// ============================================================================

static void      apply_subs_to_ast(tl_infer *);
static str       next_instantiation(tl_infer *, str);
static void      cancel_last_instantiation(tl_infer *);

static void      toplevel_add(tl_infer *, str, ast_node *);
static ast_node *toplevel_iter(tl_infer *, hashmap_iterator *);
static void      toplevel_del(tl_infer *self, str name);

static void      dbg(tl_infer const *self, char const *restrict fmt, ...);
static void      log_toplevels(tl_infer const *);
static void      log_env(tl_infer const *);
static void      log_subs(tl_infer *);

tl_infer        *tl_infer_create(allocator *alloc, tl_infer_opts const *opts) {
    tl_infer *self           = new (alloc, tl_infer);

    self->opts               = *opts;

    self->transient          = arena_create(alloc, 4096);
    self->arena              = arena_create(alloc, 16 * 1024);
    self->env                = tl_type_env_create(self->arena);
    self->subs               = tl_type_subs_create(self->arena);
    self->registry           = tl_type_registry_create(self->arena, self->transient, self->subs);

    self->synthesized_nodes  = (ast_node_array){.alloc = self->arena};

    self->toplevels          = null;
    self->instances          = map_new(self->arena, name_and_type, str, 512);
    self->instance_names     = hset_create(self->arena, 512);
    self->hash_includes      = (str_array){.alloc = self->arena};
    self->errors             = (tl_infer_error_array){.alloc = self->arena};

    self->next_var_name      = 0;
    self->next_instantiation = 0;

    self->verbose            = 0;
    self->verbose_ast        = 0;
    self->indent_level       = 0;
    self->is_constrain_ignore_error = 0;

    tl_type_registry_parse_type_ctx_init(self->arena, &self->type_parse_ctx, null);

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    if ((*p)->toplevels) map_destroy(&(*p)->toplevels);
    if ((*p)->instances) map_destroy(&(*p)->instances);
    if ((*p)->instance_names) hset_destroy(&(*p)->instance_names);

    arena_destroy(&(*p)->transient);
    arena_destroy(&(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose      = verbose;
    self->env->verbose = verbose;
}

void tl_infer_set_verbose_ast(tl_infer *self, int verbose) {
    self->verbose_ast = verbose;
}

tl_type_registry *tl_infer_get_registry(tl_infer *self) {
    return self->registry;
}

void tl_infer_get_arena_stats(tl_infer *self, arena_stats *out) {
    arena_get_stats(self->arena, out);
}

static int constrain(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node);

static int env_insert_constrain(tl_infer *self, str name, tl_polytype *type, ast_node const *node) {

    // insert type in the environment, but constrains against existing type, if any.

    tl_polytype *exist = tl_type_env_lookup(self->env, name);
    if (exist) {
        if (constrain(self, exist, type, node)) return 1;
    } else {
        tl_type_env_insert(self->env, name, type);
    }
    return 0;
}

static void expected_type(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
}

static void expected_tagged_union(tl_infer *self, ast_node const *node) {
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_tagged_union_expected_tagged_union, .node = node}));
}

static void wrong_number_of_arguments(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_arity, .node = node}));
}

static void tagged_union_case_syntax_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_tagged_union_case_syntax_error, .node = node}));
}

static void create_type_constructor_from_user_type(tl_infer *self, ast_node *node) {
    assert(ast_node_is_utd(node));

    tl_type_registry_parse_type_ctx_reset(&self->type_parse_ctx);
    tl_monotype *mono = tl_type_registry_parse_type_with_ctx(self->registry, node, &self->type_parse_ctx);
    if (!mono) {
        expected_type(self, node);
        return;
    }

    str name = node->user_type_def.name->symbol.name;
    tl_type_registry_insert_mono(self->registry, name, mono);
    tl_polytype *poly = tl_type_registry_get(self->registry, name);

    env_insert_constrain(self, name, poly, node);
    ast_node_type_set(node, poly);
}

static int toplevel_hash_command(tl_infer *self, ast_node *node) {
    assert(ast_node_is_hash_command(node));

    // skip #ifc .. #endc blocks
    if (ast_node_is_ifc_block(node)) return 0;

    str_sized words = node->hash_command.words;

    if (words.size < 2) {
        wrong_number_of_arguments(self, node);
        return 1;
    }

    if (str_eq(words.v[0], S("include"))) {
        array_push(self->hash_includes, words.v[1]);
        return 0;
    } else if (str_eq(words.v[0], S("import"))) {
        return 0;
    } else if (str_eq(words.v[0], S("unity_file"))) {
        return 0;
    } else if (str_eq(words.v[0], S("module"))) {
        return 0;
    } else {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_unknown_hash_command, .node = node}));
        return 1;
    }
}

static void specialize_type_alias(tl_infer *, ast_node *);

static void load_toplevel(tl_infer *self, ast_node_sized nodes) {
    // Types of toplevel nodes (see parser.c/toplevel())
    //
    // - struct/union type definition (utd, user_type_def)
    // - enum type definition (utd)
    // - function definition (let node)
    // - global value definition (let-in node)
    // - forward function declaration `(p1, p2, ...) -> r` (symbol with an arrow annotation)
    // - symbol annotation `sym : Type`
    // - type alias
    // - c chunks and hash directives, not processed here
    //
    // If the same symbol is seen more than once, it is usually an error. The exception is forward function
    // declarations.

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        if (ast_node_is_symbol(node)) {
            str        name_str = node->symbol.name;
            ast_node **p        = str_map_get(self->toplevels, name_str);
            if (p) {
                // merge annotation if existing node is a let node; otherwise error
                if (!ast_node_is_let(*p)) {
                    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                if (node->symbol.annotation) {
                    (*p)->let.name->symbol.annotation = node->symbol.annotation;
                    resolve_node(self, (*p)->let.name, null, npos_toplevel);
                }
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&self->toplevels, name_str, &node);
                    resolve_node(self, node, null, npos_toplevel);
                }
            }
        }

        else if (ast_node_is_type_alias(node)) {
            // FIXME: assmues alias name is a symbol. Parser may produce an nfa.
            if (!ast_node_is_symbol(node->type_alias.name)) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_expected_type_alias_symbol, .node = node}));
                continue;
            }

            str          name = toplevel_name(node);
            tl_monotype *mono = tl_type_registry_parse_type(self->registry, node->type_alias.target);
            tl_polytype *poly = tl_monotype_generalize(self->arena, mono);
            if (1) {
                str poly_str = tl_polytype_to_string(self->transient, poly);
                dbg(self, "type_alias: %s = %s", str_cstr(&name), str_cstr(&poly_str));
            }
            tl_type_registry_type_alias_insert(self->registry, name, poly);
            specialize_type_alias(self, node);
        }

        else if (ast_node_is_let(node)) {
            str        name_str = ast_node_str(node->let.name);
            ast_node **p        = str_map_get(self->toplevels, name_str);

            if (p) {
                // merge type if the existing node is a symbol; otherwise error
                if (!ast_node_is_symbol(*p)) {
                    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                // ignore prior type annotation if the current symbol is annotated: later
                // declaration overrides

                if (node->let.name->symbol.annotation) {
                    resolve_node(self, node->let.name, null, npos_toplevel);
                } else {
                    // otherwise merge in the prior annotation
                    node->let.name->symbol.annotation      = (*p)->symbol.annotation;
                    node->let.name->symbol.annotation_type = (*p)->symbol.annotation_type;
                }

                // replace prior symbol entry with let node
                *p = node;
            } else {
                str_map_set(&self->toplevels, name_str, &node);
                resolve_node(self, node->let.name, null, npos_toplevel);
            }
        }

        else if (ast_node_is_utd(node)) {
            str        name_str = ast_node_str(node->user_type_def.name);
            ast_node **p        = str_map_get(self->toplevels, name_str);

            if (p) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            } else {
                create_type_constructor_from_user_type(self, node);
                str_map_set(&self->toplevels, name_str, &node);
            }
        }

        else if (ast_node_is_let_in(node)) {
            str name_str = node->let_in.name->symbol.name;
            str_map_set(&self->toplevels, name_str, &node);
            resolve_node(self, node->let_in.name, null, npos_toplevel);
        }

        else if (ast_node_is_hash_command(node)) {
            (void)toplevel_hash_command(self, node);
        }

        else if (ast_node_is_body(node)) {
            load_toplevel(self, node->body.expressions);
        }

        else {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_invalid_toplevel, .node = node}));
            continue;
        }
    }

    arena_reset(self->transient);
}

// -- tree shake --

static ast_node *toplevel_get(tl_infer *, str);

typedef struct {
    tl_infer *self;
    hashmap  *names;  // str set
    hashmap  *recurs; // str set
} tree_shake_ctx;

hashmap *tree_shake(tl_infer *, ast_node const *);

void     do_tree_shake(void *ctx_, ast_node *node) {
    tree_shake_ctx *ctx  = ctx_;
    tl_infer       *self = ctx->self;

    if (ast_node_is_nfa(node)) {
        str name = toplevel_name(node);

        str_hset_insert(&ctx->names, name);

        // add all symbol arguments because they could be function pointers
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (ast_node_is_assignment(arg)) arg = arg->assignment.value;
            if (!ast_node_is_symbol(arg)) continue;
            if (str_eq(name, arg->symbol.name)) continue;
            if (!str_hset_contains(ctx->recurs, arg->symbol.name)) {
                str_hset_insert(&ctx->recurs, arg->symbol.name);

                // if it is a toplevel, recurse through it
                ast_node *next = toplevel_get(self, arg->symbol.name);
                if (next) {
                    ast_node_dfs(ctx, next, do_tree_shake);

                    // and save the name
                    str_hset_insert(&ctx->names, arg->symbol.name);
                }
            }
        }

        if (!str_hset_contains(ctx->recurs, name)) {
            str_hset_insert(&ctx->recurs, name);

            ast_node *next = toplevel_get(ctx->self, name);
            if (next) {
                ast_node_dfs(ctx, next, do_tree_shake);
            }

            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_node_is_let_in(node)) {
        ast_node *value = node->let_in.value;
        if (ast_node_is_symbol(value)) {
            str name = ast_node_str(value); // caution: the value name, not the let's name

            // if it is a toplevel, recurse through it
            ast_node *next = toplevel_get(self, name);
            if (next) ast_node_dfs(ctx, next, do_tree_shake);
            str_hset_insert(&ctx->recurs, name);
            str_hset_insert(&ctx->names, name);
        } else if (value) {
            // recurse into value
            ast_node *next = value;
            if (next) ast_node_dfs(ctx, next, do_tree_shake);
        }

        // the let-in name
        {
            str name = ast_node_str(node->let_in.name);
            // dbg(self, "do_tree_shake: adding '%s'", str_cstr(&name));
            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_node_is_reassignment(node)) {
        // Note: duplicate logic for let_in nodes (TODO)
        ast_node *value = node->assignment.value;
        if (ast_node_is_symbol(value)) {
            str name = ast_node_str(value); // caution: the value name, not the let's name

            // if it is a toplevel, recurse through it
            ast_node *next = toplevel_get(self, name);
            if (next) ast_node_dfs(ctx, next, do_tree_shake);
            str_hset_insert(&ctx->recurs, name);
            str_hset_insert(&ctx->names, name);
        } else if (value) {
            // recurse into value
            ast_node *next = value;
            if (next) ast_node_dfs(ctx, next, do_tree_shake);
        }

    }

    else if (ast_node_is_let(node) || ast_node_is_lambda_function(node)) {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(param)) continue;
            str name = ast_node_str(param);
            // dbg(self, "do_tree_shake: adding '%s'", str_cstr(&name));
            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_case == node->tag && node->case_.binary_predicate) {
        ast_node *pred = node->case_.binary_predicate;
        if (ast_node_is_symbol(pred)) {
            str name = ast_node_str(pred);
            str_hset_insert(&ctx->names, name);
        }
    }
}

hashmap *tree_shake(tl_infer *self, ast_node const *node) {
    tree_shake_ctx ctx = {.self = self};
    ctx.names          = hset_create(self->transient, 1024);
    ctx.recurs         = hset_create(self->transient, 1024);

    str_hset_insert(&ctx.names, toplevel_name(node));

    ast_node_dfs(&ctx, (ast_node *)node, do_tree_shake);

    return ctx.names;
}

// -- inference --

static traverse_ctx *traverse_ctx_create(allocator *transient) {
    // Use a transient allocator because the destroy function leaks the maps.
    traverse_ctx *out   = new (transient, traverse_ctx);
    out->lexical_names  = hset_create(transient, 32);
    out->type_arguments = map_create_ptr(transient, 16);
    out->user           = null;
    out->result_type    = null;
    out->node_pos       = npos_operand;
    out->is_field_name  = 0;
    out->is_annotation  = 0;

    return out;
}

static int traverse_ctx_is_param(traverse_ctx *self, str name) {
    return str_hset_contains(self->lexical_names, name);
}

typedef struct {
    char unused; // MSVC requires at least one struct member
} infer_ctx;

static infer_ctx *infer_ctx_create(allocator *alloc) {
    infer_ctx *out = new (alloc, infer_ctx);
    return out;
}

static int type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_error, .node = node}));
    return 1;
}
static int unresolved_type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_unresolved_type, .node = node}));
    return 1;
}

static void log_constraint(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
static void log_constraint_mono(tl_infer *, tl_monotype *, tl_monotype *, ast_node const *);
static void log_type_error(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
static void log_type_error_mm(tl_infer *, tl_monotype *, tl_monotype *, ast_node const *);

static int  is_carray_constructor(ast_node *);
static int  is_std_function(ast_node *);

typedef struct {
    tl_infer       *self;
    ast_node const *node;
} type_error_cb_ctx;

static void type_error_cb(void *ctx_, tl_monotype *left, tl_monotype *right) {
    type_error_cb_ctx *ctx = ctx_;
    if (!ctx->self->is_constrain_ignore_error) {
        log_type_error_mm(ctx->self, left, right, ctx->node);
        type_error(ctx->self, ctx->node);
    }
}

static int constrain_mono(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    type_error_cb_ctx error_ctx = {.self = self, .node = node};

    if (DEBUG_CONSTRAIN) {
        log_constraint_mono(self, left, right, node);
    }

    hashmap *seen = hset_create(self->transient, 32);
    int      res  = tl_type_subs_unify_mono(self->subs, left, right, type_error_cb, &error_ctx, &seen);
    return res;
}

static int constrain(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    if (left == right) return 0;
    if (0) {
        log_constraint(self, left, right, node);
    }

    tl_monotype *lhs = null, *rhs = null;

    if (left->quantifiers.size) lhs = tl_polytype_instantiate(self->arena, left, self->subs);
    else lhs = left->type;
    if (right->quantifiers.size) rhs = tl_polytype_instantiate(self->arena, right, self->subs);
    else rhs = right->type;

    return constrain_mono(self, lhs, rhs, node);
}

static int constrain_pm(tl_infer *self, tl_polytype *left, tl_monotype *right, ast_node const *node) {
    tl_polytype wrap = tl_polytype_wrap(right);
    return constrain(self, left, &wrap, node);
}

static void ensure_tv(tl_infer *self, tl_polytype **type) {
    if (!type) return;
    if (*type) return;
    *type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

static int infer_literal_type(tl_infer *self, ast_node *node,
                              tl_monotype *(*get_type)(tl_type_registry *)) {
    tl_monotype *ty = get_type(self->registry);
    ensure_tv(self, &node->type);
    return constrain_pm(self, node->type, ty, node);
}

typedef struct {
    tl_monotype *parsed;
    hashmap     *type_arguments;
} annotation_parse_result;

static annotation_parse_result parse_type_annotation(tl_infer *self, traverse_ctx *ctx,
                                                     ast_node *annotation_node) {

    if (!ast_node_is_symbol(annotation_node) && !ast_node_is_nfa(annotation_node))
        return (annotation_parse_result){0};

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_monotype                    *parsed = tl_type_registry_parse_type_out_ctx(
      self->registry, annotation_node, self->transient, ctx ? ctx->type_arguments : null, &parse_ctx);

    if (0) {
        str tmp = v2_ast_node_to_string(self->transient, annotation_node);
        dbg(self, "parse_type_annotation: '%s' -> %p", str_cstr(&tmp), parsed);
    }

    return (annotation_parse_result){.parsed = parsed, .type_arguments = parse_ctx.type_arguments};
}

static int          infer_struct_access(tl_infer *, ast_node *);
static int          add_generic(tl_infer *, ast_node *);
static int          is_type_literal(tl_infer *, traverse_ctx const *, ast_node const *);
static int          is_union_struct(tl_infer *, str);
static tl_polytype *make_arrow(tl_infer *, traverse_ctx *, ast_node_sized, ast_node *, int);
static tl_polytype *make_binary_predicate_arrow(tl_infer *, traverse_ctx *, ast_node *, ast_node *);
static void         toplevel_add(tl_infer *, str, ast_node *);
static void         update_env(tl_infer *, traverse_ctx *, ast_node *);

// ============================================================================
// Special Case Handlers
// ============================================================================

static int is_carray_constructor(ast_node *node) {
    return ast_node_is_nfa(node) && str_eq(ast_node_str(node->named_application.name), S("CArray"));
}

static int is_std_function(ast_node *node) {
    return ast_node_is_std_application(node);
}

// ============================================================================
// Type Inference Helpers
// ============================================================================

static int infer_nil(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
    return constrain_pm(self, node->type, weak, node);
}

static int infer_void(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (ctx->node_pos == npos_operand) {
        tl_monotype *nil = tl_type_registry_nil(self->registry);
        ast_node_type_set(node, tl_polytype_absorb_mono(self->arena, nil));
    }
    return 0;
}

static int infer_body(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    if (node->body.expressions.size) {
        u32       sz   = node->body.expressions.size;
        ast_node *last = node->body.expressions.v[sz - 1];
        ensure_tv(self, &last->type);

        if (ast_node_is_lambda_function(last)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_cannot_return_lambda, .node = last}));
            return 1;
        }

        return constrain(self, node->type, last->type, node);
    }
    return 0;
}

static int infer_tuple(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    ast_node_sized arr = ast_node_sized_from_ast_array(node);
    assert(arr.size > 0);

    tl_monotype_array tup_types = {.alloc = self->arena};
    array_reserve(tup_types, arr.size);
    forall(i, arr) {
        if (tl_polytype_is_scheme(arr.v[i]->type)) fatal("generic type");
        array_push(tup_types, arr.v[i]->type->type);
    }

    tl_monotype *tuple = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(tup_types));
    return constrain(self, node->type, tl_polytype_absorb_mono(self->arena, tuple), node);
}

static int infer_while(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *nil = tl_type_registry_nil(self->registry);
    return constrain_pm(self, node->type, nil, node);
}

static int infer_continue(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    return constrain_pm(self, node->type, tl_monotype_create_any(self->arena), node);
}

static int infer_return(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->return_.value, ctx, npos_operand)) return 1;

    if (node->return_.value && ast_node_is_lambda_function(node->return_.value)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_cannot_return_lambda, .node = node}));
        return 1;
    }

    ensure_tv(self, &node->type);
    if (!node->return_.is_break_statement)
        if (constrain(self, node->type, node->return_.value->type, node)) return 1;

    if (ctx->result_type)
        if (constrain_pm(self, node->return_.value->type, ctx->result_type, node)) return 1;

    return 0;
}

static int infer_lambda_function_application(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    tl_monotype *inst =
      tl_polytype_instantiate(self->arena, node->lambda_application.lambda->type, self->subs);
    ast_node_type_set(node->lambda_application.lambda, tl_polytype_absorb_mono(self->arena, inst));

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    tl_polytype       *app  = make_arrow(self, ctx, iter.nodes, node, 0);
    if (!app) return 1;

    if (self->verbose) {
        str inst_str = tl_monotype_to_string(self->transient, inst);
        str app_str  = tl_polytype_to_string(self->transient, app);
        dbg(self, "application: anon lambda %.*s callsite arrow: %.*s", str_ilen(inst_str),
            str_buf(&inst_str), str_ilen(app_str), str_buf(&app_str));
    }

    tl_polytype wrap = tl_polytype_wrap(inst);
    if (constrain(self, &wrap, app, node)) return 1;

    if (constrain(self, node->type, node->lambda_application.lambda->lambda_function.body->type, node))
        return 1;

    return 0;
}

static int infer_lambda_function(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    ensure_tv(self, &node->type);

    tl_polytype *arrow =
      make_arrow(self, ctx, ast_node_sized_from_ast_array(node), node->lambda_function.body, 1);
    if (!arrow) return 1;
    tl_polytype_generalize(arrow, self->env, self->subs);
    if (constrain(self, node->type, arrow, node)) return 1;

    return 0;
}

static int infer_if_then_else(tl_infer *self, ast_node *node) {
    tl_monotype *bool_type = tl_type_registry_bool(self->registry);
    if (constrain_pm(self, node->if_then_else.condition->type, bool_type, node)) return 1;

    ensure_tv(self, &node->type);
    if (node->if_then_else.no) {
        if (constrain(self, node->if_then_else.yes->type, node->if_then_else.no->type, node)) return 1;
        if (constrain(self, node->type, node->if_then_else.yes->type, node)) return 1;
    } else {
        tl_monotype *nil      = tl_type_registry_nil(self->registry);
        tl_monotype *any_type = tl_monotype_create_any(self->arena);
        if (constrain_pm(self, node->type, nil, node)) return 1;

        if (constrain_pm(self, node->if_then_else.yes->type, any_type, node)) return 1;
    }

    return 0;
}

static int infer_assignment(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->assignment.name, ctx, npos_assign_lhs)) return 1;
    if (resolve_node(self, node->assignment.value, ctx, npos_assign_rhs)) return 1;

    ensure_tv(self, &node->type);

    if (constrain(self, node->type, node->assignment.value->type, node)) return 1;
    if (constrain(self, node->type, node->assignment.name->type, node)) return 1;

    return 0;
}

static int infer_reassignment(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->assignment.name, ctx, npos_assign_lhs)) return 1;
    if (resolve_node(self, node->assignment.value, ctx, npos_assign_rhs)) return 1;

    ensure_tv(self, &node->type);

    // reassignment nodes have void type
    tl_monotype *nil = tl_type_registry_nil(self->registry);
    if (constrain_pm(self, node->type, nil, node)) return 1;

    // name and value are same type
    if (constrain(self, node->assignment.name->type, node->assignment.value->type, node)) return 1;

    return 0;
}

static int infer_binary_op(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    ast_node   *left = node->binary_op.left, *right = node->binary_op.right;
    char const *op = str_cstr(&node->binary_op.op->symbol.name);

    if (resolve_node(self, left, ctx, npos_operand)) return 1;
    if (resolve_node(self, right, ctx, is_struct_access_operator(op) ? npos_field_name : npos_operand))
        return 1;

    ensure_tv(self, &node->type);

    if (is_arithmetic_operator(op)) {
        if (constrain(self, node->type, left->type, node)) return 1;
        if (constrain(self, left->type, right->type, node)) return 1;
    } else if (is_bitwise_operator(op)) {
        tl_monotype *int_type = tl_type_registry_int(self->registry);
        if (constrain_pm(self, left->type, int_type, node)) return 1;
        if (constrain_pm(self, right->type, int_type, node)) return 1;
        if (constrain_pm(self, node->type, int_type, node)) return 1;
    } else if (is_logical_operator(op) || is_relational_operator(op)) {
        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, node->type, bool_type, node)) return 1;
        if (constrain(self, left->type, right->type, node)) return 1;
    } else if (is_index_operator(op)) {
        tl_monotype *int_type = tl_type_registry_int(self->registry);
        if (constrain_pm(self, right->type, int_type, node)) return 1;

        tl_monotype_substitute(self->arena, left->type->type, self->subs, null);
        tl_monotype_substitute(self->arena, right->type->type, self->subs, null);

        if (tl_monotype_has_ptr(left->type->type)) {
            tl_monotype *target = tl_monotype_ptr_target(left->type->type);
            if (constrain_pm(self, node->type, target, node)) return 1;
        }
    } else if (is_struct_access_operator(op)) {
        if (infer_struct_access(self, node)) return 1;
    } else {
        fatal("unknown operator type");
    }

    return 0;
}

static int infer_unary_op(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->unary_op.operand, ctx, npos_operand)) return 1;
    ast_node *operand = node->unary_op.operand;
    ensure_tv(self, &node->type);

    str op = ast_node_str(node->unary_op.op);
    if (str_eq(op, S("*"))) {
        tl_polytype_substitute(self->arena, operand->type, self->subs);
        if (tl_monotype_has_ptr(operand->type->type)) {
            assert(!tl_polytype_is_scheme(operand->type));
            tl_monotype *target = tl_monotype_ptr_target(operand->type->type);
            if (constrain_pm(self, node->type, target, node)) return 1;
        } else if (tl_polytype_is_concrete(operand->type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
            return 1;
        }
    } else if (str_eq(op, S("&"))) {
        if (!tl_polytype_is_scheme(operand->type)) {
            tl_monotype *ptr = tl_type_registry_ptr(self->registry, operand->type->type);
            if (constrain_pm(self, node->type, ptr, node)) return 1;
        } else {
            tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
            tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
            if (constrain_pm(self, node->type, ptr, node)) return 1;
        }
    } else if (str_eq(op, S("!"))) {
        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, node->type, bool_type, node)) return 1;
    } else if (str_eq(op, S("~")) || str_eq(op, S("-")) || str_eq(op, S("+"))) {
        if (constrain(self, node->type, operand->type, node)) return 1;
    } else {
        fatal("unknown unary operator");
    }

    return 0;
}

static int infer_case(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->case_.expression, ctx, npos_operand)) return 1;
    if (resolve_node(self, node->case_.binary_predicate, ctx, npos_operand)) return 1;

    ensure_tv(self, &node->type);
    tl_monotype *nil       = tl_type_registry_nil(self->registry);
    tl_monotype *any_type  = tl_monotype_create_any(self->arena);
    tl_polytype *expr_type = node->case_.expression->type;

    if (node->case_.conditions.size != node->case_.arms.size) fatal("logic error");

    if (node->case_.is_union) {
        // Tagged union case expression: conditions are binding patterns like "c: Circle"
        // The condition symbol (c) is bound to the variant type (Circle) for use in the arm

        // Get wrapper type and extract valid variants
        tl_monotype *wrapper_type = expr_type->type;
        tl_monotype_substitute(self->arena, wrapper_type, self->subs, null);

        // If expression type is not yet concrete, use the type annotation (e.g., T in "case x: T")
        // This handles cross-module generic functions where the type is still a type variable
        // during Phase 3 (inference) and won't be resolved until Phase 5 (specialization).
        if (!tl_monotype_is_inst(wrapper_type) && node->case_.union_annotation) {
            annotation_parse_result result = parse_type_annotation(self, ctx, node->case_.union_annotation);
            if (result.parsed && tl_monotype_is_inst(result.parsed)) {
                wrapper_type = result.parsed;
                // Constrain expression type to match annotation
                if (constrain_pm(self, expr_type, wrapper_type, node->case_.expression)) {
                    return 1;
                }
            }
        }

        if (!tl_monotype_is_inst(wrapper_type)) {
            expected_tagged_union(self, node->case_.expression);
            return 1;
        }

        // Find the 'u' (union) field in the wrapper type
        i32 u_index = tl_monotype_type_constructor_field_index(wrapper_type, S("u"));
        if (u_index < 0) {
            // wrapper type missing 'u'
            expected_tagged_union(self, node->case_.expression);
            return 1;
        }

        tl_monotype *union_type     = wrapper_type->cons_inst->args.v[u_index];
        str_sized    valid_variants = union_type->cons_inst->def->field_names;

        // Track which variants are covered (for exhaustiveness checking)
        int *variant_covered = alloc_malloc(self->transient, valid_variants.size * sizeof(int));
        memset(variant_covered, 0, valid_variants.size * sizeof(int));
        int has_else_arm = 0;

        forall(i, node->case_.conditions) {
            // Detect `else` condition on final arm
            if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) {
                has_else_arm = 1;
                break;
            }

            ast_node *cond = node->case_.conditions.v[i];
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation) {
                // "tagged union case condition must be 'binding: VariantType'"
                tagged_union_case_syntax_error(self, cond);
            }

            // Find the variant by name in the union type
            // Use the original (unmangled) name from the annotation, as union field names are unmangled
            str variant_name  = ast_node_name_original(cond->symbol.annotation);
            int variant_found = -1;
            forall(j, valid_variants) {
                if (str_eq(valid_variants.v[j], variant_name)) {
                    variant_found = (int)j;
                    break;
                }
            }

            if (variant_found < 0) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_tagged_union_unknown_variant, .node = cond}));
                return 1;
            }

            // Mark this variant as covered
            variant_covered[variant_found] = 1;

            // Get the variant type from the union type (which already has concrete types after
            // substitution) This handles both generic and non-generic variants correctly
            tl_monotype *variant_type = union_type->cons_inst->args.v[variant_found];

            // Set the binding's type (not as a literal - this is a value, not a type expression).
            // Note that we set both the condition node and the annotation_type.
            // If the case variable is mutable (var.&), we have a pointer type.
            tl_polytype *variant_poly = null;
            if (node->case_.is_union == AST_TAGGED_UNION_MUTABLE) {
                variant_poly =
                  tl_polytype_absorb_mono(self->arena, tl_type_registry_ptr(self->registry, variant_type));
            } else {
                variant_poly = tl_polytype_absorb_mono(self->arena, variant_type);
            }

            ast_node_type_set(cond, variant_poly);
            cond->symbol.annotation_type = variant_poly;
        }

        // Exhaustiveness check: if no else arm, verify all variants are covered
        if (!has_else_arm) {
            forall(j, valid_variants) {
                if (!variant_covered[j]) {
                    array_push(self->errors,
                               ((tl_infer_error){.tag = tl_err_tagged_union_missing_case, .node = node}));
                    return 1;
                }
            }
        }

    } else {
        // Standard case expression: conditions are expressions compared for equality
        forall(i, node->case_.conditions) {
            // Detect `else` condition on final arm
            if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) break;

            if (resolve_node(self, node->case_.conditions.v[i], ctx, npos_operand)) return 1;
            ensure_tv(self, &node->case_.conditions.v[i]->type);
            if (constrain(self, expr_type, node->case_.conditions.v[i]->type, node)) return 1;
        }
    }

    switch (node->case_.arms.size) {
    case 0:
        if (constrain_pm(self, node->type, nil, node)) return 1;
        break;
    case 1:
        if (constrain_pm(self, node->type, nil, node)) return 1;
        if (resolve_node(self, node->case_.arms.v[0], ctx, npos_operand)) return 1;
        if (constrain_pm(self, node->case_.arms.v[0]->type, any_type, node)) return 1;
        break;

    default: {
        if (resolve_node(self, node->case_.arms.v[0], ctx, npos_operand)) return 1;
        tl_polytype *arm_type = node->case_.arms.v[0]->type;
        if (constrain(self, node->type, arm_type, node->case_.arms.v[0])) return 1;

        forall(i, node->case_.arms) {
            if (resolve_node(self, node->case_.arms.v[i], ctx, npos_operand)) return 1;
            ensure_tv(self, &node->case_.arms.v[i]->type);
            if (constrain(self, node->case_.arms.v[i]->type, arm_type, node)) return 1;
        }
    } break;
    }

    if (node->case_.binary_predicate && node->case_.conditions.size) {
        tl_polytype *pred_arrow =
          make_binary_predicate_arrow(self, ctx, node->case_.expression, node->case_.conditions.v[0]);
        if (constrain(self, node->case_.binary_predicate->type, pred_arrow, node)) return 1;
    }

    return 0;
}

static int infer_let_in(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->let_in.name, ctx, npos_let_in_lhs)) return 1;
    if (resolve_node(self, node->let_in.value, ctx, npos_let_in_rhs)) return 1;

    ensure_tv(self, &node->type);
    if (node->let_in.body) ensure_tv(self, &node->let_in.body->type);

    if (is_carray_constructor(node->let_in.value)) {
        ast_node *nfa = node->let_in.value;
        if (2 != nfa->named_application.n_arguments) {
            wrong_number_of_arguments(self, nfa);
            return 1;
        }
        ast_node **args = nfa->named_application.arguments;
        if (ast_i64 != args[1]->tag) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_integer, .node = args[1]}));
            return 1;
        }

        tl_monotype *parsed_type = tl_type_registry_parse_type(self->registry, args[0]);
        if (!parsed_type) {
            expected_type(self, args[0]);
            return 1;
        }

        tl_monotype *inst =
          tl_type_registry_instantiate_carray(self->registry, parsed_type, (i32)args[1]->i64.val);
        if (constrain_pm(self, node->let_in.value->type, inst, node)) return 1;

        tl_monotype *ptr = tl_type_registry_ptr(self->registry, parsed_type);
        if (constrain_pm(self, node->let_in.name->type, ptr, node->let_in.name)) return 1;
        return 0;
    }

    else if (ast_node_is_lambda_function(node->let_in.value)) {
        str name = node->let_in.name->symbol.name;

        if (add_generic(self, node)) return 1;

        node->let_in.name->type = null;

        if (node->let_in.body)
            if (constrain(self, node->type, node->let_in.body->type, node)) return 1;

        {
            ast_node *let_in_lambda    = ast_node_clone(self->arena, node);
            let_in_lambda->let_in.body = null;
            toplevel_add(self, name, let_in_lambda);
        }

    } else {
        if (is_std_function(node->let_in.value)) {
            node->let_in.value->type = tl_polytype_nil(self->arena, self->registry);
        }

        tl_polytype *name_type            = node->let_in.name->type;
        tl_polytype *name_annotation_type = node->let_in.name->symbol.annotation_type;
        tl_polytype *value_type           = node->let_in.value->type;

        {
            int is_cast = 0;
            if (name_annotation_type && tl_monotype_is_ptr(name_annotation_type->type)) is_cast = 1;

            if (is_cast) self->is_constrain_ignore_error = 1;
            if (name_annotation_type) {
                name_type = name_annotation_type;

                str name  = ast_node_str(node->let_in.name);
                str tmp   = tl_polytype_to_string(self->transient, name_annotation_type);

                dbg(self, "let_in cast '%s': using annotation type '%s'", str_cstr(&name), str_cstr(&tmp));
            }

            if (constrain(self, name_type, value_type, node) && !is_cast) return 1;
            self->is_constrain_ignore_error = 0;
        }

        env_insert_constrain(self, node->let_in.name->symbol.name, name_type, node);

        if (node->let_in.body)
            if (constrain(self, node->type, node->let_in.body->type, node)) return 1;
    }
    return 0;
}

static int infer_named_function_application(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node, ctx, ctx->node_pos)) return 1;

    str          name     = ast_node_str(node->named_application.name);
    str          original = ast_node_name_original(node->named_application.name);
    tl_polytype *type     = tl_type_env_lookup(self->env, name);
    if (!type) {
        type = tl_type_registry_get(self->registry, name);
        if (!type) return 0;
    }

    if (is_type_literal(self, ctx, node)) return 0;

    if (tl_polytype_is_type_constructor(type)) {
        // This nfa can be either a type literal, or a type value constructor. Value constructors are of the
        // form `Foo(a=1, b=2)`. Type literals are of the form `Foo(Int)` for generics or plain `Foo` for
        // concrete.
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (resolve_node(self, arg, ctx, npos_function_argument)) return 1;
        }

        tl_monotype *inst = tl_type_registry_instantiate(self->registry, name);
        if (!inst) {
            wrong_number_of_arguments(self, node);
            return 1;
        }

        {
            str          inst_str = tl_monotype_to_string(self->transient, inst);
            tl_polytype *app      = make_arrow(self, ctx, iter.nodes, null, 0);
            if (!app) return 1;

            if (self->verbose) {
                str app_str = tl_polytype_to_string(self->transient, app);
                dbg(self, "type constructor: callsite '%s' (%s) arrow: %s", str_cstr(&name),
                    str_cstr(&inst_str), str_cstr(&app_str));
            }
        }

        if (!is_union_struct(self, name)) {
            iter = ast_node_arguments_iter(node);
            if (iter.nodes.size != inst->cons_inst->args.size) {
                wrong_number_of_arguments(self, node);
                return 1;
            }
        }

        u32 i = 0;
        iter  = ast_node_arguments_iter(node);
        while ((arg = ast_arguments_next(&iter))) {
            if (i >= inst->cons_inst->args.size) fatal("runtime error");

            if (ast_node_is_assignment(arg)) {
                // This is a type value constructor
                i32 found = tl_monotype_type_constructor_field_index(
                  inst, ast_node_name_original(arg->assignment.name));

                if (-1 == found) {
                    array_push(self->errors,
                               ((tl_infer_error){.tag = tl_err_field_not_found, .node = arg}));
                    return 1;
                }
                assert(found < (i32)inst->cons_inst->args.size);
                if (constrain_pm(self, arg->type, inst->cons_inst->args.v[found], node)) return 1;
            } else {
                // In this branch, node is a type literal.
            }
            ++i;
        }

        if (constrain_pm(self, node->type, inst, node)) return 1;

    } else {
        if (tl_polytype_is_concrete(type)) {
            if (!str_is_empty(original)) {
                tl_polytype *found = tl_type_env_lookup(self->env, original);
                if (found) type = found;
            }
        }

        ast_arguments_iter iter     = ast_node_arguments_iter(node);
        tl_monotype       *inst     = tl_polytype_instantiate(self->arena, type, self->subs);
        str                inst_str = tl_monotype_to_string(self->transient, inst);
        tl_polytype       *app      = make_arrow(self, ctx, iter.nodes, node, 0);
        if (!app) return 1;
        if (self->verbose) {
            str app_str = tl_polytype_to_string(self->transient, app);
            dbg(self, "application: callsite '%s' (%s) arrow: %s", str_cstr(&name), str_cstr(&inst_str),
                str_cstr(&app_str));
        }
        tl_polytype wrap = tl_polytype_wrap(inst);
        if (constrain(self, &wrap, app, node)) return 1;
    }

    return 0;
}

static void         rename_variables(tl_infer *, ast_node *, rename_variables_ctx *, int);
static void         concretize_params(tl_infer *self, ast_node *, tl_monotype *);
static tl_polytype *make_arrow(tl_infer *, traverse_ctx *, ast_node_sized, ast_node *, int is_params);
static tl_polytype *make_arrow_result_type(tl_infer *, traverse_ctx *, ast_node_sized, tl_polytype *,
                                           int is_params);
static tl_polytype *make_arrow_with(tl_infer *, traverse_ctx *, ast_node *, tl_polytype *);
static tl_polytype *make_binary_predicate_arrow(tl_infer *, traverse_ctx *, ast_node *lhs, ast_node *rhs);
static int          traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb);

//

static void      add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow);
static void      concretize_params(tl_infer *self, ast_node *node, tl_monotype *callsite);
static void      toplevel_name_replace(ast_node *node, str name_replace);

static ast_node *clone_generic_for_arrow(tl_infer *self, ast_node const *node, tl_monotype *arrow,
                                         str inst_name) {
    ast_node *clone = ast_node_clone(self->arena, node);
    ast_node *name  = toplevel_name_node(clone);
    assert(ast_node_is_symbol(name));
    name->symbol.annotation_type = null;
    name->symbol.annotation      = null;

    // rename variables: also erases type information
    rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};
    rename_variables(self, clone, &ctx, 0);

    // recalculate free variables, because symbol names have been renamed
    tl_polytype wrap = tl_polytype_wrap(arrow);
    add_free_variables_to_arrow(self, clone, &wrap);

    concretize_params(self, clone, arrow);
    toplevel_name_replace(clone, inst_name);

    return clone;
}

static int traverse_ast_node_params(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    // hashmap *types = tl_type_registry_parse_parameters(self->registry, self->transient, node);
    // map_merge(&ctx->param_types, types);

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *param;
    while ((param = ast_arguments_next(&iter))) {
        assert(ast_node_is_symbol(param));

        ensure_tv(self, &param->type);

        ctx->node_pos = npos_formal_parameter;
        if (cb(self, ctx, param)) return 1;
    }
    return 0;
}

static int traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    if (null == node) return 0;

    switch (node->tag) {
    case ast_let: {
        map_reset(ctx->type_arguments);
        map_reset(ctx->lexical_names);

        ctx->node_pos = npos_toplevel;
        // Note: traversing the name as a symbol currently causes invalid constraints to be applied when
        // specializing generic functions. The name's node->type should not in any case be relied upon: the
        // canonical arrow type of a function name is in the environment, not the ast.

        ctx->node_pos = npos_formal_parameter;
        if (traverse_ast_node_params(self, ctx, node, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->let.body, cb)) return 1;

        // Note: let nodes are intentionally not processed with the callback.

    } break;

    case ast_let_in: {

        hashmap *save = map_copy(ctx->lexical_names);
        assert(ast_node_is_symbol(node->let_in.name));

        // process name first, for lexical scope
        ctx->node_pos = npos_let_in_lhs;
        if (cb(self, ctx, node->let_in.name)) return 1;

        // process node parent before children, because there may be side effects required before traversing
        // body.
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        // traverse value first, then traverse name and body
        ctx->node_pos = npos_let_in_rhs;
        if (traverse_ast(self, ctx, node->let_in.value, cb)) return 1;
        ctx->node_pos = npos_let_in_lhs;
        if (traverse_ast(self, ctx, node->let_in.name, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->let_in.body, cb)) return 1;

        // process node again: for specialised types, typing the name depends on typing the value.
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lexical_names);
        ctx->lexical_names = save;

    } break;

    case ast_named_function_application: {

        // traverse arguments

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            ctx->node_pos = npos_function_argument;
            if (traverse_ast(self, ctx, arg, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_lambda_function: {

        hashmap *save = map_copy(ctx->lexical_names);

        if (traverse_ast_node_params(self, ctx, node, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->lambda_function.body, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lexical_names);
        ctx->lexical_names = save;

    } break;

    case ast_lambda_function_application: {

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            ctx->node_pos = npos_function_argument;
            if (traverse_ast(self, ctx, arg, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->lambda_application.lambda, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_if_then_else: {

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.condition, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.yes, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.no, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, arr.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_body:
        forall(i, node->body.expressions) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->body.expressions.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_case: {
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->case_.expression, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->case_.binary_predicate, cb)) return 1;

        if (node->case_.is_union) {
            // For union cases, conditions are handled by infer_case() directly.
            // We only need to add condition symbols to lexical scope before traversing arms.
            forall(i, node->case_.conditions) {
                hashmap  *save = map_copy(ctx->lexical_names);
                ast_node *cond = node->case_.conditions.v[i];

                // Skip nil condition (else arm)
                if (ast_node_is_nil(cond)) {
                    if (i < node->case_.arms.size) {
                        ctx->node_pos = npos_operand;
                        if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
                    }
                    ctx->lexical_names = save;
                    continue;
                }

                // Add condition symbol to lexical scope (don't traverse it - infer_case handles it)
                if (ast_node_is_symbol(cond)) {
                    str_hset_insert(&ctx->lexical_names, cond->symbol.name);
                }

                // Process only the corresponding arm (not the condition)
                if (i < node->case_.arms.size) {
                    ctx->node_pos = npos_operand;
                    if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
                }

                ctx->lexical_names = save;
            }
        } else {
            // Original behavior for non-union cases
            forall(i, node->case_.conditions) {
                ctx->node_pos = npos_operand;
                if (traverse_ast(self, ctx, node->case_.conditions.v[i], cb)) return 1;
            }
            forall(i, node->case_.arms) {
                ctx->node_pos = npos_operand;
                if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
            }
        }
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_binary_op:
        // don't traverse op, it's just an operator
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->binary_op.left, cb)) return 1;

        // when traversing to the right of . and ->, we could encounter field names that should not be
        // considered free variables, so signal that in the traverse_ctx. Note that other binary ops like
        // arithmetic should not trigger the field_name case.
        {
            char const *op               = str_cstr(&node->binary_op.op->symbol.name);
            int         is_symbol        = ast_node_is_symbol(node->binary_op.right);
            int         is_struct_access = is_struct_access_operator(op);
            int         is_field_name    = is_symbol && is_struct_access;
            int         save             = 0;
            if (is_field_name) {
                save               = ctx->is_field_name;
                ctx->is_field_name = 1;
            }

            if (is_struct_access_operator(op)) ctx->node_pos = npos_field_name;
            else ctx->node_pos = npos_operand;

            if (traverse_ast(self, ctx, node->binary_op.right, cb)) return 1;
            if (is_field_name) ctx->is_field_name = save;
        }

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_unary_op:
        // don't traverse op, it's just an operator
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->unary_op.operand, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_assignment:
        ctx->node_pos      = npos_assign_lhs;
        ctx->is_field_name = 1;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;

        ctx->node_pos      = npos_assign_rhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_reassignment:
    case ast_reassignment_op:
        // don't traverse op, it's just an operator
        ctx->node_pos      = npos_assign_lhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;

        ctx->node_pos      = npos_assign_rhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_return:
        //
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->return_.value, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_while:
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->while_.condition, cb)) return 1;

        if (node->while_.update) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->while_.update, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->while_.body, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_hash_command:
    case ast_nil:
    case ast_void:
    case ast_continue:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_string:
    case ast_char:
    case ast_symbol:
    case ast_u64:
    case ast_type_alias:
    case ast_type_assertion:
    case ast_user_type_definition:

        // operate on the leaf node
        if (cb(self, ctx, node)) return 1;

        break;
    }

    return 0;
}

static str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args,
                                       tl_polytype **out_type);

static int type_literal_specialize(tl_infer *self, ast_node *node) {
    // specialize a type id, e.g. `Point(Int)`. Contrast to specialize_type_constructor, which specialises
    // based on a callsite like `Point(1, 2)`. Assuming Point(a) { x : a, y : a }.
    // return 1 if node is not a type identifier or other error occurs.

    if (ast_node_is_symbol(node)) return 1;

    if (0) {
        str tmp = v2_ast_node_to_string(self->transient, node);
        dbg(self, "type_literal_specialize: '%s'", str_cstr(&tmp));
    }

    tl_monotype *parsed = tl_type_registry_parse_type(self->registry, node);
    if (parsed) {
        tl_monotype *target = parsed;
        if (!tl_monotype_is_inst(target)) return 1;
        str               name = target->cons_inst->def->generic_name;

        tl_monotype_sized args = target->cons_inst->args;
        tl_monotype      *inst = tl_type_registry_get_cached_specialization(self->registry, name, args);
        str               name_inst    = str_empty();
        tl_polytype      *special_type = null;
        if (!inst) {
            name_inst = specialize_type_constructor(self, name, args, &special_type);
            // ok to fail: enums, nullary builtins, etc
            if (str_is_empty(name_inst)) return 0;
            if (!special_type) return 0;
        } else {
            name_inst    = inst->cons_inst->special_name;
            special_type = tl_polytype_absorb_mono(self->arena, inst);
        }

        if (ast_node_is_symbol(node)) {
            ast_node_name_replace(node, name_inst);
            if (node->type) {
                if (constrain(self, node->type, special_type, node)) return 1;
            } else {
                ast_node_type_set(node, special_type);
            }
        } else if (ast_node_is_nfa(node)) {
            ast_node_name_replace(node->named_application.name, name_inst);
            if (node->named_application.name->type) {
                if (constrain(self, node->named_application.name->type, special_type, node)) return 1;
            } else {
                ast_node_type_set(node->named_application.name, special_type);
            }

        } else fatal("logic error");

        return 0;
    }

    return 1;
}

static int add_generic(tl_infer *, ast_node *);

static int constrain_or_set(tl_infer *self, ast_node *node, tl_polytype *type) {
#if DEBUG_RESOLVE
    str name     = ast_node_is_symbol(node) ? ast_node_str(node) : str_empty();
    str poly_str = tl_polytype_to_string(self->transient, type);
#endif

    if (node->type) {
#if DEBUG_RESOLVE
        str node_type_str = tl_polytype_to_string(self->transient, node->type);
        dbg(self, "constrain_or_set: '%s' : %s :: %s", str_cstr(&name), str_cstr(&node_type_str),
            str_cstr(&poly_str));
#endif
        if (constrain(self, node->type, type, node)) return type_error(self, node);
    }

    else {
#if DEBUG_RESOLVE
        dbg(self, "constrain_or_set: '%s': %s", str_cstr(&name), str_cstr(&poly_str));
#endif
        ast_node_type_set(node, type);
    }
    return 0;
}

static int expected_symbol(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_symbol, .node = node}));
    return 1;
}

static void update_env(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    // If it's a symbol, if it has a type, add it to the environment. If it doesn't have a type, look it up
    // from the environment.

    if (ast_node_is_symbol(node)) {
        str name = ast_node_str(node);
        if (!ctx || !str_map_contains(ctx->type_arguments, name)) {
            if (node->type) {
                env_insert_constrain(self, name, node->type, node);
            } else {
                ast_node_type_set(node, tl_type_env_lookup(self->env, name));
            }
        } else {
            // a type argument: do nothing
            ;
        }
    }
}

static int reject_type_literal(tl_infer *self, ast_node const *node) {
    if (node->type && tl_monotype_is_type_literal(node->type->type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_unexpected_type_literal, .node = node}));
        return 1;
    }
    return 0;
}

static int check_is_pointer(tl_infer *self, tl_polytype *type, ast_node *node) {
    if (tl_monotype_is_inst(type->type)) {
        if (!tl_monotype_is_ptr(type->type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
            return 1;
        }
    }
    return 0;
}

static void ensure_symbol_type_from_env(tl_infer *self, ast_node *node) {
    if (!ast_node_is_symbol(node)) return;

    tl_polytype *poly = tl_type_env_lookup(self->env, ast_node_str(node));

    // Note: do not override node->type if it is already concrete. There is some confusion with the handling
    // of type literals that makes the constrain fail unless it is guarded behind this if condition.
    if (!node->type || !tl_polytype_is_concrete(node->type)) constrain_or_set(self, node, poly);
}

static int infer_struct_access(tl_infer *self, ast_node *node) {
    if (!ast_node_is_binary_op_struct_access(node)) fatal("logic error");
    ensure_tv(self, &node->type);
    ensure_tv(self, &node->binary_op.left->type);
    ensure_tv(self, &node->binary_op.right->type);
    ast_node    *left = node->binary_op.left, *right = node->binary_op.right;
    char const  *op          = str_cstr(&node->binary_op.op->symbol.name);

    tl_monotype *struct_type = null;

    tl_monotype_substitute(self->arena, left->type->type, self->subs, null);
    tl_monotype_substitute(self->arena, right->type->type, self->subs, null);

    // handle -> vs . access
    if (0 == strcmp("->", op)) {

        // FIXME: it should be an error if inference completes and struct access has never checked
        // field names being valid. Possibly do this check in a later phase rather than here.

        // if type is not a constructor instance, all we can assert is that the left side must be a
        // pointer
        if (check_is_pointer(self, left->type, left)) return 1;
        ensure_symbol_type_from_env(self, left);

        if (tl_monotype_is_ptr(left->type->type)) {
            struct_type = tl_monotype_ptr_target(left->type->type);
        } else {
            tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
            tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
            if (constrain_pm(self, left->type, ptr, node)) return 1;
            struct_type = weak;
        }

    } else {
        ensure_symbol_type_from_env(self, left);
        struct_type = (tl_monotype *)left->type->type;
    }

    // Note: must substitute to resolve type of chained field access, eg: foo.bar.baz
    tl_monotype_substitute(self->arena, struct_type, self->subs, null);

    if (tl_monotype_is_type_literal(struct_type)) {
        // enum access is through a type literal
        struct_type = struct_type->literal;
    }

    if (tl_monotype_is_inst(struct_type)) {
        // Note: this handling of nfas supports terms like: `obj.fun_ptr()` where a field called
        // fun_ptr is a function pointer.
        ast_node *nfa = null;
        if (ast_node_is_nfa(right)) {
            nfa   = right;
            right = right->named_application.name;
        }
        ensure_tv(self, &right->type);
        if (ast_node_is_symbol(right)) {
            str                       field_name = right->symbol.name;
            tl_type_constructor_inst *inst       = struct_type->cons_inst;
            i32 found = tl_monotype_type_constructor_field_index(struct_type, field_name);

            dbg(self, "searched struct '%s' field name %s", str_cstr(&inst->def->name),
                str_cstr(&field_name));

            if (found != -1) {
                if (!inst->args.size) {
                    // empty struct
                    if (constrain_pm(self, right->type, struct_type, node)) return 1;
                    if (constrain_pm(self, node->type, struct_type, node)) return 1;
                    if (constrain(self, node->type, right->type, node)) return 1;
                    goto end_struct_access_op;
                }

                if ((u32)found >= inst->args.size) fatal("out of range");
                tl_monotype *field_type = inst->args.v[found];
                if (nfa) {
                    tl_monotype *result_type;
                    if (tl_monotype_is_arrow(field_type)) {
                        result_type = tl_monotype_arrow_result(field_type);
                    } else {
                        // Field type is not yet an arrow (e.g., a type variable).
                        result_type = tl_monotype_create_fresh_tv(self->subs);
                    }
                    // right = nfa's name
                    if (constrain_pm(self, right->type, field_type, node)) return 1;
                    if (constrain_pm(self, nfa->type, result_type, node)) return 1;
                    if (constrain_pm(self, node->type, result_type, node)) return 1;
                } else {
                    if (constrain_pm(self, right->type, field_type, node)) return 1;
                    if (constrain_pm(self, node->type, field_type, node)) return 1;
                    if (constrain(self, node->type, right->type, node)) return 1;
                }
            } else {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_field_not_found, .node = right}));
                return 1;
            }

        } else {
            // not a symbol
            fatal("unreachable");
        }
    }

    else {
        // struct type is not a type constructor
        dbg(self, "warning: infer struct access without a struct type");
    }

end_struct_access_op:
    // always substitute operands immediately
    tl_polytype_substitute(self->arena, node->binary_op.left->type, self->subs);
    tl_polytype_substitute(self->arena, node->binary_op.right->type, self->subs);
    return 0;
}

static void maybe_handle_null(tl_infer *self, ast_node *node) {
    // Note: special case: if `null` appears and there is no node type yet, or if it's not a Ptr, assign a
    // Ptr(tv). The reason we do this is to assist non-annotated nodes such as struct fields that are
    // initialised to null. Without this handling, Foo(ptr = Null) would need to be Foo(ptr: Ptr(T) = null).
    //
    // Note: special case: if `void` appears, we assign a fresh type variable. The transpiler will detect
    // void nodes and leave the struct field uninitialised.

    if (ast_node_is_nil(node)) {
        if (!node->type || !tl_monotype_is_ptr(node->type->type)) {
            ast_node_type_set(
              node, tl_polytype_absorb_mono(
                      self->arena,
                      tl_type_registry_ptr(self->registry, tl_monotype_create_fresh_tv(self->subs))));
        }
    } else if (ast_node_is_void(node)) {
        if (!node->type || !tl_monotype_is_ptr(node->type->type)) {
            ast_node_type_set(node, tl_polytype_create_fresh_tv(self->arena, self->subs));
        }
    }
}

static int resolve_node(tl_infer *self, ast_node *node, traverse_ctx *ctx, node_position pos) {

    // Note: ctx may be null if this processing is initiated from load_toplevel()

    if (!node) return 0;

    switch (pos) {
    case npos_toplevel:
    case npos_formal_parameter:
        if (ast_node_is_symbol(node)) {
            // as a formal parameter, a symbol with a : Type annotation creates a type argument
            str name = ast_node_str(node);

            if (node->symbol.annotation) {

                // parse annotation type: parse_type needs to start at the symbol node being annotated so it
                // knows how to managed its type arguments. Then we merge the parse context's type arguments
                // into our own.

                annotation_parse_result result = parse_type_annotation(self, ctx, node);
                if (!result.parsed) {
                    expected_type(self, node);
                    return 1;
                }
                if (ctx) {
                    map_merge(&ctx->type_arguments, result.type_arguments);

                    // Also add type arguments to lexical names. Especially for free variables process.

                    str_array arr = str_map_keys(self->transient, result.type_arguments);
                    forall(i, arr) {
#if DEBUG_RESOLVE
                        dbg(self, "resolve_node: adding type argument to lexicals: '%s'",
                            str_cstr(&arr.v[i]));
#endif
                        str_hset_insert(&ctx->lexical_names, arr.v[i]);
                    }
                }

                tl_monotype *mono = result.parsed;

                (void)name;
                // If annotation is a type argument, grab its wrapped type from the type arguments map. We
                // need it to be wrapped in a literal.
                tl_monotype *found;
                if ((found = str_map_get_ptr(result.type_arguments, name))) mono = found;

                node->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, mono);
                assert(node->symbol.annotation_type);

                if (constrain_or_set(self, node, node->symbol.annotation_type)) return 1;

            } else {
                ensure_tv(self, &node->type);
            }

            if (ctx) {
                // During inference, also add lexical names.
                str_hset_insert(&ctx->lexical_names, node->symbol.name);
            }
        } else return expected_symbol(self, node);

        update_env(self, ctx, node);
        break;

    case npos_function_argument:
        if (ast_node_is_symbol(node) || ast_node_is_nfa(node)) {
            if (!ctx) fatal("logic error");

            // A type literal in argument position must be wrapped in literal
            annotation_parse_result result = parse_type_annotation(self, ctx, node);
            if (result.parsed) map_merge(&ctx->type_arguments, result.type_arguments);

            if (result.parsed) {
                tl_monotype *mono = result.parsed;

                mono              = tl_monotype_create_literal(self->arena, mono);
#if DEBUG_RESOLVE
                str node_str = v2_ast_node_to_string(self->transient, node);
                str mono_str = tl_monotype_to_string(self->transient, mono);
                dbg(self, "npos_function_argument %s : %s", str_cstr(&node_str), str_cstr(&mono_str));
#endif
                if (constrain_or_set(self, node, tl_polytype_absorb_mono(self->arena, mono))) return 1;

            } else {
                ensure_tv(self, &node->type);
                update_env(self, ctx, node);
            }
        }

        else if (ast_node_is_binary_op_struct_access(node)) {
            return infer_struct_access(self, node);
        }

        maybe_handle_null(self, node);
        break;

    case npos_let_in_lhs:
        // The lhs of a let-in can be treated the same as a formal parameter.
        if (resolve_node(self, node, ctx, npos_formal_parameter)) return 1;
        break;

    case npos_assign_lhs:
        if (reject_type_literal(self, node)) return 1;
        // Note: do not add symbol to env from this position: could be a generic re-use with prior type
        // information.

        // Support annotations on lhs of assignments, such as field names
        if (ast_node_is_symbol(node) && node->symbol.annotation) {
            if (!ctx) fatal("logic error");

            annotation_parse_result result = parse_type_annotation(self, ctx, node->symbol.annotation);
            if (result.parsed) map_merge(&ctx->type_arguments, result.type_arguments);

            if (result.parsed) {
                if (constrain_or_set(self, node, tl_polytype_absorb_mono(self->arena, result.parsed)))
                    return 1;
            }
        }

        else if (ast_node_is_binary_op_struct_access(node)) {
            return infer_struct_access(self, node);
        }

        break;

    case npos_field_name:
        if (reject_type_literal(self, node)) return 1;
        // Note: do not add symbol node to env from this position: field names. Rather,
        // ensure any existing non-type-variable type is replaced by a new type variable.
        if (node->type && !tl_monotype_is_tv(node->type->type)) {
            ast_node_type_set(node, tl_polytype_create_fresh_tv(self->arena, self->subs));
        }
        break;

    case npos_operand: {
        // Accept type literal in operand position
        if (!ctx) fatal("logic error");

        if (ast_node_is_binary_op_struct_access(node)) {
            return infer_struct_access(self, node);
        }

        annotation_parse_result result = parse_type_annotation(self, ctx, node);
        if (result.parsed) map_merge(&ctx->type_arguments, result.type_arguments);

        if (result.parsed) {
            tl_monotype *parsed = tl_monotype_create_literal(self->arena, result.parsed);
            if (constrain_or_set(self, node, tl_polytype_absorb_mono(self->arena, parsed))) return 1;
        } else {
            maybe_handle_null(self, node);
        }
        update_env(self, ctx, node);
    } break;

    case npos_let_in_rhs:
    case npos_assign_rhs:
        if (reject_type_literal(self, node)) return 1;
        maybe_handle_null(self, node);

        // Note: ensure a fresh type for any node in rhs position, because it could be a generic name.
        ensure_tv(self, &node->type);
        update_env(self, ctx, node);
        break;
    }

    ensure_tv(self, &node->type);

#if DEBUG_RESOLVE
    if (ast_node_is_symbol(node)) {
        str name = ast_node_str(node);
        str tmp  = tl_polytype_to_string(self->transient, node->type);

        dbg(self, "resolve_node '%s' : %s", str_cstr(&name), str_cstr(&tmp));
    }

    str node_str = v2_ast_node_to_string(self->transient, node);
    str mono_str = tl_monotype_to_string(self->transient, node->type->type);
    dbg(self, "resolve_node pos %i:  %s : %s", pos, str_cstr(&node_str), str_cstr(&mono_str));
#endif

    return 0;
}

static int is_type_literal(tl_infer *self, traverse_ctx const *ctx, ast_node const *node) {
    tl_type_registry_parse_type_ctx parse_ctx;
    tl_monotype *mono = tl_type_registry_parse_type_out_ctx(self->registry, node, self->transient,
                                                            ctx->type_arguments, &parse_ctx);
    return !!mono;
}

static int check_type_assertion(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, traverse_ctx->type_arguments);

    tl_monotype *type =
      tl_type_registry_parse_type_with_ctx(self->registry, node->type_assertion.annotation, &parse_ctx);

    if (resolve_node(self, node->type_assertion.name, traverse_ctx, npos_operand)) {
        dbg(self, "assert resolve node failed");
        return 1;
    }
    tl_polytype *name_type = node->type_assertion.name->type;
    if (!tl_polytype_is_concrete(name_type)) {
        tl_polytype_substitute(self->arena, name_type, self->subs);
        if (!tl_polytype_is_concrete(name_type)) {
            log_subs(self);
            return unresolved_type_error(self, node->type_assertion.name);
        }
    }
    if (constrain_pm(self, node->type_assertion.name->type, type, node)) {
        dbg(self, "assert constrain failed");
        return 1;
    }
    ast_node_type_set(node, tl_polytype_nil(self->arena, self->registry));
    return 0;
}

int is_union_struct(tl_infer *self, str name);

// ============================================================================
// Type Constraint Generation
// ============================================================================

static int infer_traverse_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (null == node) return 0;

    // infer_ctx *ctx = traverse_ctx->user;

#if DEBUG_RESOLVE
    str node_str = v2_ast_node_to_string(self->transient, node);
    dbg(self, "infer_traverse_cb: %s:  %s", ast_tag_to_string(node->tag), str_cstr(&node_str));
#endif

    switch (node->tag) {

    case ast_type_assertion:
        //
        ast_node_type_set(node, tl_polytype_nil(self->arena, self->registry));
        break;

    case ast_nil:
        // tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
        return infer_nil(self, node);

    case ast_void:
        // else handled by maybe_handle_null()
        return infer_void(self, traverse_ctx, node);

    case ast_string: return infer_literal_type(self, node, tl_type_registry_ptr_char);
    case ast_char:   return infer_literal_type(self, node, tl_type_registry_char);
    case ast_f64:    return infer_literal_type(self, node, tl_type_registry_float);
    case ast_i64:    return infer_literal_type(self, node, tl_type_registry_int);
    case ast_u64: // FIXME unsigned
        return infer_literal_type(self, node, tl_type_registry_int);
    case ast_bool:      return infer_literal_type(self, node, tl_type_registry_bool);
    case ast_body:      return infer_body(self, node);
    case ast_case:      return infer_case(self, traverse_ctx, node);
    case ast_return:    return infer_return(self, traverse_ctx, node);

    case ast_binary_op: return infer_binary_op(self, traverse_ctx, node);
    case ast_unary_op:  return infer_unary_op(self, traverse_ctx, node);
    case ast_let_in:    return infer_let_in(self, traverse_ctx, node);

    case ast_symbol:
        // When resolving a symbol, we need to know what context it's in. This is specified by its parent
        // node, and is communicated via the `node_pos` field in traverse_ctx.
        if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) return 1;
        assert(node->type);
        break;

    case ast_named_function_application: return infer_named_function_application(self, traverse_ctx, node);
    case ast_lambda_function_application:
        return infer_lambda_function_application(self, traverse_ctx, node);
    case ast_lambda_function:      return infer_lambda_function(self, traverse_ctx, node);
    case ast_if_then_else:         return infer_if_then_else(self, node);

    case ast_tuple:                return infer_tuple(self, node);

    case ast_user_type_definition: break;

    case ast_assignment:           return infer_assignment(self, traverse_ctx, node);

    case ast_reassignment:
    case ast_reassignment_op:      return infer_reassignment(self, traverse_ctx, node);

    case ast_continue:
        // use 'any' for continue so it can unify with any other conditional arm
        return infer_continue(self, node);

    case ast_while:        return infer_while(self, node);

    case ast_let:          // intentionally not processed
    case ast_hash_command:
    case ast_arrow:
    case ast_ellipsis:
    case ast_eof:
    case ast_type_alias:   break;
    }

    return 0;
}

static name_and_type make_instance_key(str generic_name, tl_monotype *arrow) {
    return (name_and_type){.name_hash = str_hash64(generic_name), .type_hash = tl_monotype_hash64(arrow)};
}

static str *instance_lookup(tl_infer *self, name_and_type *key) {
    return map_get(self->instances, key, sizeof *key);
}

static str *instance_lookup_arrow(tl_infer *self, str generic_name, tl_monotype *arrow) {
    if (!tl_monotype_is_concrete(arrow)) return null;

    // de-duplicate instances: hashes give us structural equality (barring hash collisions), which we need
    // because types are frequently cloned.
    name_and_type key = make_instance_key(generic_name, arrow);
    return instance_lookup(self, &key);
}

static int instance_name_exists(tl_infer *self, str instance_name) {
    // NB: here, the set is keyed by _instance_ name, not generic name.
    return str_hset_contains(self->instance_names, instance_name);
}

static void instance_add(tl_infer *self, name_and_type *key, str instance_name) {
    map_set(&self->instances, key, sizeof *key, &instance_name);
    str_hset_insert(&self->instance_names, instance_name);
}

static str specialize_type_constructor_(tl_infer *self, str name, tl_monotype_sized args,
                                        tl_polytype **out_type, hashmap **seen) {
    if (out_type) *out_type = null;

    // do not specialize if it's an enum
    {
        ast_node *utd = toplevel_get(self, name);
        if (utd && ast_node_is_enum_def(utd)) return str_empty();
    }

    if (1) {
        name_and_type key = {.name_hash = str_hash64(name), .type_hash = tl_monotype_sized_hash64(0, args)};
        if (hset_contains(*seen, &key, sizeof key)) return str_empty();
        hset_insert(seen, &key, sizeof key);
    }

    // To keep track of monotypes that are recursive references to the type being specialized.
    tl_monotype_ptr_array recur_refs = {.alloc = self->transient};

    // specialize args first
    forall(i, args) {
        if (tl_monotype_is_inst(args.v[i]) && !tl_monotype_is_inst_specialized(args.v[i])) {
            tl_polytype *poly         = null;
            str          generic_name = args.v[i]->cons_inst->def->generic_name;

            // Do not recurse: fixup after
            if (str_eq(name, generic_name)) {
                {
                    tl_monotype **_t = &args.v[i];
                    array_push(recur_refs, _t);
                }
                continue;
            }

            // // Do not recurse into pointer target: fixup after
            if (tl_monotype_is_ptr(args.v[i])) {
                tl_monotype *target = tl_monotype_ptr_target(args.v[i]);
                if (tl_monotype_is_inst(target) && str_eq(name, target->cons_inst->def->generic_name)) {
                    {
                        tl_monotype **_t = &args.v[i]->cons_inst->args.v[0];
                        array_push(recur_refs, _t);
                    }
                    continue;
                }
            }

            (void)specialize_type_constructor_(self, generic_name, args.v[i]->cons_inst->args, &poly, seen);
            if (poly) {
                // Note: it's a runtime error if poly is not concrete
                args.v[i] = tl_polytype_concrete(poly);
            }
        }
    }

    str                             out_str   = str_empty();
    str                             name_inst = next_instantiation(self, name); // may be cancelled later
    tl_type_registry_specialize_ctx inst_ctx =
      tl_type_registry_specialize_begin(self->registry, name, name_inst, args);

    if (!inst_ctx.specialized) goto cancel;
    if (!tl_monotype_is_inst(inst_ctx.specialized)) fatal("runtime error");

    name_and_type key      = make_instance_key(name, inst_ctx.specialized);
    str          *existing = instance_lookup(self, &key);
    if (existing) {
        tl_polytype *poly = tl_type_env_lookup(self->env, *existing);
        if (out_type) *out_type = poly;
        out_str = *existing;

        goto cancel;
    }

    // Look up generic type using the generic_name field, not the name parameter, because the latter may be
    // a type alias.
    ast_node *utd = toplevel_get(self, inst_ctx.specialized->cons_inst->def->generic_name);
    if (!utd) goto cancel;

    instance_add(self, &key, name_inst);

    utd = ast_node_clone(self->arena, utd);
    ast_node_name_replace(utd->user_type_def.name, name_inst);
    utd->type = tl_polytype_absorb_mono(self->arena, inst_ctx.specialized);
    toplevel_add(self, name_inst, utd);
    tl_type_env_insert(self->env, name_inst, utd->type);
    array_push(self->synthesized_nodes, utd);

    assert(tl_monotype_is_inst_specialized(utd->type->type));
    if (out_type) *out_type = utd->type; // Note: this helps the transpiler

    // fixup recur refs
    forall(i, recur_refs) {
        *recur_refs.v[i] = utd->type->type;
    }
    array_free(recur_refs);

    tl_type_registry_specialize_commit(self->registry, inst_ctx);
    return name_inst;

cancel:
    cancel_last_instantiation(self);
    return out_str;
}

static str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args,
                                       tl_polytype **out_type) {

    hashmap *seen = hset_create(self->transient, 8);
    str      out  = specialize_type_constructor_(self, name, args, out_type, &seen);
    return out;
}

int is_union_struct(tl_infer *self, str name) {
    ast_node *utd = toplevel_get(self, name);
    if (utd && ast_node_is_union_def(utd)) return 1;
    return 0;
}

static int specialize_value_arguments(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx,
                                      ast_node *node, tl_monotype_sized expected_types);

static int specialize_user_type(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // divert if type constructor application is actually a type literal
    if (0 == type_literal_specialize(self, node)) return 0;

    if (!ast_node_is_nfa(node)) return 0;

    str               name      = node->named_application.name->symbol.name;

    tl_monotype_array arr       = {.alloc = self->transient};
    tl_monotype_sized arr_sized = {0};

    // Check if type being constructed is concrete. If so, we want to take its arguments' concrete types
    // rather than instantiate into new type variables.
    tl_polytype *existing = tl_type_registry_get(self->registry, name);
    if (existing && tl_polytype_is_concrete(existing)) {
        assert(tl_monotype_is_inst(existing->type));

        arr_sized = existing->type->cons_inst->args;

        // If name is a type alias pointing to a concrete type, we want the transpiler to ignore the alias
        // name, and act as if the alias' target was referenced directly. This ensures the same type is used
        // in the generated C code, allowing variables to be assignable.
        if (tl_type_registry_is_type_alias(self->registry, name)) {
            name = existing->type->cons_inst->def->generic_name;
        }

    } else if (is_union_struct(self, name)) {
        // For unions, get args from the node's inferred type, not AST arguments.
        // Union constructions pass only one variant value, but specialization
        // needs all variant types from the inferred type.
        if (node->type && tl_monotype_is_inst(node->type->type)) {
            tl_monotype *mono = node->type->type;
            tl_monotype_substitute(self->arena, mono, self->subs, null);
            arr_sized = mono->cons_inst->args;
        } else {
            return 0; // Type not ready yet
        }
    } else {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {

            tl_monotype *type_id = null;
            if ((type_id = tl_type_registry_parse_type(self->registry, arg))) {
                // a literal type
                {
                    tl_monotype *_t = tl_monotype_create_literal(self->arena, type_id);
                    array_push(arr, _t);
                }
                continue;
            }

            tl_monotype *mono = null;
            if (!tl_polytype_is_concrete(arg->type)) {
                mono = tl_polytype_instantiate(self->arena, arg->type, self->subs);
                tl_monotype_substitute(self->arena, mono, self->subs, null);
            } else {
                mono = arg->type->type;
            }

            array_push(arr, mono);
        }

        assert(arr.size == node->named_application.n_arguments);
        arr_sized = (tl_monotype_sized)array_sized(arr);
    }

    tl_polytype *special_type = null;
    str          name_inst    = specialize_type_constructor(self, name, arr_sized, &special_type);
    if (str_is_empty(name_inst)) return 0;

    // update callsite
    ast_node_name_replace(node->named_application.name, name_inst);
    ast_node_set_is_specialized(node);
    if (special_type) {
        // fprintf(stderr, "specialize_user_type: replacing node type.\n");

        assert(tl_monotype_is_inst_specialized(special_type->type));

        // Note: For type constructors being specialized, we must always override the node type.
        ast_node_type_set(node, special_type);
    }

    // Specialize function pointer arguments in struct initialization.
    // This must be done for both generic and non-generic type constructors,
    // as function pointer arguments need to reference specialized function names.
    if (node->named_application.n_arguments > 0) {
        if (specialize_value_arguments(self, null, traverse_ctx, node, arr_sized)) {
            return 1;
        }
    }

    return 0;
}

static ast_node *get_infer_target(ast_node *node) {
    if (ast_node_is_let(node) || ast_node_is_lambda_function(node)) {
        return node;
    }

    else if (ast_node_is_let_in(node)) {
        return node->let_in.value;
    }

    else if (ast_node_is_symbol(node)) {
        return null;
    }

    return null;
}

static int  specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node);

static void toplevel_name_replace(ast_node *node, str name_replace) {
    if (ast_node_is_let(node)) {
        ast_node_name_replace(node->let.name, name_replace);
        ast_node_set_is_specialized(node);
    } else if (ast_node_is_let_in_lambda(node)) {
        ast_node_name_replace(node->let_in.name, name_replace);
    } else if (ast_node_is_symbol(node)) {
        // no body
        ;
    } else {
        fatal("logic error");
    }
}

static void specialized_add_to_env(tl_infer *self, str inst_name, tl_monotype *mono) {
    // add to type environment
    if (!tl_monotype_is_concrete(mono)) {
        // Note: functions like c_malloc etc will not have concrete types but still need to exist in the
        // environment.
        str arrow_str = tl_monotype_to_string(self->transient, mono);
        dbg(self, "note: adding non-concrete type to environment: '%s' : %s", str_cstr(&inst_name),
            str_cstr(&arrow_str));
    }
    tl_type_env_insert_mono(self->env, inst_name, mono);
}

static void do_apply_subs(void *ctx, ast_node *node);

static void apply_subs_to_ast_node(tl_infer *self, ast_node *node) {
    ast_node_dfs(self, node, do_apply_subs);
}

static int post_specialize(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *special,
                           tl_monotype *callsite) {
    // Do this after creating a specialised function
    ast_node *infer_target = get_infer_target(special);
    if (infer_target) {
        // set result type into traverse_ctx
        if (callsite) {
            tl_monotype *result_type  = tl_monotype_arrow_result(callsite);
            traverse_ctx->result_type = result_type;
        }
        if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) {
            dbg(self, "note: post_specialize failed infer");
            return 1;
        }
        // Apply substitutions to AST before specialization, so types are concrete
        apply_subs_to_ast_node(self, infer_target);
        if (traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb)) {
            dbg(self, "note: post_specialize failed specialize");
            return 1;
        }
    }
    return 0;
}

static void add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow);

static str  specialize_arrow(tl_infer *self, traverse_ctx *traverse_ctx, str name, tl_monotype *arrow) {

    // 1. Check if already specialized
    if (instance_name_exists(self, name)) return name;

    // 2. Check cache for this name+type combination
    str *found = instance_lookup_arrow(self, name, arrow);
    if (found) return *found;

    // 3. Create unique instance name(e.g., "identity_0")
    name_and_type key       = make_instance_key(name, arrow);
    str           inst_name = next_instantiation(self, name);
    instance_add(self, &key, inst_name);

    // 4. Clone generic function's AST
    ast_node *generic_node = clone_generic_for_arrow(self, toplevel_get(self, name), arrow, inst_name);

    // 5. Add to environment and toplevel
    specialized_add_to_env(self, inst_name, arrow);
    toplevel_add(self, inst_name, generic_node);
    dbg(self, "toplevel_add: %s", str_cstr(&inst_name));

    // 6. CRITICAL: Process the specialized function body
    ast_node *special = toplevel_get(self, inst_name);
    if (post_specialize(self, traverse_ctx, special, arrow)) return str_empty();
    return inst_name;
}

static int specialize_arrow_with_name(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx,
                                      ast_node *fun_name_node, tl_monotype *callsite) {
    (void)ctx;
    if (!tl_monotype_is_arrow(callsite)) return 0;

    str instance_name = specialize_arrow(self, traverse_ctx, ast_node_str(fun_name_node), callsite);
    if (str_is_empty(instance_name)) return 1;
    ast_node_name_replace(fun_name_node, instance_name);
    return 0;
}

static int specialize_operand(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // Here we handle function pointers in operand positions. When this is called after the function being
    // pointed to has been specialised, the arrow types will be concrete. We use those types to look up
    // (using specialize_arrow) the specialised version and replace the symbol name with the specialised
    // name. This ensures the transpiler refers to an existant concrete function rather than the generic
    // template.

    tl_polytype *value_type = node->type;

    // Note: important: need to substitute to ensure type is concrete if possible
    tl_polytype_substitute(self->arena, value_type, self->subs);

    if (!value_type || !tl_monotype_is_arrow(value_type->type)) return 0;
    if (!tl_polytype_is_concrete(value_type)) return 0;
    if (!ast_node_is_symbol(node)) return 0;

    str value_name = ast_node_str(node);
    str inst_name  = specialize_arrow(self, traverse_ctx, value_name, value_type->type);
    if (str_is_empty(inst_name)) return 0;
    ast_node_name_replace(node, inst_name);
    return 0;
}

static int specialize_let_in(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // Here we handle let fptr = id in ... function pointers.
    assert(ast_node_is_let_in(node));
    tl_polytype *name_type = node->let_in.name->type;

    if (!name_type || !tl_polytype_is_concrete(name_type)) return 0;
    return specialize_operand(self, traverse_ctx, node->let_in.value);
}

static int specialize_reassignment(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_assignment(node));
    return specialize_operand(self, traverse_ctx, node->assignment.value);
}

static int specialize_case(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_case(node));

    // Handle tagged union case: specialize variant types in conditions
    if (node->case_.is_union) {
        forall(i, node->case_.conditions) {
            ast_node *cond = node->case_.conditions.v[i];

            // Skip nil condition (else arm)
            if (ast_node_is_nil(cond)) continue;

            // Condition should be a symbol with annotation_type set by infer_case
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation_type) continue;

            tl_monotype *variant_type = cond->symbol.annotation_type->type;

            // Handle pointer types (for mutable case bindings)
            tl_monotype *inner_type = variant_type;
            if (tl_monotype_is_ptr(variant_type)) {
                inner_type = tl_monotype_ptr_target(variant_type);
            }

            // If the variant type is a generic inst that needs specialization
            if (tl_monotype_is_inst(inner_type) && !tl_monotype_is_inst_specialized(inner_type)) {
                str               generic_name = inner_type->cons_inst->def->generic_name;
                tl_monotype_sized args         = inner_type->cons_inst->args;
                tl_polytype      *special_type = null;

                str inst_name = specialize_type_constructor(self, generic_name, args, &special_type);
                if (!str_is_empty(inst_name) && special_type) {
                    // Update the annotation_type with the specialized type
                    if (tl_monotype_is_ptr(variant_type)) {
                        tl_monotype *new_ptr =
                          tl_type_registry_ptr(self->registry, tl_polytype_concrete(special_type));
                        cond->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, new_ptr);
                    } else {
                        cond->symbol.annotation_type = special_type;
                    }
                    ast_node_type_set(cond, cond->symbol.annotation_type);

                    // Update the annotation node's name to the specialized name
                    if (cond->symbol.annotation && ast_node_is_symbol(cond->symbol.annotation)) {
                        ast_node_name_replace(cond->symbol.annotation, inst_name);
                    }
                }
            }
        }
        return 0;
    }

    // Handle binary predicate case (non-union)
    if (!node->case_.binary_predicate) return 0;

    ast_node *predicate = node->case_.binary_predicate;
    if (!ast_node_is_symbol(predicate)) return 0; // FIXME: what about lambdas?
    if (!node->case_.conditions.size) return 0;

    tl_polytype *pred_arrow =
      make_binary_predicate_arrow(self, traverse_ctx, node->case_.expression, node->case_.conditions.v[0]);

    str predicate_name = ast_node_str(predicate);
    str inst_name      = specialize_arrow(self, traverse_ctx, predicate_name, pred_arrow->type);
    if (str_is_empty(inst_name)) return 0;
    ast_node_name_replace(predicate, inst_name);

    return 0;
}

static void specialize_type_alias(tl_infer *self, ast_node *node) {
    // if target of type alias is a generic instantiation, ensure it it is properly specialized.
    // load_toplevel will have generalized the target type and inserted it into the alias registry.
    // we will need to specialize it and replace it.
    assert(ast_node_is_type_alias(node));

    ast_node    *target = node->type_alias.target;
    tl_monotype *parsed = tl_type_registry_parse_type(self->registry, target);
    if (tl_monotype_is_inst(parsed) && tl_monotype_is_concrete(parsed)) {
        str name = toplevel_name(node);
        str tmp  = tl_monotype_to_string(self->transient, parsed);
        dbg(self, "specialize_type_alias: %s = %s", str_cstr(&name), str_cstr(&tmp));
        tl_type_registry_type_alias_insert(self->registry, name,
                                           tl_polytype_absorb_mono(self->arena, parsed));
        assert(tl_polytype_is_concrete(tl_type_registry_get(self->registry, name)));
    } else if (tl_monotype_is_inst(parsed)) {
        str name = toplevel_name(node);
        str tmp  = tl_monotype_to_string(self->transient, parsed);
        dbg(self, "specialize_type_alias: not concrete: %s = %s", str_cstr(&name), str_cstr(&tmp));
    }
}

static int is_toplevel_function_name(tl_infer *self, ast_node *arg) {
    str       arg_name = ast_node_str(arg);
    ast_node *top      = toplevel_get(self, arg_name);
    if (!top) return 0;
    if (top->type && !tl_monotype_is_arrow(top->type->type)) return 0;
    return 1;
}

static int specialize_value_arguments(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx,
                                      ast_node *node, tl_monotype_sized expected_types) {
    // Visits arguments to check for symbols referring to toplevel functions.
    // When found, specializes the function according to the expected type.

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    u32                i = 0;
    while ((arg = ast_arguments_next(&iter))) {

        if (ast_node_is_assignment(arg))
            if (specialize_reassignment(self, traverse_ctx, arg)) return 1;

        if (!ast_node_is_symbol(arg)) goto next;
        if (!is_toplevel_function_name(self, arg)) goto next;
        if (i >= expected_types.size) break;
        if (specialize_arrow_with_name(self, ctx, traverse_ctx, arg, expected_types.v[i])) return 1;

    next:
        ++i;
    }
    return 0;
}

static int specialize_arguments(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx, ast_node *node,
                                tl_monotype *arrow) {
    // Visits arguments used in node (function call arguments, etc) to check for symbols which refer to
    // toplevel functions. When found, that function is specialized according to the callsite's expected
    // type.

    tl_monotype_sized app_args = tl_monotype_arrow_args(arrow)->list.xs;
    return specialize_value_arguments(self, ctx, traverse_ctx, node, app_args);
}

// ============================================================================
// Generic Function Specialization
// ============================================================================

static int specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    infer_ctx *ctx = traverse_ctx->user;
    if (ast_node_is_nfa(node)) {
        str name = ast_node_str(node->named_application.name);

        dbg(self, "specialize_applications_cb: enter '%s'", str_cstr(&name));
    }

    // Important: resolve the node, so that traverse_ctx is properly updated, including type arguments. For
    // example, node could be a symbol which is a formal argument that carries an annotation referring to a
    // type variable.
    if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) {
        return 1;
    }

    // check for nullary type constructors
    if (ast_node_is_symbol(node)) return specialize_user_type(self, traverse_ctx, node);
    // check for let_in nodes and assignments
    if (ast_node_is_let_in(node)) return specialize_let_in(self, traverse_ctx, node);
    if (ast_node_is_assignment(node)) return specialize_reassignment(self, traverse_ctx, node);
    if (ast_node_is_case(node)) return specialize_case(self, traverse_ctx, node);

    // check for type assertion
    if (ast_node_is_type_assertion(node)) return check_type_assertion(self, traverse_ctx, node);

    int is_anon = ast_node_is_lambda_application(node);

    // or else the remainder of this function handles nfas and anon lambda applications
    if (!ast_node_is_nfa(node) && !ast_node_is_lambda_application(node)) return 0;

    if (ast_node_is_specialized(node)) return 0;

    tl_polytype *callsite = null;
    if (!is_anon) {
        str name = ast_node_str(node->named_application.name);

        if (self->verbose) {
            str tmp = v2_ast_node_to_string(self->transient, node);
            dbg(self, "specialize_applications_cb: nfa '%s'", str_cstr(&tmp));
            str_deinit(self->transient, &tmp);
        }

        // do not process a second time
        if (ast_node_is_specialized(node)) return 0;

        // do not process intrinsic calls or their arguments
        if (is_intrinsic(name)) return 0;

        // may be too early, e.g. for pointers
        // but type aliases and type constructors are always available
        if (!toplevel_get(self, name) && !tl_type_registry_exists(self->registry, name)) {
            dbg(self, "specialize_applications_cb: skipping '%s'", str_cstr(&name));

            return 0; // too early
        }

        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) {
            // check if it's a type alias or type constructor in the registry
            type = tl_type_registry_get(self->registry, name);
            if (!type) return 0; // mutual recursion or variable holding function pointer
        }

        // divert if this is a type constructor
        if (tl_polytype_is_type_constructor(type)) return specialize_user_type(self, traverse_ctx, node);

        // remember this callsite is specialized
        ast_node_set_is_specialized(node);

        // Important: use _with variant to copy free variables info to the arrow, which is added to the
        // environment further down.
        callsite = make_arrow_with(self, traverse_ctx, node, type);
        if (!callsite) {
            return 1;
        }

        if (self->verbose) {
            str app_str = tl_polytype_to_string(self->transient, callsite);
            dbg(self, "specialize application: callsite '%.*s' arrow: %.*s", str_ilen(name), str_buf(&name),
                str_ilen(app_str), str_buf(&app_str));
        }

        // try to specialize
        if (specialize_arrow_with_name(self, ctx, traverse_ctx, node->named_application.name,
                                       callsite->type)) {
            dbg(self, "note: failed to specialize '%s'", str_cstr(&name));
            return 1;
        }
        // and recurse over any arguments which are toplevel functions
        if (specialize_arguments(self, ctx, traverse_ctx, node, callsite->type)) {
            dbg(self, "note: failed to specialize arguments of '%s'", str_cstr(&name));
            return 1;
        }

        dbg(self, "specialize_applications_cb done: nfa '%s'",
            str_cstr(&node->named_application.name->symbol.name));

    } else {
        dbg(self, "specialize_applications_cb: anon");
        callsite = make_arrow(self, traverse_ctx, ast_node_sized_from_ast_array(node), node, 0);

        concretize_params(self, node, callsite->type);
        if (post_specialize(self, traverse_ctx, node->lambda_application.lambda, callsite->type)) {
            return 1;
        }
        if (specialize_arguments(self, ctx, traverse_ctx, node, callsite->type)) {
            return 1;
        }
    }

    return 0;
}

// --

static str next_variable_name(tl_infer *, str);

// Performs alpha-conversion on the AST to ensure all bound variables have globally unique names while
// preserving lexical scope. This simplifies later passes by removing name collision concerns.

static void rename_let_in(tl_infer *self, ast_node *node, rename_variables_ctx *ctx) {
    // For toplevel definitions, rename them and keep them in lexical scope.
    if (!ast_node_is_let_in(node)) return;

    str name = node->let_in.name->symbol.name;
    if (is_c_symbol(name)) return;

    str newvar = next_variable_name(self, name);
    ast_node_name_replace(node->let_in.name, newvar);

#if DEBUG_RENAME
    dbg(self, "rename %.*s => %.*s", str_ilen(node->let_in.name->symbol.original),
        str_buf(&node->let_in.name->symbol.original), str_ilen(node->let_in.name->symbol.name),
        str_buf(&node->let_in.name->symbol.name));
#endif

    str_map_set(&ctx->lex, name, &newvar);
}

static hashmap *rename_function_params(tl_infer *self, ast_node *node, rename_variables_ctx *ctx,
                                       int level) {
    hashmap           *save = map_copy(ctx->lex);

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *param;
    while ((param = ast_arguments_next(&iter))) {
        assert(ast_node_is_symbol(param));
        str name   = param->symbol.name;
        str newvar = next_variable_name(self, name);
        ast_node_name_replace(param, newvar);
        str_map_set(&ctx->lex, name, &newvar);
        rename_variables(self, param, ctx, level + 1);
    }

    return save;
}

// ============================================================================
// Variable Renaming (Alpha Conversion)
// ============================================================================

static void rename_variables(tl_infer *self, ast_node *node, rename_variables_ctx *ctx, int level) {
    // level should be 0 on entry. It is used to recognize toplevel let nodes which assign static values
    // that must remain in lexical scope throughout the program.

    if (null == node) return;

    // ensure all types are removed: important for the post-clone rename of functions being specialized.
    ast_node_type_set(node, null);

    switch (node->tag) {

    case ast_if_then_else:
        rename_variables(self, node->if_then_else.condition, ctx, level + 1);
        rename_variables(self, node->if_then_else.yes, ctx, level + 1);
        rename_variables(self, node->if_then_else.no, ctx, level + 1);
        break;

    case ast_let_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, ctx, level + 1);

        hashmap *save = null;
        str      name = node->let_in.name->symbol.name;
        if (is_c_symbol(name)) break;
        if (level) {
            // do not rename toplevel symbols again (see rename_let_in)
            str newvar = next_variable_name(self, name);

            // establish lexical scope of the let-in binding and recurse
            save = map_copy(ctx->lex);
            str_map_set(&ctx->lex, name, &newvar);

            rename_variables(self, node->let_in.name, ctx, level + 1);
        }

        rename_variables(self, node->let_in.body, ctx, level + 1);

        // restore prior scope
        if (save) {
            map_destroy(&ctx->lex);
            ctx->lex = save;
        }
    } break;

    case ast_symbol: {

        // Do not rename symbols found immediately after a struct access
        if (ctx->is_field) {
            ctx->is_field = 0;
            break;
        }

        str *found;
        if ((found = str_map_get(ctx->lex, node->symbol.name))) {
            ast_node_name_replace(node, *found);
#if DEBUG_RENAME
            dbg(self, "rename %.*s => %.*s", str_ilen(node->symbol.original),
                str_buf(&node->symbol.original), str_ilen(node->symbol.name), str_buf(&node->symbol.name));
#endif
        } else if (node->symbol.is_mangled && (found = str_map_get(ctx->lex, node->symbol.original))) {
            // name was mangled because it conflicts with a toplevel name. But lexical rename is meant to
            // take precedence over mangling to match toplevel names.
            ast_node_name_replace(node, *found);
#if DEBUG_RENAME
            dbg(self, "rename mangled %.*s => %.*s", str_ilen(node->symbol.original),
                str_buf(&node->symbol.original), str_ilen(node->symbol.name), str_buf(&node->symbol.name));
#endif
        } else {
            // a free variable, a field name, a toplevel function name, etc
        }

        // ensure renamed symbols do not carry a type
        ast_node_type_set(node, null);
        node->symbol.annotation_type = null;

        // traverse into annotation too, to support type arguments.
        // Note: keep in sync with ast_let_in arm.
        rename_variables(self, node->symbol.annotation, ctx, level + 1);
    } break;

    case ast_lambda_function: {
        hashmap *save = rename_function_params(self, node, ctx, level);
        rename_variables(self, node->lambda_function.body, ctx, level + 1);
        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_let: {
        hashmap *save = rename_function_params(self, node, ctx, level);
        rename_variables(self, node->let.body, ctx, level + 1);
        ast_node_type_set(node->let.name, null);
        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_lambda_function_application: {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, ctx, level + 1);

        // establishes scope for lambda body
        rename_variables(self, node->lambda_application.lambda, ctx, level + 1);

    } break;

    case ast_named_function_application: {
        rename_variables(self, node->named_application.name, ctx, level + 1);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;

        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, ctx, level + 1);

    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) rename_variables(self, arr.v[i], ctx, level + 1);
    } break;

    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:
        // Note: no longer rename lhs of assignment, because it is used for named arguments of type
        // constructors. However, the type must be erased, because cloning generic functions relies on
        // rename_variables to erase types.
        if (!node->assignment.is_field_name) rename_variables(self, node->assignment.name, ctx, level + 1);
        else {
            // Note: however, field names may now be annotated, so the annotations have to be processed.
            // This handles type variables in the field assignment annotation.
            if (ast_node_is_symbol(node->assignment.name))
                rename_variables(self, node->assignment.name->symbol.annotation, ctx, level + 1);

            ast_node_type_set(node->assignment.name, null);
        }

        rename_variables(self, node->assignment.value, ctx, level + 1);
        break;

    case ast_binary_op: {
        rename_variables(self, node->binary_op.left, ctx, level + 1);

        // Note: If op is a struct access operator (. or ->), signal it
        char const *op = str_cstr(&node->binary_op.op->symbol.name);
        if (is_struct_access_operator(op)) ctx->is_field = 1;
        rename_variables(self, node->binary_op.right, ctx, level + 1);
    } break;

    case ast_unary_op: rename_variables(self, node->unary_op.operand, ctx, level + 1); break;

    case ast_return:   rename_variables(self, node->return_.value, ctx, level + 1); break;

    case ast_while:
        rename_variables(self, node->while_.condition, ctx, level + 1);
        rename_variables(self, node->while_.update, ctx, level + 1);
        rename_variables(self, node->while_.body, ctx, level + 1);
        break;

    case ast_body:
        //
        forall(i, node->body.expressions) {
            rename_variables(self, node->body.expressions.v[i], ctx, level + 1);
        }
        break;

    case ast_case: {
        int is_union = node->case_.is_union;

        rename_variables(self, node->case_.expression, ctx, level + 1);
        rename_variables(self, node->case_.binary_predicate, ctx, level + 1);
        if (node->case_.conditions.size != node->case_.arms.size) fatal("runtime error");
        forall(i, node->case_.conditions) {
            hashmap *save = null;
            if (is_union && ast_node_is_symbol(node->case_.conditions.v[i])) {
                // node may be ast_nil for an else clause
                str name   = ast_node_str(node->case_.conditions.v[i]);
                str newvar = next_variable_name(self, name);

                // establish lexical scope of the union case binding
                save = map_copy(ctx->lex);
                str_map_set(&ctx->lex, name, &newvar);
            }

            rename_variables(self, node->case_.conditions.v[i], ctx, level + 1);
            rename_variables(self, node->case_.arms.v[i], ctx, level + 1);

            if (save) {
                map_destroy(&ctx->lex);
                ctx->lex = save;
            }
        }
    } break;

    case ast_type_assertion:
        //
        rename_variables(self, node->type_assertion.name, ctx, level + 1);
        break;

    case ast_hash_command:
    case ast_continue:
    case ast_string:
    case ast_char:
    case ast_nil:
    case ast_void:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_u64:
    case ast_type_alias:
    case ast_user_type_definition: break;
    }
}

static void add_free_variables_to_arrow(tl_infer *, ast_node *, tl_polytype *);

static void concretize_params(tl_infer *self, ast_node *node, tl_monotype *callsite) {
    if (ast_node_is_symbol(node)) return;

    ast_node      *body   = null;
    ast_node_sized params = {0};
    if (ast_node_is_let(node)) {
        body   = node->let.body;
        params = ast_node_sized_from_ast_array(node);
    } else if (ast_node_is_let_in_lambda(node)) {
        body   = node->let_in.value->lambda_function.body;
        params = ast_node_sized_from_ast_array(node->let_in.value);
    } else if (ast_node_is_lambda_application(node)) {
        body   = node->lambda_application.lambda->lambda_function.body;
        params = ast_node_sized_from_ast_array(node->lambda_application.lambda);
    } else {
        fatal("logic error");
    }

    // assign concrete types to parameters based on callsite arguments

    assert(tl_arrow == callsite->tag);
    assert(callsite->list.xs.size == 2);
    assert(tl_tuple == callsite->list.xs.v[0]->tag);
    tl_monotype_sized callsite_args = callsite->list.xs.v[0]->list.xs;
    assert(callsite_args.size == params.size);

    forall(i, params) {
        ast_node *param = params.v[i];
        param->type     = tl_polytype_absorb_mono(self->arena, callsite_args.v[i]);
    }

    tl_monotype *inst_result = tl_monotype_sized_last(callsite->list.xs);
    body->type               = tl_polytype_absorb_mono(self->arena, inst_result);
}

static str next_variable_name(tl_infer *self, str name) {
    char buf[64];
    if (0 == str_cmp_nc(name, "tl_", 3))
        snprintf(buf, sizeof buf, "%s_v%u", str_cstr(&name), self->next_var_name++);
    else snprintf(buf, sizeof buf, "tl_%s_v%u", str_cstr(&name), self->next_var_name++);
    return str_init(self->arena, buf);
}

static str next_instantiation(tl_infer *self, str name) {
    if (str_len(name) < 128 - 24) {
        char buf[128];
        snprintf(buf, sizeof buf, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
        return str_init(self->arena, buf);
    } else {
        size_t len = str_len(name) + 24;
        char  *buf = alloc_malloc(self->transient, len);
        snprintf(buf, len, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
        str out = str_init(self->arena, buf);
        alloc_free(self->transient, buf);
        return out;
    }
}

static void cancel_last_instantiation(tl_infer *self) {
    self->next_instantiation--;
}

static tl_polytype *make_arrow_result_type(tl_infer *self, traverse_ctx *ctx, ast_node_sized args,
                                           tl_polytype *result_type, int is_parameters) {
    if (args.size == 0 || (args.size == 1 && ast_node_is_nil(args.v[0]))) {
        // always use a tuple on the left side of arrow, even if zero elements
        tl_monotype *lhs   = tl_monotype_create_tuple(self->arena, (tl_monotype_sized){0});
        tl_monotype *rhs   = result_type ? result_type->type : null;
        tl_monotype *arrow = tl_type_registry_create_arrow(self->registry, lhs, rhs);

        {
            str str = tl_monotype_to_string(self->transient, arrow);
            dbg(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_polytype_absorb_mono(self->arena, arrow);
    }

    else {
        tl_monotype_array args_types = {.alloc = self->arena};
        array_reserve(args_types, args.size);
        forall(i, args) {
            if (resolve_node(self, args.v[i], ctx,
                             is_parameters ? npos_formal_parameter : npos_function_argument))
                return null;

            tl_monotype *mono = args.v[i]->type->type;

            // make concrete if possible
            tl_monotype_substitute(self->arena, mono, self->subs, null);
            array_push(args_types, mono);
        }

        tl_monotype *left = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(args_types));
        tl_monotype *right = null;
        if (result_type) {
            right = result_type->type;
            tl_monotype_substitute(self->arena, right, self->subs, null);
        } else {
            right = tl_type_registry_nil(self->registry);
        }

        tl_monotype *out = tl_type_registry_create_arrow(self->registry, left, right);

        {
            str str = tl_monotype_to_string(self->transient, out);
            dbg(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }

        return tl_polytype_absorb_mono(self->arena, out);
    }
}

static tl_polytype *make_arrow(tl_infer *self, traverse_ctx *ctx, ast_node_sized args, ast_node *result,
                               int is_parameters) {
    if (result) ensure_tv(self, &result->type);
    return make_arrow_result_type(self, ctx, args, result ? result->type : null, is_parameters);
}

static tl_polytype *make_arrow_with(tl_infer *self, traverse_ctx *ctx, ast_node *node, tl_polytype *type) {
    ast_arguments_iter iter = ast_node_arguments_iter(node); // not used for iter, just for args
    tl_polytype       *out  = make_arrow(self, ctx, iter.nodes, node, 0);

    if (!out) return null;
    if (tl_monotype_is_list(out->type) && tl_monotype_is_list(type->type)) {
        (out->type)->list.fvs = type->type->list.fvs;
    }
    return out;
}

static tl_polytype *make_binary_predicate_arrow(tl_infer *self, traverse_ctx *ctx, ast_node *lhs,
                                                ast_node *rhs) {

    ast_node_array args = {.alloc = self->arena};
    array_push(args, lhs);
    array_push(args, rhs);
    tl_monotype *bool_type = tl_type_registry_bool(self->registry);

    tl_polytype *bool_poly = tl_polytype_absorb_mono(self->arena, bool_type);
    tl_polytype *pred_arrow =
      make_arrow_result_type(self, ctx, (ast_node_sized)array_sized(args), bool_poly, 0);

    return pred_arrow;
}

typedef struct {
    str_array fvs;
} collect_free_variables_ctx;

static int can_be_free_variable(tl_infer *self, traverse_ctx *traverse_ctx, ast_node const *node) {
    if (!ast_node_is_symbol(node) || traverse_ctx->is_field_name) return 0;

    str name = ast_node_str(node);

    // don't collect symbols which are nullary type literals
    if (tl_type_registry_is_nullary_type(self->registry, name)) return 0;

    // don't collect symbols that start with c_
    if (0 == str_cmp_nc(name, "c_", 2)) return 0;

    // don't collect symbols that are already in lexical scope (e.g., union case bindings)
    if (str_hset_contains(traverse_ctx->lexical_names, name)) return 0;

    return 1;
}

static int collect_free_variables_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (ast_node_is_binary_op(node)) node = node->binary_op.left;
    if (!can_be_free_variable(self, traverse_ctx, node)) return 0;

    str name = ast_node_str(node);
    if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) return 1;

    collect_free_variables_ctx *ctx      = traverse_ctx->user;

    tl_polytype                *type     = tl_type_env_lookup(self->env, name);
    int                         is_arrow = type && tl_monotype_is_arrow(type->type);

    // Note: arrow types in the environment are global functions and are not free variables. Note that
    // even local let-in-lambda functions are also in the environment, but their names will never clash
    // with function names.
    if (is_arrow || traverse_ctx_is_param(traverse_ctx, name)) {
        // FIXME: arrow type may exist because of a forward declaration, but function definition may be
        // missing. Need to report an error.
        ;
    } else {
        // a free variable
        dbg(self, "collect_free_variables_cb: add '%s'", str_cstr(&name));
        str_array_set_insert(&ctx->fvs, name);
    }

    // if symbol has a type which carries fvs, we also collect those.
    if (is_arrow && !tl_polytype_is_scheme(type)) {
        str_sized type_fvs = tl_monotype_fvs(type->type);
        forall(i, type_fvs) {
            if (!traverse_ctx_is_param(traverse_ctx, type_fvs.v[i]))
                str_array_set_insert(&ctx->fvs, type_fvs.v[i]);
        }
    }

    return 0;
}

static void promote_free_variables(str_array *out, tl_monotype *in) {
    if (tl_monotype_is_list(in) || tl_monotype_is_tuple(in)) {
        // TODO: clean up tuple args handling
        forall(i, in->list.xs) promote_free_variables(out, in->list.xs.v[i]);
        forall(i, in->list.fvs) str_array_set_insert(out, in->list.fvs.v[i]);
    }
}

static void add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow) {
    // collect free variables from infer target and add to the generic's arrow type

    collect_free_variables_ctx ctx;
    ctx.fvs                    = (str_array){.alloc = self->arena};

    traverse_ctx *traverse_ctx = traverse_ctx_create(self->transient);
    traverse_ctx->user         = &ctx;
    int res                    = traverse_ast(self, traverse_ctx, node, collect_free_variables_cb);
    if (res) fatal("runtime error");

    array_shrink(ctx.fvs);
    dbg(self, "-- free variables: %u --", ctx.fvs.size);
    forall(i, ctx.fvs) {
        dbg(self, "%.*s", str_ilen(ctx.fvs.v[i]), str_buf(&ctx.fvs.v[i]));
    }

    // find any sublists with free variables and bring them to the top
    promote_free_variables(&ctx.fvs, arrow->type);

    // add free variables to arrow type
    if (ctx.fvs.size) {
        tl_monotype_absorb_fvs(arrow->type, (str_sized)sized_all(ctx.fvs));

        // sort free variables
        tl_monotype_sort_fvs(arrow->type);
    }
}

static int generic_declaration(tl_infer *self, str name, ast_node const *name_node, ast_node *node) {
    // no function body, so let's treat this as a type declaration
    if (!name_node->symbol.annotation_type) {
        expected_type(self, node);
        return 1;
    }

    // must quantify arrow types
    if (tl_monotype_is_arrow(node->symbol.annotation_type->type)) {
        tl_polytype_generalize(node->symbol.annotation_type, self->env, self->subs);
    }
    tl_type_env_insert(self->env, name, node->symbol.annotation_type);
    return 0;
}

static int infer_one(tl_infer *self, ast_node *infer_target, tl_polytype *arrow) {
    // arrow is non-null only for let nodes
    if (arrow && !ast_node_is_let(infer_target) && !ast_node_is_lambda_function(infer_target))
        fatal("logic error");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    infer_ctx    *ctx      = infer_ctx_create(self->transient);
    traverse->user         = ctx;
    if (traverse_ast(self, traverse, infer_target, infer_traverse_cb)) return 1;

    // constrain arrow result type and infer target's type
    if (arrow) {
        if (tl_polytype_is_scheme(arrow)) fatal("logic error");
        ast_node *body = null;
        if (ast_node_is_let(infer_target)) body = infer_target->let.body;
        else if (ast_node_is_lambda_function(infer_target)) body = infer_target->lambda_function.body;
        if (!body) fatal("logic error");

        if (constrain_pm(self, body->type, tl_monotype_arrow_result(arrow->type), body)) return 1;
    }
    return 0;
}

static int add_generic(tl_infer *self, ast_node *node) {
    if (!node) return 0;

    // Handle body nodes early - they contain multiple definitions (e.g., from tagged union desugaring)
    if (ast_node_is_body(node)) {
        forall(i, node->body.expressions) {
            if (add_generic(self, node->body.expressions.v[i])) return 1;
        }
        return 0;
    }

    ast_node    *infer_target = get_infer_target(node);
    ast_node    *name_node    = toplevel_name_node(node);
    tl_polytype *provisional  = name_node->symbol.annotation_type;
    str          name         = name_node->symbol.name;
    str          orig_name    = name_node->symbol.original;

    // calculate provisional type, for recursive functions
    if (ast_node_is_let(node)) {
        if (!provisional) {
            // Note: special case: force main() to have a CInt result type
            if (str_eq(name, S("main"))) {
                provisional = make_arrow_result_type(self, null, ast_node_sized_from_ast_array(node),
                                                     tl_type_registry_get(self->registry, S("CInt")), 1);
            }

            else {
                provisional =
                  make_arrow(self, null, ast_node_sized_from_ast_array(node), node->let.body, 1);
            }
        }
    } else if (ast_node_is_let_in_lambda(node)) {
        if (!provisional)
            provisional = make_arrow(self, null, ast_node_sized_from_ast_array(infer_target),
                                     node->let_in.value->lambda_function.body, 1);
    } else if (ast_node_is_symbol(node)) {
        // toplevel symbol node, e.g. for declaration of intrinsics, or forward type annotations. They will
        // take precedence to any later declarations, so let's be careful
    } else if (ast_node_is_utd(node)) {
        // already loaded from load_toplevel
        return 0;
    } else if (ast_node_is_let_in(node)) {
        if (infer_one(self, infer_target, null)) {
            dbg(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
                str_ilen(orig_name), str_buf(&orig_name));
        }

        assert(node->let_in.value->type);
        tl_type_env_insert(self->env, name, node->let_in.value->type);
        ast_node_type_set(node->let_in.name, node->let_in.value->type);
        return 0;

    } else {
        fatal("logic error");
    }

    dbg(self, "-- add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    if (!infer_target) {
        // no function body, so let's treat this as a type declaration
        return generic_declaration(self, name, name_node, node);
    }

    // ensure provisional type is not quantified. If it is, instantiate it
    if (tl_polytype_is_scheme(provisional)) {
        provisional = tl_polytype_absorb_mono(
          self->arena, tl_polytype_instantiate(self->arena, provisional, self->subs));
    }

    // add provisional type to environment (for polymorphic recursion)
    if (provisional) {
        // Note: ensure this is not quantified until after inference
        tl_type_env_insert(self->env, name, provisional);
    }

    // run inference
    if (infer_one(self, infer_target, provisional)) {
        dbg(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
            str_ilen(orig_name), str_buf(&orig_name));
        return 1;
    }

    // Must apply subs before quantifying, because we want to replace any tvs (that would otherwise be
    // quantified) with primitives if possible, or the same root of an equivalence class
    tl_type_subs_apply(self->subs, self->env);

    // get the arrow type from the annotation, or else from the result of inference
    tl_polytype *arrow = null;
    if (name_node->symbol.annotation_type) {
        arrow = name_node->symbol.annotation_type;
        // since arrow does not come from env, ensure it's fully substituted
        tl_polytype_substitute(self->arena, arrow, self->subs);
    } else {
        tl_polytype *tmp = tl_type_env_lookup(self->env, name);
        if (!tmp) fatal("runtime error");
        arrow = tmp;

        if (!arrow) fatal("runtime error");
    }

    tl_polytype_generalize(arrow, self->env, self->subs);

    // collect free variables from infer target and add to the generic's arrow type

    add_free_variables_to_arrow(self, infer_target, arrow);
    tl_type_env_insert(self->env, name, arrow);

    dbg(self, "-- done add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    return 0;
}

void missing_fv_error_cb(void *ctx, str fun, str var) {
    tl_infer *self = ctx;
    ast_node *node = toplevel_get(self, fun);
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_free_variable_not_found, .node = node, .message = var}));
}

int check_missing_free_variables(tl_infer *self) {
    return tl_type_env_check_missing_fvs(self->env, missing_fv_error_cb, self);
}

void remove_generic_toplevels(tl_infer *self) {
    str_array        names = {.alloc = self->transient};

    ast_node        *node;
    hashmap_iterator iter = {0};
    while ((node = toplevel_iter(self, &iter))) {

        str name = ast_node_str(toplevel_name_node(node));
        if (str_eq(S("main"), name)) continue;

        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("runtime error");

        if (tl_monotype_is_arrow(type->type) && tl_monotype_arrow_is_concrete(type->type)) continue;
        if (!tl_polytype_is_concrete(type)) array_push(names, name);
    }

    forall(i, names) {
        dbg(self, "remove_generic_toplevels: removing '%s'", str_cstr(&names.v[i]));
        toplevel_del(self, names.v[i]);
    }
    array_free(names);
}

void tree_shake_toplevels(tl_infer *self, ast_node const *start) {
    hashmap  *used   = tree_shake(self, start);

    str_array remove = {.alloc = self->transient};

    // Add all toplevel let-in names (globals) and type names because we now use this process to determine
    // all symbols used in the program. This helps us identify nonexistent free variables.
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_let_in(node)) {
            str name = ast_node_str(node->let_in.name);
            str_hset_insert(&used, name);

            // recurse into toplevel let-in nodes
            hashmap *recur = tree_shake(self, node);
            map_merge(&used, recur);
            map_destroy(&recur);

        } else if (ast_node_is_utd(node)) {
            str name = ast_node_str(node->user_type_def.name);
            str_hset_insert(&used, name);
        }

        // Note: special case: preserve module init functions.
        else if (ast_node_is_let(node) && is_module_init(ast_node_str(node->let.name))) {
            str_hset_insert(&used, node->let.name->symbol.name);

            // recurse into toplevel module init functions
            hashmap *recur = tree_shake(self, node);
            map_merge(&used, recur);
            map_destroy(&recur);
        }
    }

    iter = (hashmap_iterator){0};
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;

        // preserve value let-ins, but not unused let-in-lambdas (including the latter causes a test
        // failure)
        if (ast_node_is_let_in(node) && !ast_node_is_let_in_lambda(node)) continue;

        str name = toplevel_name(node);
        if (!str_hset_contains(used, name)) array_push(remove, name);
    }

    forall(i, remove) {
        dbg(self, "tree_shake_toplevels: removing '%s'", str_cstr(&remove.v[i]));
        toplevel_del(self, remove.v[i]);
    }
    array_free(remove);

    // Note: also remove any unused name in the environment
    tl_type_env_remove_unknown_symbols(self->env, used);

    map_destroy(&used);
}

static int check_main_function(tl_infer *self, ast_node *main) {
    // instantiate and infer main
    assert(ast_node_is_let(main));
    tl_polytype *type = tl_type_env_lookup(self->env, S("main"));
    if (!type) fatal("main function with no type");

    // set is_specialized
    ast_node_set_is_specialized(main);

    tl_polytype *body_type = main->let.body->type;
    if (!body_type || tl_polytype_is_scheme(body_type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_main_function_bad_type, .node = main}));
        return 1;
    }

    // remove free variables from main type if they are toplevel (e.g. lambda functions)
    str_sized *fvs = &type->type->list.fvs;
    if (tl_monotype_is_arrow(type->type)) {
        for (u32 i = 0; i < fvs->size;) {
            str fv = fvs->v[i];
            if (toplevel_get(self, fv)) array_sized_erase(*fvs, i);
            else ++i;
        }
    }

    // report errors: main must have no free variables
    int error = 0;
    forall(i, *fvs) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_unknown_symbol_in_main, .message = fvs->v[i]}));
        ++error;
    }
    return error;
}

tl_monotype *tl_infer_update_specialized_type_(tl_infer *self, tl_monotype *mono, hashmap **in_progress) {

    // Note: this function pretty definitely breaks the isolation between tl_infer and the transpiler so
    // that makes me a little bit sad. But it makes sizeof(TypeConstructor) work.

    switch (mono->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:
    case tl_weak:        break;

    case tl_literal:     {
        tl_monotype *target  = tl_monotype_literal_target(mono);
        tl_monotype *replace = tl_infer_update_specialized_type_(self, target, in_progress);
        if (replace) return tl_monotype_create_literal(self->arena, replace);
    } break;

    case tl_cons_inst: {

        int did_replace  = !tl_monotype_is_inst_specialized(mono);
        str generic_name = mono->cons_inst->def->generic_name;

        // check args recursively
        str_hset_insert(in_progress, generic_name);
        forall(i, mono->cons_inst->args) {
            tl_monotype *arg = mono->cons_inst->args.v[i];
            if (!tl_monotype_is_inst(arg) ||
                !str_hset_contains(*in_progress, arg->cons_inst->def->generic_name)) {

                tl_monotype *replace = tl_infer_update_specialized_type_(self, arg, in_progress);

                if (replace) {
                    mono->cons_inst->args.v[i] = replace;
                    did_replace                = 1;
                }
            }
        }
        str_hset_remove(*in_progress, generic_name);

        tl_polytype *replace = null;
        (void)specialize_type_constructor(self, mono->cons_inst->def->generic_name, mono->cons_inst->args,
                                          &replace);

        if (replace && !tl_monotype_is_inst_specialized(replace->type)) fatal("unreachable");

        if (replace && did_replace) {
            return replace->type;
        } else {
            return null;
        }

    } break;

    case tl_arrow:
    case tl_tuple: {
        int did_replace = 0;
        forall(i, mono->list.xs) {
            tl_monotype *replace = tl_infer_update_specialized_type_(self, mono->list.xs.v[i], in_progress);
            if (replace) {
                mono->list.xs.v[i] = replace;
                did_replace        = 1;
            }
        }
        if (did_replace) return mono;

    } break;
    }

    return null;
}

tl_monotype *tl_infer_update_specialized_type(tl_infer *self, tl_monotype *mono) {
    switch (mono->tag) {
    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_var:
    case tl_weak:
    case tl_placeholder: return null;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:
    case tl_literal:     {
        hashmap     *in_progress = hset_create(self->transient, 8);
        tl_monotype *out         = tl_infer_update_specialized_type_(self, mono, &in_progress);
        return out;
    }
    }
    return null;
}

typedef struct {
    hashmap *in_progress;
} update_types_ctx;

static void update_types_one_type(tl_infer *self, update_types_ctx *ctx, tl_polytype **poly) {
    if (!poly || !*poly) return; // not all ast nodes will have types

    // Don't try to specialize type schemes
    if (tl_polytype_is_scheme(*poly)) return;

    switch ((*poly)->type->tag) {
    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_var:
    case tl_weak:
    case tl_placeholder: return;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:
    case tl_literal:     {
        // For recursive types, bounce until no changes. update_specialized_type returns null if there is no
        // need to replace the type being tested.
        int tries = 10;
        while (tries--) {
            int did_replace = 1;

            hset_reset(ctx->in_progress);
            tl_monotype *replace =
              tl_infer_update_specialized_type_(self, (*poly)->type, &ctx->in_progress);

            if (replace) *poly = tl_polytype_absorb_mono(self->arena, replace);
            else did_replace = 0;

            if (!did_replace) break;
        }
        if (-1 == tries) fatal("loop exhausted");
    }
    }
}

static void fixup_arrow_name(tl_infer *self, ast_node *ident) {
    if (ast_node_is_symbol(ident)) {
        tl_monotype *type = ident->type->type;
        if (!tl_monotype_is_arrow(type)) return;
        str  name      = ast_node_str(ident);

        str *inst_name = instance_lookup_arrow(self, name, type);
        if (inst_name) ast_node_name_replace(ident, *inst_name);
    }
}

static void update_types_arrow(tl_infer *self, ast_node *node) {
    if (ast_node_is_let_in(node)) {
        ast_node *ident = node->let_in.value;
        fixup_arrow_name(self, ident);
    }
}

static int update_types_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    update_types_ctx *ctx = traverse_ctx->user;
    update_types_one_type(self, ctx, &node->type);
    update_types_arrow(self, node);

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

    // propagate the types back up the ast, especially for type constructors
    switch (node->tag) {
    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:      ast_node_type_set(node, node->assignment.value->type); break;

    case ast_body:            {
        u32 n = node->body.expressions.size;
        if (n) ast_node_type_set(node, node->body.expressions.v[n - 1]->type);
    } break;

    case ast_let_in:
        if (node->let_in.body) ast_node_type_set(node, node->let_in.body->type);

        // Note: ensure name's type in the environment matches a specialized type constructor on the rhs
        {
            tl_monotype *value_type = node->let_in.value->type->type;
            // Also check for type literals whose target is a specialized instance
            if (tl_monotype_is_type_literal(value_type)) {
                value_type = tl_monotype_literal_target(value_type);
            }
            if (tl_monotype_is_inst_specialized(value_type)) {
                tl_polytype *new_type = tl_polytype_absorb_mono(self->arena, value_type);
                ast_node_type_set(node->let_in.name, new_type);
                tl_type_env_insert(self->env, ast_node_str(node->let_in.name), new_type);
            }
        }
        break;

    default: break;
    }

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
    return 0;
}

static void update_specialized_types(tl_infer *self) {
    update_types_ctx ctx  = {.in_progress = hset_create(self->transient, 8)};

    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype **poly = iter.data;
        update_types_one_type(self, &ctx, poly);
    }

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    traverse->user         = &ctx;
    iter                   = (hashmap_iterator){0};
    ast_node *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        traverse_ast(self, traverse, node, update_types_cb);
        // Note: traverse_ast does not traverse let nodes directly (just their sub-parts)
    }
    arena_reset(self->transient);
}

static int check_unresolved_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (traverse_ctx->is_field_name) return 0;

    if (ast_node_is_let_in(node) && !tl_monotype_is_arrow(node->let_in.value->type->type)) {
        if (!tl_polytype_is_concrete(node->let_in.name->type)) {
            type_error(self, node->let_in.name);
            type_error(self, node);
        }
    } else if (ast_node_is_reassignment(node) && !tl_polytype_is_concrete(node->type)) {
        unresolved_type_error(self, node);
    }

    return 0;
}

static void check_unresolved_types(tl_infer *self) {
    // checks if any nodes in ast are still type variables
    traverse_ctx    *traverse = traverse_ctx_create(self->transient);
    hashmap_iterator iter     = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        traverse_ast(self, traverse, node, check_unresolved_cb);
    }
    arena_reset(self->transient);
}

int tl_infer_run(tl_infer *self, ast_node_sized nodes, tl_infer_result *out_result) {
    dbg(self, "-- start inference --");

    // Phase 1: Alpha-conversion - ensure unique variable names
    // Performs alpha-conversion on the AST to ensure all bound variables have globally unique names
    // while preserving lexical scope. This simplifies later passes by removing name collision concerns.
    {
        rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};
        // rename toplevel let-in symbols and keep them in global lexical scope
        forall(i, nodes) rename_let_in(self, nodes.v[i], &ctx);

        // rename the rest
        ctx = (rename_variables_ctx){.lex = ctx.lex};
        forall(i, nodes) rename_variables(self, nodes.v[i], &ctx, 0);
        arena_reset(self->transient);
    }

    // Phase 2: Load top-level definitions
    // Load all top level forms.
    self->toplevels = ast_node_str_map_create(self->arena, 1024);
    load_toplevel(self, nodes);
    arena_reset(self->transient);
    if (self->errors.size) return 1;

    dbg(self, "-- toplevels");
    log_toplevels(self);

    // Phase 3: Generic function type inference
    // now go through the toplevel let nodes and create generic functions: don't call add_generic from
    // inside the iteration because infer will add lambda functions to the toplevel.
    forall(i, nodes) {
        if (ast_node_is_hash_command(nodes.v[i])) continue;
        if (ast_node_is_type_alias(nodes.v[i])) continue;
        add_generic(self, nodes.v[i]);
    }
    arena_reset(self->transient);

    if (self->errors.size) return 1;

    // Phase 4: Check free variables
    // check if free variables are present
    if (check_missing_free_variables(self)) return 1;
    if (self->errors.size) return 1;
    arena_reset(self->transient);

    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    dbg(self, "-- inference complete --");
    dbg(self, "");
    dbg(self, "-- toplevels");
    log_toplevels(self);
    if (1) {
        dbg(self, "-- subs");
        log_subs(self);
    }
    dbg(self, "-- env");
    log_env(self);
    arena_reset(self->transient);

    ast_node *main = null;
    if (!self->opts.is_library) {
        ast_node **found_main = str_map_get(self->toplevels, S("main"));
        if (!found_main) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_no_main_function}));
            return 1;
        }
        main = *found_main;
    }

    // Phase 5: Generic function specialization
    // Final phase: communiate type information top-down by following applications. This contrasts with
    // the bottom-up inference we just completed. At this point the program is well-typed and we are
    // setting up for the transpiler.
    dbg(self, "-- specialize phase");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    infer_ctx    *ctx      = infer_ctx_create(self->transient);
    traverse->user         = ctx;

    if (main) {
        traverse_ast(self, traverse, main, specialize_applications_cb);
    } else {
        assert(self->opts.is_library);
        ast_node *node = null;
        forall(i, nodes) {
            node = nodes.v[i];

            if (ast_node_is_let(node)) {
                ast_node *name = toplevel_name_node(node);
                if (!name->symbol.annotation_type) {
                    str fun_name = ast_node_str(name);
                    dbg(self, "skipping '%s' due to lack of annotation", str_cstr(&fun_name));
                    continue;
                }
            }
        }
    }

    // specialize toplevel nodes e.g. global values that may refer to functions by name
    forall(i, nodes) {
        ast_node *node = nodes.v[i];

        if (ast_node_is_let_in(node)) {
            traverse_ast(self, traverse, node, specialize_applications_cb);
        }
    }

    // specialize module init functions
    {
        // () -> Void
        tl_polytype *callsite = make_arrow_result_type(self, traverse, (ast_node_sized){0},
                                                       tl_polytype_nil(self->arena, self->registry), 0);
        forall(i, nodes) {
            ast_node *node = nodes.v[i];
            if (ast_node_is_let(node)) {
                str name = ast_node_str(node->let.name);
                if (is_module_init(name)) {
                    // These two things must be done in order for the transpiler to emit the function: It
                    // must be specialized and it must not have a generic type.
                    ast_node_set_is_specialized(node);
                    tl_type_env_insert(self->env, name, callsite);
                    // recurse through init body as if we had specialized it
                    post_specialize(self, traverse, node, callsite->type);
                }
            }
        }
    }

    arena_reset(self->transient);

    // apply subs to global environment
    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    // ensure main function has the correct type
    if (main) {
        if (check_main_function(self, main)) return 1;
        arena_reset(self->transient);
    }

    remove_generic_toplevels(self);
    arena_reset(self->transient);

    // Phase 6: Tree shaking
    // tree shake
    if (main) {
        tree_shake_toplevels(self, main);
        arena_reset(self->transient);

        // after tree shake, extraneous symbols will have been removed from environment
        if (check_missing_free_variables(self)) return 1;
        if (self->errors.size) return 1;
    }

    // Phase 7: Type specialization updates
    // update type specialisations: replace generic constructors with specialised constructors.
    update_specialized_types(self);
    arena_reset(self->transient);

    check_unresolved_types(self);
    arena_reset(self->transient);

    if (1) {
        dbg(self, "-- final subs");
        log_subs(self);
    }
    dbg(self, "-- final env --");
    log_env(self);
    arena_reset(self->transient);
    dbg(self, "-- final toplevels");
    log_toplevels(self);
    arena_reset(self->transient);

    if (self->errors.size) {
        return 1;
    }

    if (out_result) {
        out_result->infer     = self;
        out_result->registry  = self->registry;
        out_result->env       = self->env;
        out_result->subs      = self->subs;
        out_result->toplevels = self->toplevels;
        out_result->nodes     = nodes;

        array_shrink(self->synthesized_nodes);
        array_shrink(self->hash_includes);
        out_result->synthesized_nodes = (ast_node_sized)sized_all(self->synthesized_nodes);
        out_result->hash_includes     = (str_sized)sized_all(self->hash_includes);
    }
    arena_reset(self->transient);
    return 0;
}

void tl_infer_report_errors(tl_infer *self) {
    if (self->errors.size) {
        forall(i, self->errors) {
            tl_infer_error *err     = &self->errors.v[i];
            ast_node const *node    = err->node;
            str             message = err->message;

            if (node) {
                str node_str = v2_ast_node_to_string(self->transient, node);
                if (node->file && *node->file)
                    fprintf(stderr, "%s:%u: %s: %.*s: %.*s\n", node->file, node->line,
                            tl_error_tag_to_string(err->tag), str_ilen(message), str_buf(&message),
                            str_ilen(node_str), str_buf(&node_str));
                else
                    fprintf(stderr, "%s: %.*s: %.*s\n", tl_error_tag_to_string(err->tag), str_ilen(message),
                            str_buf(&message), str_ilen(node_str), str_buf(&node_str));
            }

            else
                fprintf(stderr, "error: %s: %.*s\n", tl_error_tag_to_string(err->tag), str_ilen(message),
                        str_buf(&message));
        }
    }
}

//

static void dbg(tl_infer const *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "tl_infer: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}

static void log_str(tl_infer const *self, str str) {
    if (!self->verbose) return;

    int spaces = self->indent_level * 2;
    fprintf(stderr, "%*stl_infer: %.*s\n", spaces, "", str_ilen(str), str_buf(&str));
}

static void log_toplevels(tl_infer const *self) {
    if (!self->verbose) return;
    str_array sorted = str_map_sorted_keys(self->transient, self->toplevels);
    forall(i, sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, sorted.v[i]);
        str       str;
        if (self->verbose_ast) str = v2_ast_node_to_string(self->transient, node);
        else str = ast_node_to_short_string(self->transient, node);
        log_str(self, str);
        str_deinit(self->transient, &str);
    }
}

static void log_env(tl_infer const *self) {
    if (self->verbose) tl_type_env_log(self->env);
}

//

static void do_apply_subs(void *ctx, ast_node *node) {
    tl_infer *self = ctx;
    if (node->type) {
        tl_polytype_substitute(self->arena, node->type, self->subs);
    }
}

static void apply_subs_to_ast(tl_infer *self) {
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {
        ast_node_dfs(self, node, do_apply_subs);
    }
}

//

int is_intrinsic(str name) {
    return (0 == str_cmp_nc(name, "_tl_", 4));
}

int is_c_symbol(str name) {
    return (0 == str_cmp_nc(name, "c_", 2));
}

int is_c_struct_symbol(str name) {
    return (0 == str_cmp_nc(name, "c_struct_", 9));
}

int is_module_init(str name) {
    return str_ends_with(name, S("___init__0"));
}

//

static void toplevel_add(tl_infer *self, str name, ast_node *node) {
    ast_node_str_map_add(&self->toplevels, name, node);
}

static void toplevel_del(tl_infer *self, str name) {
    ast_node_str_map_erase(self->toplevels, name);
}

static ast_node *toplevel_get(tl_infer *self, str name) {
    return ast_node_str_map_get(self->toplevels, name);
}

static ast_node *toplevel_iter(tl_infer *self, hashmap_iterator *iter) {
    return ast_node_str_map_iter(self->toplevels, iter);
}

ast_node *toplevel_name_node(ast_node *node) {
    if (ast_node_is_let(node)) return node->let.name;
    else if (ast_node_is_let_in(node)) return node->let_in.name;
    else if (ast_node_is_symbol(node)) return node;
    else if (ast_node_is_utd(node)) return node->user_type_def.name;
    else if (ast_node_is_nfa(node)) return node->named_application.name;
    else if (ast_node_is_type_alias(node)) {
        if (ast_node_is_symbol(node->type_alias.name)) return node->type_alias.name;
        else fatal("runtime error");
    } else fatal("logic error");
}

str toplevel_name(ast_node const *node) {
    return toplevel_name_node((ast_node *)node)->symbol.name;
}

//

static void log_constraint(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    if (!self->verbose) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    dbg(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

static void log_constraint_mono(tl_infer *self, tl_monotype *left, tl_monotype *right,
                                ast_node const *node) {
    if (!self->verbose) return;
    str left_str  = tl_monotype_to_string(self->transient, left);
    str right_str = tl_monotype_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    dbg(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

static void log_type_error(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    // Note: always print err to stderr
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);

    fprintf(stderr, "%s:%i: error: conflicting types: %s versus %s\n", node->file, node->line,
            str_cstr(&left_str), str_cstr(&right_str));
}
static void log_type_error_mm(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    tl_polytype l = tl_polytype_wrap((tl_monotype *)left), r = tl_polytype_wrap((tl_monotype *)right);
    log_type_error(self, &l, &r, node);
}

static void log_subs(tl_infer *self) {
    if (self->verbose) tl_type_subs_log(self->subs);
}
