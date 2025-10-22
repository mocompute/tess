#include "infer.h"
#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "error.h"
#include "str.h"
#include "type.h"

#include "ast.h"
#include "hashmap.h"

#include "types.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    enum tl_error_tag tag;
    ast_node const   *node;
    str               message;
} tl_infer_error;

defarray(tl_infer_error_array, tl_infer_error);

struct tl_infer {
    allocator           *transient;
    allocator           *arena;

    tl_type_registry    *registry;
    tl_type_env         *env;
    tl_type_subs        *subs;

    ast_node_array       synthesized_nodes;

    hashmap             *toplevels; // str => ast_node*
    hashmap             *instances; // u64 hash => str specialised name in env
    tl_infer_error_array errors;

    u32                  next_var_name;
    u32                  next_instantiation;

    int                  verbose;
    int                  indent_level;
};

typedef struct {
    u64 name_hash;
    u64 type_hash;
} name_and_type;

//

static void      apply_subs_to_ast(tl_infer *);
static str       next_instantiation(tl_infer *, str);
static void      cancel_last_instantiation(tl_infer *);

static void      toplevel_add(tl_infer *, str, ast_node *);
static ast_node *toplevel_iter(tl_infer *, hashmap_iterator *);
static void      toplevel_del(tl_infer *self, str name);

static void      log(tl_infer const *self, char const *restrict fmt, ...);
static void      log_toplevels(tl_infer const *);
static void      log_env(tl_infer const *);
static void      log_subs(tl_infer *);

//

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self           = new (alloc, tl_infer);

    self->transient          = arena_create(alloc, 4096);
    self->arena              = arena_create(alloc, 16 * 1024);
    self->env                = tl_type_env_create(self->arena, self->transient);
    self->subs               = tl_type_subs_create(self->arena);
    self->registry           = tl_type_registry_create(self->arena, self->subs);

    self->synthesized_nodes  = (ast_node_array){.alloc = self->arena};

    self->toplevels          = null;
    self->instances          = map_new(self->arena, name_and_type, str, 512);
    self->errors             = (tl_infer_error_array){.alloc = self->arena};

    self->next_var_name      = 0;
    self->next_instantiation = 0;

    self->verbose            = 0;
    self->indent_level       = 0;

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    if ((*p)->toplevels) map_destroy(&(*p)->toplevels);
    if ((*p)->instances) map_destroy(&(*p)->instances);

    arena_destroy(alloc, &(*p)->transient);
    arena_destroy(alloc, &(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose      = verbose;
    self->env->verbose = verbose;
}

static tl_type_variable get_tv_or_fresh(str name, hashmap **map, tl_type_subs *subs) {
    tl_type_variable  tv;
    tl_type_variable *found = str_map_get(*map, name);
    if (found) {
        tv = *found;
    } else {
        tv = tl_type_subs_fresh(subs);
        str_map_set(map, name, &tv);
    }
    return tv;
}

tl_monotype const *tl_type_registry_parse(tl_type_registry *self, ast_node const *node, tl_type_subs *subs,
                                          hashmap **map) {

    // map : map_new(self->transient, str, tl_type_variable, 8);

    if (ast_node_is_nil(node)) {
        return tl_type_registry_nil(self);
    }

    if (ast_node_is_symbol(node)) {
        tl_monotype const *out = tl_type_registry_instantiate(self, ast_node_str(node));
        if (out) return out;

        str               name = ast_node_str(node);

        tl_type_variable *tv   = str_map_get(*map, name);
        if (tv) return tl_monotype_create_tv(self->alloc, *tv);

        tl_type_variable fresh = tl_type_subs_fresh(subs);
        str_map_set(map, name, &fresh);
        return tl_monotype_create_tv(self->alloc, fresh);
    }

    if (ast_node_is_nfa(node)) {
        ast_node_sized    args      = {.v    = node->named_application.arguments,
                                       .size = node->named_application.n_arguments};

        tl_monotype_array args_mono = {.alloc = self->alloc};
        array_reserve(args_mono, args.size);
        forall(i, args) {
            tl_monotype const *mono = tl_type_registry_parse(self, args.v[i], subs, map);
            if (!mono) {
                // a type variable
                if (!ast_node_is_symbol(args.v[i])) fatal("logic error");
                tl_type_variable tv = get_tv_or_fresh(ast_node_str(args.v[i]), map, subs);
                mono                = tl_monotype_create_tv(self->alloc, tv);
            }
            array_push(args_mono, mono);
        }
        array_shrink(args_mono);

        return tl_type_registry_instantiate_with(self, ast_node_str(node->named_application.name),
                                                 (tl_monotype_sized)sized_all(args_mono));
    }

    if (ast_node_is_arrow(node)) {
        tl_monotype const *left  = tl_type_registry_parse(self, node->arrow.left, subs, map);
        tl_monotype const *right = tl_type_registry_parse(self, node->arrow.right, subs, map);
        tl_monotype_array  arr   = {.alloc = self->alloc};
        // flatten lists and concatenate
        // TODO: library function
        if (tl_monotype_is_list(left)) forall(i, left->list.xs) array_push(arr, left->list.xs.v[i]);
        else array_push(arr, left);
        if (tl_monotype_is_list(right)) forall(i, right->list.xs) array_push(arr, right->list.xs.v[i]);
        else array_push(arr, right);
        array_shrink(arr);
        return tl_monotype_create_list(self->alloc, (tl_monotype_sized)sized_all(arr));
    }

    fatal("logic error");
}

static void create_type_constructor_from_user_type(tl_infer *self, ast_node *node) {
    assert(ast_node_is_utd(node));
    str                    name              = node->user_type_def.name->symbol.name;
    u32                    n_type_arguments  = node->user_type_def.n_type_arguments;
    u32                    n_fields          = node->user_type_def.n_fields;
    ast_node             **type_arguments    = node->user_type_def.type_arguments;
    ast_node             **fields            = node->user_type_def.field_names;

    hashmap               *type_argument_map = map_new(self->transient, str, tl_type_variable, 8);
    tl_type_variable_array type_argument_tvs = {.alloc = self->arena};
    for (u32 i = 0; i < n_type_arguments; ++i) {
        ast_node const *ta = type_arguments[i];
        assert(ast_node_is_symbol(ta));
        str              ta_name = ast_node_str(ta);
        tl_type_variable fresh   = tl_type_subs_fresh(self->subs);
        str_map_set(&type_argument_map, ta_name, &fresh);

        array_push(type_argument_tvs, fresh);
    }

    str_array field_names = {.alloc = self->arena};
    array_reserve(field_names, node->user_type_def.n_fields);

    ast_node        **annotations = node->user_type_def.field_annotations;
    tl_monotype_array field_types = {.alloc = self->arena};
    for (u32 i = 0; i < n_fields; ++i) {
        // field name
        assert(ast_node_is_symbol(fields[i]));
        array_push(field_names, fields[i]->symbol.name);

        // field type, could be type argument, or type constructor
        tl_monotype const *field           = null;
        ast_node const    *field_type_node = annotations[i];
        if (ast_node_is_symbol(field_type_node)) {
            tl_type_variable *found = str_map_get(type_argument_map, ast_node_str(field_type_node));
            if (found) field = tl_monotype_create_tv(self->arena, *found);
        }

        if (!field)
            field = tl_type_registry_parse(self->registry, field_type_node, self->subs, &type_argument_map);

        if (!field) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
            return;
        }

        array_push(field_types, field);
    }

    array_shrink(field_types);
    array_shrink(field_names);
    array_shrink(type_argument_tvs);

    tl_polytype const *poly = tl_type_constructor_def_create(
      self->registry, name, (tl_type_variable_sized)sized_all(type_argument_tvs),
      (str_sized)sized_all(field_names), (tl_monotype_sized)sized_all(field_types));

    tl_type_env_insert(self->env, name, poly);
    node->type = poly;
}

void load_user_type(tl_infer *self, ast_node *node) {
    if (!ast_node_is_utd(node)) return;
    str name = ast_node_str(node->user_type_def.name);
    if (tl_type_registry_exists(self->registry, name)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
        return;
    }

    create_type_constructor_from_user_type(self, node);
    arena_reset(self->transient);
}

static void process_annotation(tl_infer *self, ast_node *node) {
    ast_node const *name = toplevel_name_node(node);

    if (!name->symbol.annotation) return;
    if (name->symbol.annotation_type) return;

    hashmap *map = map_new(self->transient, str, tl_type_variable, 8);
    // tl_polytype const *ann          = make_type_annotation(self, name->symbol.annotation, &map);
    tl_monotype const *ann =
      tl_type_registry_parse(self->registry, name->symbol.annotation, self->subs, &map);

    node->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, ann);

    map_destroy(&map);
}

static hashmap *load_toplevel(tl_infer *self, allocator *alloc, ast_node_sized nodes,
                              tl_infer_error_array *out_errors) {
    hashmap             *tops   = ast_node_str_map_create(alloc, 1024);
    tl_infer_error_array errors = {.alloc = alloc};
    (void)self;

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        if (ast_node_is_symbol(node)) {
            str        name_str = node->symbol.name;
            ast_node **p        = str_map_get(tops, name_str);
            if (p) {
                // merge annotation if existing node is a let node; otherwise error
                if (!ast_node_is_let(*p)) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                if (node->symbol.annotation) {
                    (*p)->let.name->symbol.annotation = node->symbol.annotation;
                    process_annotation(self, (*p)->let.name);
                }
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&tops, name_str, &node);
                    process_annotation(self, node);
                }
            }
        }

        else if (ast_node_is_let(node)) {
            str        name_str = ast_node_str(node->let.name);
            ast_node **p        = str_map_get(tops, name_str);

            if (p) {
                // merge type if the existing node is a symbol; otherwise error
                if (!ast_node_is_symbol(*p)) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                // ignore prior type annotation if the current symbol is annotated: later
                // declaration overrides
                if (!node->let.name->symbol.annotation) {
                    // apply annotation
                    node->let.name->symbol.annotation = (*p)->symbol.annotation;
                    process_annotation(self, node->let.name);
                }

                // replace prior symbol entry with let node
                *p = node;
            } else {
                str_map_set(&tops, name_str, &node);
                process_annotation(self, node->let.name);
            }
        }

        else if (ast_node_is_utd(node)) {
            str        name_str = ast_node_str(node->user_type_def.name);
            ast_node **p        = str_map_get(tops, name_str);

            if (p) {
                array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            } else {
                str_map_set(&tops, name_str, &node);
            }
        }

        else if (ast_node_is_let_in(node)) {
            str name_str = node->let_in.name->symbol.name;
            str_map_set(&tops, name_str, &node);
            process_annotation(self, node);
        }

        else {
            array_push(errors, ((tl_infer_error){.tag = tl_err_invalid_toplevel, .node = node}));
            continue;
        }
    }

    *out_errors = errors;
    arena_reset(self->transient);
    return tops;
}

// -- tree shake --

static ast_node *toplevel_get(tl_infer *, str);

typedef struct {
    tl_infer *self;
    hashmap  *names; // str set
} tree_shake_ctx;

hashmap *tree_shake(tl_infer *, ast_node const *);

void     do_tree_shake(void *ctx_, ast_node *node) {
    tree_shake_ctx *ctx  = ctx_;
    tl_infer       *self = ctx->self;

    if (ast_node_is_nfa(node)) {
        str name = toplevel_name(node);

        // add all symbol arguments because they could be function pointers
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(arg)) continue;
            if (str_eq(name, arg->symbol.name)) continue;
            if (!str_hset_contains(ctx->names, arg->symbol.name)) {
                str_hset_insert(&ctx->names, arg->symbol.name);

                // if it is a toplevel, recurse through it
                ast_node *next = toplevel_get(self, arg->symbol.name);
                if (next) {
                    ast_node_dfs(ctx, next, do_tree_shake);
                }
            }
        }

        if (!str_hset_contains(ctx->names, name)) {
            str_hset_insert(&ctx->names, name);

            ast_node *next = toplevel_get(ctx->self, name);
            if (next) {
                ast_node_dfs(ctx, next, do_tree_shake);
            }
        }
    }
}

hashmap *tree_shake(tl_infer *self, ast_node const *node) {

    tree_shake_ctx ctx = {.self = self};
    ctx.names          = hset_create(self->transient, 1024);

    str_hset_insert(&ctx.names, toplevel_name(node));

    ast_node_dfs(&ctx, (ast_node *)node, do_tree_shake);

    return ctx.names;
}

// -- inference --

typedef struct {
    hashmap *call_chain; // hset str
    hashmap *lex;        // hset str (names in local lexical scope)
    void    *user;
} traverse_ctx;

typedef int (*traverse_cb)(tl_infer *, traverse_ctx *, ast_node *);

static traverse_ctx *traverse_ctx_create(allocator *alloc) {
    traverse_ctx *out = new (alloc, traverse_ctx);
    out->call_chain   = hset_create(alloc, 16);
    out->lex          = hset_create(alloc, 16);
    out->user         = null;

    return out;
}

static void traverse_ctx_destroy(allocator *alloc, traverse_ctx **p) {
    if ((*p)->lex) hset_destroy(&(*p)->lex);
    if ((*p)->call_chain) hset_destroy(&(*p)->call_chain);

    alloc_free(alloc, *p);
    *p = null;
}

typedef struct {
    hashmap *specials; // str => str
} infer_ctx;

static infer_ctx *infer_ctx_create(allocator *alloc) {
    infer_ctx *out = new (alloc, infer_ctx);
    out->specials  = map_new(alloc, str, str, 16);

    return out;
}

static void infer_ctx_destroy(allocator *alloc, infer_ctx **p) {
    if ((*p)->specials) map_destroy(&(*p)->specials);

    alloc_free(alloc, *p);
    *p = null;
}

static int type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_error, .node = node}));
    return 1;
}

static void log_constraint(tl_infer *, tl_polytype const *, tl_polytype const *, ast_node const *);
static void log_type_error(tl_infer *, tl_polytype const *, tl_polytype const *);
static void log_type_error_mm(tl_infer *, tl_monotype const *, tl_monotype const *);

typedef struct {
    tl_infer       *self;
    ast_node const *node;
} type_error_cb_ctx;

static void type_error_cb(void *ctx_, tl_monotype const *left, tl_monotype const *right) {
    type_error_cb_ctx *ctx = ctx_;
    log_type_error_mm(ctx->self, left, right);
    type_error(ctx->self, ctx->node);
}

static int constrain_mono(tl_infer *self, tl_monotype const *left, tl_monotype const *right,
                          ast_node const *node) {
    type_error_cb_ctx error_ctx = {.self = self, .node = node};
    return tl_type_subs_unify_mono(self->subs, left, right, type_error_cb, &error_ctx);
}

static int constrain(tl_infer *self, infer_ctx *ctx, tl_polytype const *left, tl_polytype const *right,
                     ast_node const *node) {
    (void)ctx;

    if (left == right) return 0;
    log_constraint(self, left, right, node);

    tl_monotype const *lhs = null, *rhs = null;

    if (left->quantifiers.size) lhs = tl_polytype_instantiate(self->arena, left, self->subs);
    else lhs = left->type;
    if (right->quantifiers.size) rhs = tl_polytype_instantiate(self->arena, right, self->subs);
    else rhs = right->type;

    return constrain_mono(self, lhs, rhs, node);
}

static int constrain_pm(tl_infer *self, infer_ctx *ctx, tl_polytype const *left, tl_monotype const *right,
                        ast_node const *node) {

    tl_polytype wrap = tl_polytype_wrap(right);
    return constrain(self, ctx, left, &wrap, node);
}

static void ensure_tv(tl_infer *self, str const *name, tl_polytype const **type) {
    if (!type) return;
    if (*type) return;

    if (name) *type = tl_polytype_clone(self->arena, (tl_type_env_lookup(self->env, *name)));
    if (*type) return;

    *type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

static void               rename_variables(tl_infer *, ast_node *, hashmap **, int);
static str                specialize_fun(tl_infer *, infer_ctx *, ast_node *, tl_monotype const *);
static tl_polytype const *make_arrow(tl_infer *, ast_node_sized, ast_node *);
static tl_polytype const *make_arrow_with(tl_infer *, ast_node_sized, ast_node *, tl_polytype const *);

static ast_node          *clone_generic(allocator *alloc, ast_node const *node) {
    ast_node *clone = ast_node_clone(alloc, node);
    ast_node *name  = toplevel_name_node(clone);
    assert(ast_node_is_symbol(name));
    name->symbol.annotation_type = null;
    name->symbol.annotation      = null;
    return clone;
}

static int traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    if (null == node) return 0;

    switch (node->tag) {
    case ast_let: {

        hashmap           *save = map_copy(ctx->lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str_hset_insert(&ctx->lex, param->symbol.name);
            ensure_tv(self, null, &param->type);
            if (cb(self, ctx, param)) return 1;
        }

        if (traverse_ast(self, ctx, node->let.body, cb)) return 1;

        map_destroy(&ctx->lex);
        ctx->lex = save;

    } break;

    case ast_let_in: {

        hashmap *save = map_copy(ctx->lex);
        assert(ast_node_is_symbol(node->let_in.name));
        str_hset_insert(&ctx->lex, node->let_in.name->symbol.name);

        // process node first, because there may be side effects required before traversing body.
        if (cb(self, ctx, node)) return 1;

        // traverse value first, then traverse body
        if (traverse_ast(self, ctx, node->let_in.value, cb)) return 1;

        ensure_tv(self, null, &node->let_in.name->type);
        if (cb(self, ctx, node->let_in.name)) return 1;

        if (traverse_ast(self, ctx, node->let_in.body, cb)) return 1;

        // process node again: for specialised types, typing the name depends on typing the value.
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lex);
        ctx->lex = save;

    } break;

    case ast_let_match_in: {

        hashmap *save = map_copy(ctx->lex);

        // process node first, because there may be side effects required before traversing body.
        if (cb(self, ctx, node)) return 1;

        ast_node_sized arr = ast_node_sized_from_ast_array(node->let_match_in.lt);
        forall(i, arr) {
            ast_node *ass = arr.v[i];
            assert(ast_node_is_assignment(ass));
            ast_node *name_node = ass->assignment.name;
            assert(ast_node_is_symbol(name_node));
            str_hset_insert(&ctx->lex, name_node->symbol.name);

            ensure_tv(self, null, &name_node->type);
            if (cb(self, ctx, name_node)) return 1;
        }

        if (traverse_ast(self, ctx, node->let_match_in.body, cb)) return 1;

        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_named_function_application: {

        str name = node->named_application.name->symbol.name;

        // do not process recursive calls
        if (str_hset_contains(ctx->call_chain, name)) {
            log(self, "detected recursive call to '%s'", str_cstr(&name));
            return 0;
        }

        // traverse arguments
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter)))
            if (traverse_ast(self, ctx, arg, cb)) return 1;

        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_lambda_function: {

        hashmap           *save = map_copy(ctx->lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str_hset_insert(&ctx->lex, param->symbol.name);
            ensure_tv(self, null, &param->type);
            if (cb(self, ctx, param)) return 1;
        }

        if (traverse_ast(self, ctx, node->lambda_function.body, cb)) return 1;

        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lex);
        ctx->lex = save;

    } break;

    case ast_lambda_function_application: {

        ensure_tv(self, null, &node->type);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter)))
            if (traverse_ast(self, ctx, arg, cb)) return 1;

        if (traverse_ast(self, ctx, node->lambda_application.lambda, cb)) return 1;

        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_if_then_else: {
        if (traverse_ast(self, ctx, node->if_then_else.condition, cb)) return 1;
        if (traverse_ast(self, ctx, node->if_then_else.yes, cb)) return 1;
        if (traverse_ast(self, ctx, node->if_then_else.no, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) {
            if (traverse_ast(self, ctx, arr.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_user_type_get:
        if (traverse_ast(self, ctx, node->user_type_get.struct_name, cb)) return 1;
        if (traverse_ast(self, ctx, node->user_type_get.field_name, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_user_type_set:
        if (traverse_ast(self, ctx, node->user_type_set.struct_name, cb)) return 1;
        if (traverse_ast(self, ctx, node->user_type_set.field_name, cb)) return 1;
        if (traverse_ast(self, ctx, node->user_type_set.value, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_body:
        forall(i, node->body.expressions) {
            if (traverse_ast(self, ctx, node->body.expressions.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_binary_op:
        // don't traverse op, it's just an operator
        if (traverse_ast(self, ctx, node->binary_op.left, cb)) return 1;
        if (traverse_ast(self, ctx, node->binary_op.right, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_unary_op:
        // don't traverse op, it's just an operator
        if (traverse_ast(self, ctx, node->unary_op.operand, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

        // FIXME: complete the misisng traversals for the various ast types below

    case ast_nil:
    case ast_any:
    case ast_address_of:
    case ast_pointer_to:
    case ast_arrow:
    case ast_assignment:
    case ast_bool:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_string:
    case ast_symbol:
    case ast_u64:
    case ast_user_type_definition:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_user_type:

        // operate on the leaf node
        if (cb(self, ctx, node)) return 1;

        break;
    }

    return 0;
}

static int add_generic(tl_infer *, ast_node *);

static int infer_traverse_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    infer_ctx *ctx = traverse_ctx->user;

    if (null == node) return 0;

    switch (node->tag) {
    case ast_nil:
    case ast_any:        break;
    case ast_address_of:
    case ast_pointer_to: {

        // address-of operator only accept symbols (lvalues)
        ensure_tv(self, null, &node->type);
        ast_node *target = node->address_of.target;
        if (ast_node_is_symbol(target)) {
            tl_polytype const *target_ty = tl_type_env_lookup(self->env, target->symbol.name);
            if (target_ty && tl_polytype_is_concrete(target_ty)) {
                // ptr to concrete type
                tl_monotype const *ptr = tl_type_registry_ptr(self->registry, target_ty->type);
                if (constrain_pm(self, ctx, node->type, ptr, node)) return 1;
            } else if (target_ty) {
                // ptr to weak type variable, constrained to the type of the target
                tl_monotype const *wv  = tl_monotype_create_fresh_weak(self->subs);
                tl_monotype const *ptr = tl_type_registry_ptr(self->registry, wv);
                if (constrain_pm(self, ctx, node->type, ptr, node)) return 1;
                if (constrain_pm(self, ctx, target_ty, wv, node)) return 1;
            }
        }
    } break;

    case ast_dereference: {
        ensure_tv(self, null, &node->type);
        ast_node *target = node->dereference.target;
        if (ast_node_is_symbol(target)) {
            tl_polytype const *target_ty = tl_type_env_lookup(self->env, target->symbol.name);
            if (!target_ty) return 0;
            assert(target_ty->type->cons_inst->args.size == 1);
            tl_monotype const *deref = target_ty->type->cons_inst->args.v[0];
            if (constrain_pm(self, ctx, node->type, deref, node)) return 1;
        }

    } break;

    case ast_string: {
        tl_monotype const *ty = tl_type_registry_string(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_f64: {
        tl_monotype const *ty = tl_type_registry_float(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_i64: {
        tl_monotype const *ty = tl_type_registry_int(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_u64: {
        tl_monotype const *ty = tl_type_registry_int(self->registry); // FIXME unsigned
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_bool: {
        tl_monotype const *ty = tl_type_registry_bool(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_body: {
        if (node->body.expressions.size) {
            u32       sz   = node->body.expressions.size;
            ast_node *last = node->body.expressions.v[sz - 1];
            ensure_tv(self, null, &node->type);
            ensure_tv(self, null, &last->type);
            if (constrain(self, ctx, node->type, last->type, node)) return 1;
        }
    } break;

    case ast_binary_op: {
        ast_node *left = node->binary_op.left, *right = node->binary_op.right;
        ensure_tv(self, null, &left->type);
        ensure_tv(self, null, &right->type);
        if (constrain(self, ctx, node->type, left->type, node)) return 1;
        if (constrain(self, ctx, left->type, right->type, node)) return 1;
    } break;

    case ast_unary_op: {
        ast_node *operand = node->unary_op.operand;
        ensure_tv(self, null, &operand->type);
        if (constrain(self, ctx, node->type, operand->type, node)) return 1;
    } break;

    case ast_let_in: {
        ensure_tv(self, null, &node->type);
        ensure_tv(self, null, &node->let_in.name->type);
        ensure_tv(self, null, &node->let_in.value->type);
        if (node->let_in.body) ensure_tv(self, null, &node->let_in.body->type);

        if (ast_node_is_lambda_function(node->let_in.value)) {

            str name = node->let_in.name->symbol.name;

            // define a generic lambda
            if (add_generic(self, node)) return 1;

            // add let-in node to toplevels (because we need the name and the body)
            toplevel_add(self, name, node);

            // Do not infer the node value - add_generic takes care of that.
            // Instead, trigger runtime problems if the name's type is referenced using the expression type
            // (rather than the type_env type).
            node->let_in.name->type = null;

            if (node->let_in.body)
                if (constrain(self, ctx, node->type, node->let_in.body->type, node)) return 1;

        } else {

            if (ast_node_is_std_application(node->let_in.value)) {
                // Note: special case std_ functions and give them all a Nil return type
                tl_monotype const *nil   = tl_type_registry_nil(self->registry);
                node->let_in.value->type = tl_polytype_absorb_mono(self->arena, nil);
            }

            if (constrain(self, ctx, node->let_in.name->type, node->let_in.value->type, node)) return 1;

            // add value to environment
            tl_type_env_insert(self->env, node->let_in.name->symbol.name, node->let_in.value->type);

            if (node->let_in.body)
                if (constrain(self, ctx, node->type, node->let_in.body->type, node)) return 1;
        }
    } break;

    case ast_let: {

        ast_arguments_iter iter  = ast_node_arguments_iter(node);
        tl_polytype const *arrow = make_arrow(self, iter.nodes, node->let.body);
        if (!arrow) return 1;
        tl_polytype_substitute(self->arena, (tl_polytype *)arrow, self->subs); // const cast
        tl_type_env_insert(self->env, node->let.name->symbol.name, arrow);

    } break;

    case ast_symbol: {
        tl_polytype const *global = tl_type_env_lookup(self->env, node->symbol.name);

        if (global) {
            tl_polytype const *global_copy = tl_polytype_clone(self->arena, global);
            if (node->type) {
                if (constrain(self, ctx, node->type, global_copy, node)) return 1;
            } else {
                node->type = global_copy;
            }
        }

        else {
            ensure_tv(self, null, &node->type);
        }

        // if symbol has a type annotation, constrain it
        if (node->symbol.annotation) {
            process_annotation(self, node);
            if (constrain(self, ctx, node->symbol.annotation_type, node->type, node)) return 1;
        }

        // add to environment
        if (!global) tl_type_env_insert(self->env, node->symbol.name, node->type);

    } break;

    case ast_named_function_application: {

        ensure_tv(self, null, &node->type);

        str                name = node->named_application.name->symbol.name;
        tl_polytype const *type = tl_type_env_lookup(self->env, name);
        if (!type) {
            return 0; // mututal recursion, undeclared std_* functions, etc
        }

        if (tl_polytype_is_type_constructor(type)) {
            // a type constructor

            tl_monotype_array  args = {.alloc = self->arena};
            ast_arguments_iter iter = ast_node_arguments_iter(node);
            ast_node          *arg;
            while ((arg = ast_arguments_next(&iter))) {
                ensure_tv(self, null, &arg->type);
                assert(!tl_polytype_is_scheme(arg->type));
                array_push(args, arg->type->type);
            }
            array_shrink(args);

            tl_monotype const *inst = tl_type_registry_instantiate(self->registry, name);
            if (!inst) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_arity, .node = node}));
                return 1;
            }

            {
                str                inst_str = tl_monotype_to_string(self->transient, inst);
                tl_polytype const *app      = make_arrow(self, iter.nodes, null);
                if (!app) return 1;
                str app_str = tl_polytype_to_string(self->transient, app);
                log(self, "type constructor: callsite '%s' (%s) arrow: %s", str_cstr(&name),
                    str_cstr(&inst_str), str_cstr(&app_str));
            }

            iter  = ast_node_arguments_iter(node);
            u32 i = 0;
            while ((arg = ast_arguments_next(&iter))) {
                if (i >= inst->cons_inst->args.size) fatal("runtime error");
                if (constrain_pm(self, ctx, arg->type, inst->cons_inst->args.v[i], node)) return 1;
            }

            if (constrain_pm(self, ctx, node->type, inst, node)) return 1;

        } else {
            // a function type

            // instantiate generic function type being applied
            ast_arguments_iter iter     = ast_node_arguments_iter(node);
            tl_monotype const *inst     = tl_polytype_instantiate(self->arena, type, self->subs);
            str                inst_str = tl_monotype_to_string(self->transient, inst);
            tl_polytype const *app      = make_arrow(self, iter.nodes, node);
            if (!app) return 1;
            str app_str = tl_polytype_to_string(self->transient, app);
            log(self, "application: callsite '%s' (%s) arrow: %s", str_cstr(&name), str_cstr(&inst_str),
                str_cstr(&app_str));
            tl_polytype wrap = tl_polytype_wrap(inst);

            // and constrain it with the callsite types (arguments -> result)
            if (constrain(self, ctx, &wrap, app, node)) return 1;
        }

    } break;

    case ast_lambda_function_application: {

        // Instantiate and save type since it will never be generic.
        tl_monotype const *inst =
          tl_polytype_instantiate(self->arena, node->lambda_application.lambda->type, self->subs);
        node->lambda_application.lambda->type = tl_polytype_absorb_mono(self->arena, inst);

        // constrain arrow types
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        tl_polytype const *app  = make_arrow(self, iter.nodes, node);
        if (!app) return 1;
        tl_polytype wrap     = tl_polytype_wrap(inst);
        str         inst_str = tl_monotype_to_string(self->transient, inst);
        str         app_str  = tl_polytype_to_string(self->transient, app);
        log(self, "application: anon lambda %.*s callsite arrow: %.*s", str_ilen(inst_str),
            str_buf(&inst_str), str_ilen(app_str), str_buf(&app_str));
        if (constrain(self, ctx, &wrap, app, node)) return 1;

        // constain node type to the lambda body type
        if (constrain(self, ctx, node->type, node->lambda_application.lambda->lambda_function.body->type,
                      node))
            return 1;

    } break;

    case ast_lambda_function: {

        if (!node->type) {
            ast_arguments_iter iter  = ast_node_arguments_iter(node);
            tl_polytype const *arrow = make_arrow(self, iter.nodes, node->lambda_function.body);
            if (!arrow) return 1;
            tl_polytype_generalize((tl_polytype *)arrow, self->env, self->subs); // const cast
            node->type = arrow;
        }

    } break;

    case ast_if_then_else: {

        tl_monotype const *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, ctx, node->if_then_else.condition->type, bool_type, node)) return 1;
        if (constrain(self, ctx, node->if_then_else.yes->type, node->if_then_else.no->type, node)) return 1;
        ensure_tv(self, null, &node->type);
        if (constrain(self, ctx, node->type, node->if_then_else.yes->type, node)) return 1;
    } break;

    case ast_tuple: {
        ensure_tv(self, null, &node->type);
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        assert(arr.size > 0);

        tl_monotype_array tup_types = {.alloc = self->arena};
        array_reserve(tup_types, arr.size);
        forall(i, arr) {
            if (tl_polytype_is_scheme(arr.v[i]->type)) fatal("generic type");
            array_push(tup_types, arr.v[0]->type->type);
        }

        if (constrain(self, ctx, node->type,
                      tl_polytype_absorb_mono(
                        self->arena,
                        tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(tup_types))),
                      node))
            return 1;

    } break;

    case ast_user_type_get: {
        ensure_tv(self, null, &node->type);

        tl_polytype const *struct_type =
          tl_type_env_lookup(self->env, ast_node_str(node->user_type_get.struct_name));
        tl_polytype_substitute(self->arena, (tl_polytype *)struct_type, self->subs);
        if (!tl_polytype_is_type_constructor(struct_type)) return 0; // too early

        tl_type_constructor_def const *def        = struct_type->type->cons_inst->def;

        str                            field_name = ast_node_str(node->user_type_get.field_name);
        u32                            found      = INT32_MAX;
        forall(i, def->field_names) {
            if (str_eq(def->field_names.v[i], field_name)) {
                found = i;
                break;
            }
        }
        if (INT32_MAX == found) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_field_not_found, .node = node}));
            return 1;
        }
        assert(found < struct_type->type->cons_inst->args.size);
        tl_monotype const *field_type = struct_type->type->cons_inst->args.v[found];
        if (constrain_pm(self, ctx, node->type, field_type, node)) return 1;
    } break;

    case ast_user_type_set: {
        ensure_tv(self, null, &node->type);
        if (constrain(self, ctx, node->type, node->user_type_set.value->type, node)) return 1;
    } break;

    case ast_user_type_definition: {
    } break;

    case ast_arrow:
    case ast_assignment:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_let_match_in:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_user_type:            break;
    }

    // apply newly created constraint substitutions
    tl_type_subs_apply(self->subs, self->env);
    return 0;
}

static int specialize_user_type(tl_infer *self, ast_node *node) {
    str                name = node->named_application.name->symbol.name;

    tl_monotype_array  arr  = {.alloc = self->transient};
    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    while ((arg = ast_arguments_next(&iter))) {
        tl_polytype *poly = (tl_polytype *)tl_polytype_clone(self->arena, arg->type);
        assert(tl_polytype_is_concrete(poly));
        tl_polytype_substitute(self->arena, poly, self->subs);
        array_push(arr, poly->type);
    }
    array_shrink(arr);

    str                name_inst = next_instantiation(self, name);
    tl_monotype const *inst =
      tl_type_registry_specialize(self->registry, name, name_inst, (tl_monotype_sized)sized_all(arr));

    name_and_type key      = {.name_hash = str_hash64(name), .type_hash = tl_monotype_hash64(inst)};
    str          *existing = map_get(self->instances, &key, sizeof key);
    if (existing) {
        node->named_application.name->symbol.original = node->named_application.name->symbol.name;
        node->named_application.name->symbol.name     = *existing;

        cancel_last_instantiation(self);
        return 0;
    }
    map_set(&self->instances, &key, sizeof key, &name_inst);

    ast_node *utd = toplevel_get(self, name);
    assert(utd);
    utd                                      = ast_node_clone(self->arena, utd);
    utd->user_type_def.name->symbol.original = utd->user_type_def.name->symbol.name;
    utd->user_type_def.name->symbol.name     = name_inst;
    utd->type                                = tl_polytype_absorb_mono(self->arena, inst);
    toplevel_add(self, name_inst, utd);
    tl_type_env_insert(self->env, name_inst, utd->type);
    array_push(self->synthesized_nodes, utd);

    // update callsite
    node->named_application.name->symbol.original = node->named_application.name->symbol.name;
    node->named_application.name->symbol.name     = name_inst;
    node->type                                    = utd->type; // Note: this helps the transpiler

    return 0;
}

static ast_node *get_infer_target(ast_node *node) {
    if (ast_node_is_let(node)) {
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

static int specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {

    if (!ast_node_is_nfa(node)) return 0;

    infer_ctx         *ctx  = traverse_ctx->user;

    str                name = node->named_application.name->symbol.name;
    tl_polytype const *type = tl_type_env_lookup(self->env, name);
    if (!type) {
        return 0; // mutual recursion
    }

    if (tl_polytype_is_type_constructor(type)) return specialize_user_type(self, node);

    // instantiate generic function type being applied
    ast_arguments_iter iter     = ast_node_arguments_iter(node);
    ast_node          *fun_node = toplevel_get(self, name);
    if (!fun_node) return 0; // too early

    // Important: use _with variant to copy free variables info to the arrow, which is added to the
    // environment further down.
    tl_polytype const *app = make_arrow_with(self, iter.nodes, node, type);
    if (!app) return 1;

    // Important: resolve type variables by calling polytype_substitute.
    tl_polytype_substitute(self->arena, (tl_polytype *)app, self->subs); // const cast

    str app_str = tl_polytype_to_string(self->transient, app);
    log(self, "specialize application: callsite '%.*s' arrow: %.*s", str_ilen(name), str_buf(&name),
        str_ilen(app_str), str_buf(&app_str));

    // Important: If type at the callsite is not fully concrete, do not specialise. It must remain
    // polymorphic until all concrete type information is known.
    if (!tl_polytype_is_concrete(app)) return 0;

    // check if we already have a specialisation by name, because specialize_fun de-duplicates using name +
    // type, e.g consider the identity function
    str *existing;
    if ((existing = str_map_get(ctx->specials, name))) {
        str inst_name = *existing;

        ast_node_name_replace(node->named_application.name, inst_name);

    } else {
        str       inst_name = specialize_fun(self, ctx, fun_node, app->type);
        ast_node *special   = toplevel_get(self, inst_name);

        str_map_set(&ctx->specials, name, &inst_name);

        ast_node_name_replace(node->named_application.name, inst_name);

        // now infer and specialize the newly specialised fun
        ast_node *infer_target = get_infer_target(special);
        if (infer_target) {
            if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) return 1;
            if (traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb)) return 1;
        }

        // and recurse over any arguments which are toplevel functions

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(arg)) continue;
            str       arg_name = ast_node_str(arg);
            ast_node *top      = toplevel_get(self, arg_name);
            if (!top) continue;
            if (top->type && !tl_monotype_is_arrow(top->type->type)) continue;

            if (traverse_ast(self, traverse_ctx, top, infer_traverse_cb)) return 1;
            if (traverse_ast(self, traverse_ctx, top, specialize_applications_cb)) return 1;
        }

        // remove name from specials after recursing, so it doesn't shadow subsequent uses of the same name,
        // eg: let id x = x in let x1 = id 0 in let x2 = id "hello" in x1
        str_map_erase(ctx->specials, name);
    }
    return 0;
}

static str next_variable_name(tl_infer *);

// Performs alpha-conversion on the AST to ensure all bound variables have globally unique names while
// preserving lexical scope. This simplifies later passes by removing name collision concerns.

static void rename_let_in(tl_infer *self, ast_node *node, hashmap **lex) {
    // For toplevel definitions, rename them and keep them in lexical scope.
    if (!ast_node_is_let_in(node)) return;

    str name   = node->let_in.name->symbol.name;
    str newvar = next_variable_name(self);
    ast_node_name_replace(node->let_in.name, newvar);
    log(self, "rename %.*s => %.*s", str_ilen(node->let_in.name->symbol.original),
        str_buf(&node->let_in.name->symbol.original), str_ilen(node->let_in.name->symbol.name),
        str_buf(&node->let_in.name->symbol.name));

    str_map_set(lex, name, &newvar);
}

static void rename_variables(tl_infer *self, ast_node *node, hashmap **lex, int level) {
    // level should be 0 on entry. It is used to recognize toplevel let nodes which assign static values
    // that must remain in lexical scope throughout the program.

    if (null == node) return;

    // ensure all types are removed: important for the post-clone rename of functions being specialized.
    node->type = null;

    switch (node->tag) {

    case ast_address_of:
    case ast_pointer_to:  rename_variables(self, node->address_of.target, lex, level + 1); break;
    case ast_dereference: rename_variables(self, node->dereference.target, lex, level + 1); break;

    case ast_dereference_assign:
        rename_variables(self, node->dereference_assign.target, lex, level + 1);
        rename_variables(self, node->dereference_assign.value, lex, level + 1);
        break;

    case ast_if_then_else:
        rename_variables(self, node->if_then_else.condition, lex, level + 1);
        rename_variables(self, node->if_then_else.yes, lex, level + 1);
        rename_variables(self, node->if_then_else.no, lex, level + 1);
        break;

    case ast_let_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, lex, level + 1);

        hashmap *save = null;
        str      name = node->let_in.name->symbol.name;
        if (level) {
            // do not rename toplevel symbols again (see rename_let_in)
            str newvar = next_variable_name(self);
            ast_node_name_replace(node->let_in.name, newvar);
            log(self, "rename %.*s => %.*s", str_ilen(node->let_in.name->symbol.original),
                str_buf(&node->let_in.name->symbol.original), str_ilen(node->let_in.name->symbol.name),
                str_buf(&node->let_in.name->symbol.name));

            // establish lexical scope of the let-in binding and recurse
            save = map_copy(*lex);
            str_map_set(lex, name, &newvar);
        }

        rename_variables(self, node->let_in.body, lex, level + 1);

        // restore prior scope
        if (save) {
            map_destroy(lex);
            *lex = save;
        }
    } break;

    case ast_let_match_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, lex, level + 1);

        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->let_match_in.lt->labelled_tuple.n_assignments; ++i) {
            ast_node *ass = node->let_match_in.lt->labelled_tuple.assignments[i];
            assert(ast_node_is_assignment(ass));
            ast_node *name_node = ass->assignment.name;
            assert(ast_node_is_symbol(name_node));
            str name   = name_node->symbol.name;
            str newvar = next_variable_name(self);
            ast_node_name_replace(name_node, newvar);

            str_map_set(lex, name, &newvar);
        }

        rename_variables(self, node->let_match_in.body, lex, level + 1);

        map_destroy(lex);
        *lex = save;
    } break;

    case ast_symbol: {
        str *found;
        if ((found = str_map_get(*lex, node->symbol.name))) {
            node->symbol.original = node->symbol.name;
            node->symbol.name     = *found;
            log(self, "rename %.*s => %.*s", str_ilen(node->symbol.original),
                str_buf(&node->symbol.original), str_ilen(node->symbol.name), str_buf(&node->symbol.name));
        } else {
            // a free variable
        }

        // ensure renamed symbols do not carry a type
        node->type = null;
    } break;

    case ast_lambda_function: {
        // establish lexical scope for formal parameters and recurse
        hashmap           *save = map_copy(*lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str name   = param->symbol.name;
            str newvar = next_variable_name(self);
            ast_node_name_replace(param, newvar);
            str_map_set(lex, name, &newvar);
            rename_variables(self, param, lex, level + 1);
        }

        rename_variables(self, node->lambda_function.body, lex, level + 1);

        map_destroy(lex);
        *lex = save;
    } break;

    case ast_let: {
        // establish lexical scope for formal parameters and recurse
        hashmap           *save = map_copy(*lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str name   = param->symbol.name;
            str newvar = next_variable_name(self);
            ast_node_name_replace(param, newvar);
            str_map_set(lex, name, &newvar);
            rename_variables(self, param, lex, level + 1);
        }

        rename_variables(self, node->let.body, lex, level + 1);

        map_destroy(lex);
        *lex = save;

    } break;

    case ast_begin_end:
        for (u32 i = 0; i < node->begin_end.n_expressions; ++i)
            rename_variables(self, node->begin_end.expressions[i], lex, level + 1);
        break;

    case ast_lambda_function_application: {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, lex, level + 1);

        // establishes scope for lambda body
        rename_variables(self, node->lambda_application.lambda, lex, level + 1);
    } break;

    case ast_named_function_application: {
        rename_variables(self, node->named_application.name, lex, level + 1);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, lex, level + 1);

    } break;

    case ast_user_type_get: rename_variables(self, node->user_type_get.struct_name, lex, level + 1); break;
    case ast_user_type_set: rename_variables(self, node->user_type_set.struct_name, lex, level + 1); break;

    case ast_tuple:         {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) rename_variables(self, arr.v[i], lex, level + 1);
    } break;

    case ast_assignment:
        rename_variables(self, node->assignment.name, lex, level + 1);
        rename_variables(self, node->assignment.value, lex, level + 1);
        break;

    case ast_labelled_tuple:
        for (u32 i = 0; i < node->labelled_tuple.n_assignments; ++i)
            rename_variables(self, node->labelled_tuple.assignments[i], lex, level + 1);
        break;

    case ast_binary_op:
        rename_variables(self, node->binary_op.left, lex, level + 1);
        rename_variables(self, node->binary_op.right, lex, level + 1);
        break;

    case ast_unary_op: rename_variables(self, node->unary_op.operand, lex, level + 1); break;

    case ast_body:
        //
        forall(i, node->body.expressions) {
            rename_variables(self, node->body.expressions.v[i], lex, level + 1);
        }
        break;

    case ast_string:
    case ast_nil:
    case ast_any:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_u64:
    case ast_user_type_definition:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_user_type:            break;
    }
}

static void add_free_variables_to_arrow(tl_infer *, ast_node *, tl_polytype *);

static str  specialize_fun(tl_infer *self, infer_ctx *ctx, ast_node *node, tl_monotype const *arrow) {
    (void)ctx;
    str name = toplevel_name(node);

    // de-duplicate instances: hashes give us structural equality (barring hash collisions), which we need
    // because types are frequently cloned.
    name_and_type key      = {.name_hash = str_hash64(name), .type_hash = tl_monotype_hash64(arrow)};
    str          *existing = map_get(self->instances, &key, sizeof key);
    if (existing) return *existing;

    // instantiate unique name
    str name_inst = next_instantiation(self, name);
    map_set(&self->instances, &key, sizeof key, &name_inst);

    // clone function source ast and rename variables
    ast_node *generic_node = clone_generic(self->arena, toplevel_get(self, name));
    hashmap  *rename_lex   = map_new(self->transient, str, str, 16);
    rename_variables(self, generic_node, &rename_lex, 0);
    map_destroy(&rename_lex);

    // recalculate free variables, because symbol names have been renamed
    tl_polytype wrap                      = tl_polytype_wrap(arrow);
    ((tl_monotype *)arrow)->list.fvs.size = 0; // const cast
    add_free_variables_to_arrow(self, generic_node, &wrap);

    // add to type environment
    tl_type_env_insert_mono(self->env, name_inst, arrow);

    ast_node      *body   = null;
    ast_node_sized params = {0};
    if (ast_node_is_let(generic_node)) {
        body   = generic_node->let.body;
        params = ast_node_sized_from_ast_array(generic_node);
        ast_node_name_replace(generic_node->let.name, name_inst);
    } else if (ast_node_is_let_in_lambda(generic_node)) {
        body   = generic_node->let_in.value->lambda_function.body;
        params = ast_node_sized_from_ast_array(generic_node->let_in.value);
        ast_node_name_replace(generic_node->let_in.name, name_inst);
    } else if (ast_node_is_symbol(generic_node)) {
        // no body
        ;
    } else {
        fatal("logic error");
    }

    if (body) {
        // assign concrete types to parameters
        assert(tl_list == arrow->tag);

        assert(arrow->list.xs.size == params.size + 1);
        forall(i, params) {
            ast_node *param = params.v[i];
            param->type =
              tl_polytype_absorb_mono(self->arena, tl_monotype_clone(self->arena, arrow->list.xs.v[i]));
        }

        tl_monotype const *inst_result = tl_monotype_sized_last(arrow->list.xs);
        body->type = tl_polytype_absorb_mono(self->arena, tl_monotype_clone(self->arena, inst_result));

        // add to toplevel
        log(self, "toplevel_add: %.*s", str_ilen(name_inst), str_buf(&name_inst));
        toplevel_add(self, name_inst, generic_node);
    }

    else {
        // even no-body polymorphic toplevels (like intrinsics, which are represented by symbols as forward
        // decls), need to be added to toplevel with their specialized names.
        log(self, "toplevel_add: %.*s", str_ilen(name_inst), str_buf(&name_inst));
        toplevel_add(self, name_inst, generic_node);
    }

    return name_inst;
}

static str next_variable_name(tl_infer *self) {
    char buf[40];
    snprintf(buf, sizeof buf, "tl_v%u", self->next_var_name++);
    return str_init(self->arena, buf);
}

static str next_instantiation(tl_infer *self, str name) {
    char buf[128];
    snprintf(buf, sizeof buf, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
    return str_init(self->arena, buf);
}

static void cancel_last_instantiation(tl_infer *self) {
    self->next_instantiation--;
}

static tl_polytype const *make_arrow(tl_infer *self, ast_node_sized args, ast_node *result) {

    if (result) ensure_tv(self, null, &result->type);

    if (args.size == 0) {
        tl_monotype const *lhs   = tl_type_registry_nil(self->registry);
        tl_monotype const *rhs   = result ? tl_monotype_clone(self->arena, result->type->type) : null;
        tl_monotype const *arrow = tl_monotype_create_arrow(self->arena, lhs, rhs);

        {
            str str = tl_monotype_to_string(self->transient, arrow);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_polytype_absorb_mono(self->arena, arrow);
    }

    else if (args.size == 1) {
        // nil type
        if (ast_node_is_nil(args.v[0])) {
            tl_monotype const *mono = tl_type_registry_nil(self->registry);
            if (!mono) fatal("runtime error");
            args.v[0]->type = tl_polytype_absorb_mono(self->arena, (tl_monotype *)mono); // FIXME const
        } else ensure_tv(self, null, &args.v[0]->type);

        if (tl_polytype_is_scheme(args.v[0]->type)) {
            array_push(self->errors,
                       ((tl_infer_error){.tag = tl_err_polymorphic_function_argument, .node = args.v[0]}));
            // Note: add a type annotation to the function definition to eliminate this error
            return null;
        }

        tl_monotype const *lhs   = tl_monotype_clone(self->arena, args.v[0]->type->type);
        tl_monotype const *rhs   = result ? tl_monotype_clone(self->arena, result->type->type) : null;
        tl_monotype const *arrow = tl_monotype_create_arrow(self->arena, lhs, rhs);
        {
            str str = tl_monotype_to_string(self->transient, arrow);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_polytype_absorb_mono(self->arena, arrow);
    }

    else {

        tl_monotype_array clone = {.alloc = self->arena};
        array_reserve(clone, args.size);
        forall(i, args) {
            ensure_tv(self, null, &args.v[i]->type);

            if (tl_polytype_is_scheme(args.v[i]->type)) {
                array_push(self->errors, ((tl_infer_error){.tag  = tl_err_polymorphic_function_argument,
                                                           .node = args.v[i]}));
                // Note: add a type annotation to the function definition to eliminate this error
                return null;
            }

            tl_monotype const *ty = tl_monotype_clone(self->arena, args.v[i]->type->type);
            array_push(clone, ty);
        }

        if (result) {
            tl_monotype const *res_ty = tl_monotype_clone(self->arena, result->type->type);
            array_push(clone, res_ty);
        }

        array_shrink(clone);

        tl_monotype const *out = tl_monotype_create_list(self->arena, (tl_monotype_sized)sized_all(clone));
        {
            str str = tl_monotype_to_string(self->transient, out);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }

        return tl_polytype_absorb_mono(self->arena, out);
    }
}

static tl_polytype const *make_arrow_with(tl_infer *self, ast_node_sized args, ast_node *result,
                                          tl_polytype const *type) {
    tl_polytype const *out = make_arrow(self, args, result);
    if (!out) return null;
    if (tl_monotype_is_list(out->type) && tl_monotype_is_list(type->type)) {
        ((tl_monotype *)out->type)->list.fvs = type->type->list.fvs; // const cast
    }
    return out;
}

typedef struct {
    str_array fvs;
} collect_free_variables_ctx;

static int collect_free_variables_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (!ast_node_is_symbol(node)) return 0;

    collect_free_variables_ctx *ctx      = traverse_ctx->user;

    tl_polytype const          *type     = tl_type_env_lookup(self->env, node->symbol.name);
    int                         is_arrow = type && tl_monotype_is_arrow(type->type);

    // Note: arrow types in the environment are global functions and are not free variables. Note that
    // even local let-in-lambda functions are also in the environment, but their names will never clash
    // with function names.
    if (is_arrow || (str_hset_contains(traverse_ctx->lex, node->symbol.name))) {
        ;
    } else {
        // a free variable
        str_array_set_insert(&ctx->fvs, node->symbol.name);
    }

    // if symbol has a type which carries fvs, we also collect those.
    if (is_arrow && !tl_polytype_is_scheme(type)) {
        str_sized type_fvs = tl_monotype_fvs(type->type);
        forall(i, type_fvs) {
            str_array_set_insert(&ctx->fvs, type_fvs.v[i]);
        }
    }

    return 0;
}

static void promote_free_variables(str_array *out, tl_monotype const *in) {
    if (tl_monotype_is_list(in)) {
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
    traverse_ctx_destroy(self->transient, &traverse_ctx);

    array_shrink(ctx.fvs);
    log(self, "-- free variables: %u --", ctx.fvs.size);
    forall(i, ctx.fvs) {
        log(self, "%.*s", str_ilen(ctx.fvs.v[i]), str_buf(&ctx.fvs.v[i]));
    }

    // find any sublists with free variables and bring them to the top
    promote_free_variables(&ctx.fvs, arrow->type);

    // add free variables to arrow type
    if (ctx.fvs.size) {
        tl_monotype_absorb_fvs((tl_monotype *)arrow->type,
                               (str_sized)sized_all(ctx.fvs)); // const cast

        // sort free variables
        tl_monotype_sort_fvs((tl_monotype *)arrow->type); // const cast
    }
}

static int generic_declaration(tl_infer *self, str name, ast_node const *name_node, ast_node *node) {
    // no function body, so let's treat this as a type declaration
    if (!name_node->symbol.annotation_type) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
        return 1;
    }

    // must quantify arrow types
    if (tl_monotype_is_arrow(node->symbol.annotation_type->type))
        tl_polytype_generalize((tl_polytype *)node->symbol.annotation_type, self->env,
                               self->subs); // const cast
    tl_type_env_insert(self->env, name, node->symbol.annotation_type);
    return 0;
}

static int infer_one(tl_infer *self, ast_node *infer_target) {
    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    infer_ctx    *ctx      = infer_ctx_create(self->transient);
    traverse->user         = ctx;
    if (traverse_ast(self, traverse, infer_target, infer_traverse_cb)) return 1;
    infer_ctx_destroy(self->transient, &ctx);
    traverse_ctx_destroy(self->transient, &traverse);
    return 0;
}

static int add_generic(tl_infer *self, ast_node *node) {
    if (!node) return 0;

    ast_node          *infer_target = get_infer_target(node);
    ast_node          *name_node    = toplevel_name_node(node);
    tl_polytype const *provisional  = null;

    str                name         = name_node->symbol.name;
    str                orig_name    = name_node->symbol.original;

    // do not process a second time
    if (tl_type_env_lookup(self->env, name)) return 0;

    // calculate provisional type, for recursive functions
    if (ast_node_is_let(node)) {
        provisional = make_arrow(self, ast_node_sized_from_ast_array(node), node->let.body);
    } else if (ast_node_is_let_in_lambda(node)) {
        provisional = make_arrow(self, ast_node_sized_from_ast_array(infer_target),
                                 node->let_in.value->lambda_function.body);
    } else if (ast_node_is_symbol(node)) {
        // toplevel symbol node, e.g. for declaration of intrinsics, or forward type annotations. They will
        // take precedence to any later declarations, so let's be careful
    } else if (ast_node_is_utd(node)) {
        load_user_type(self, node);
        return 0;
    } else if (ast_node_is_let_in(node)) {
        if (infer_one(self, infer_target)) {
            log(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
                str_ilen(orig_name), str_buf(&orig_name));
        }

        assert(node->let_in.value->type);
        tl_type_env_insert(self->env, name, node->let_in.value->type);
        return 0;

    } else {
        fatal("logic error");
    }

    log(self, "-- add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    process_annotation(self, name_node);

    if (!infer_target) {
        // no function body, so let's treat this as a type declaration
        return generic_declaration(self, name, name_node, node);
    }

    // add provisional type to environment (for polymorphic recursion)
    if (provisional) {
        // Note: ensure this is not quantified until after inference
        tl_type_env_insert(self->env, name, provisional);
    }

    // run inference
    if (infer_one(self, infer_target)) {
        log(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
            str_ilen(orig_name), str_buf(&orig_name));
        return 1;
    }

    // Must apply subs before quantifying, because we want to replace any tvs (that would otherwise be
    // quantified) with primitives if possible, or the same root of an equivalence class
    tl_type_subs_apply(self->subs, self->env);

    // get the arrow type from the annotation, or else from the result of inference
    tl_polytype const *arrow = null;
    if (name_node->symbol.annotation_type) {
        arrow = name_node->symbol.annotation_type;
    } else {
        tl_polytype const *tmp = tl_type_env_lookup(self->env, name);
        if (!tmp) fatal("runtime error");
        arrow = tl_polytype_clone(self->arena, tmp);
    }
    tl_polytype_generalize((tl_polytype *)arrow, self->env, self->subs); // const cast

    // collect free variables from infer target and add to the generic's arrow type
    add_free_variables_to_arrow(self, infer_target, (tl_polytype *)arrow); // const cast
    tl_type_env_insert(self->env, name, arrow);

    // log(self, "-- global env --");
    // log_env(self);
    // log(self, "-- subs");
    // log_subs(self);

    log(self, "-- done add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
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

        tl_polytype const *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("runtime error");

        if (tl_polytype_is_scheme(type)) array_push(names, name);
    }

    forall(i, names) {
        str_map_erase(self->toplevels, names.v[i]);
    }
    array_free(names);
}

void tree_shake_toplevels(tl_infer *self, ast_node const *start) {
    hashmap         *used   = tree_shake(self, start);

    str_array        remove = {.alloc = self->transient};

    hashmap_iterator iter   = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;

        // preserve value let-ins, but not unused let-in-lambdas (including the latter causes a test
        // failure)
        if (ast_node_is_let_in(node) && !ast_node_is_let_in_lambda(node)) continue;

        str name = toplevel_name(node);
        if (!str_hset_contains(used, name)) array_push(remove, name);
    }

    forall(i, remove) toplevel_del(self, remove.v[i]);
    array_free(remove);
    map_destroy(&used);
}

static int check_main_function(tl_infer *self, ast_node const *main) {
    // instantiate and infer main
    assert(ast_node_is_let(main));
    tl_polytype const *type = tl_type_env_lookup(self->env, S("main"));
    if (!type) fatal("main function with no type");

    tl_polytype const *body_type = main->let.body->type;
    if (!body_type || tl_polytype_is_scheme(body_type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_main_function_bad_type, .node = main}));
        return 1;
    }

    return 0;
}

static void update_specialized_types(tl_infer *self) {

    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype const *poly = *(tl_polytype const **)iter.data;
        if (!tl_polytype_is_type_constructor(poly)) continue;

        // already specialized?
        if (!str_is_empty(poly->type->cons_inst->special_name)) continue;

        str                type_name = poly->type->cons_inst->def->name;
        tl_monotype_sized  type_args = poly->type->cons_inst->args;

        tl_monotype const *mono =
          tl_type_registry_get_cached_instance(self->registry, type_name, type_args);

        if (!mono) continue;

        tl_polytype const *replace =
          tl_polytype_absorb_mono(self->arena, tl_monotype_clone(self->arena, mono));

        // update map in place
        *(tl_polytype const **)iter.data = replace;
    }

    arena_reset(self->transient);
}

int tl_infer_run(tl_infer *self, ast_node_sized nodes, tl_infer_result *out_result) {
    log(self, "-- start inference --");

    // Performs alpha-conversion on the AST to ensure all bound variables have globally unique names while
    // preserving lexical scope. This simplifies later passes by removing name collision concerns.
    {
        hashmap *lex = map_new(self->transient, str, str, 16);
        // rename toplevel let-in symbols and keep them in global lexical scope
        forall(i, nodes) rename_let_in(self, nodes.v[i], &lex);

        // rename the rest
        forall(i, nodes) rename_variables(self, nodes.v[i], &lex, 0);
        arena_reset(self->transient);
    }

    // Load all top level forms.
    self->toplevels = load_toplevel(self, self->arena, nodes, &self->errors);
    arena_reset(self->transient);
    if (self->errors.size) return 1;

    // now go through the toplevel let nodes and create generic functions: don't call add_generic from
    // inside the iteration because infer will add lambda functions to the toplevel.
    forall(i, nodes) add_generic(self, nodes.v[i]);
    arena_reset(self->transient);

    if (self->errors.size) return 1;

    // check if free variables are present
    if (check_missing_free_variables(self)) return 1;
    arena_reset(self->transient);

    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    log(self, "-- inference complete --");
    log(self, "-- toplevels");
    log_toplevels(self);
    log(self, "-- subs");
    log_subs(self);
    log(self, "-- env");
    log_env(self);
    arena_reset(self->transient);

    ast_node **found_main = str_map_get(self->toplevels, S("main"));
    if (!found_main) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_no_main_function}));
        return 1;
    }
    ast_node *main = *found_main;

    // Final phase: communiate type information top-down by following applications. This contrasts with the
    // bottom-up inference we just completed. At this point the program is well-typed and we are setting up
    // for the transpiler.
    log(self, "-- specialize phase");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    infer_ctx    *ctx      = infer_ctx_create(self->transient);
    traverse->user         = ctx;

    traverse_ast(self, traverse, main, specialize_applications_cb);

    infer_ctx_destroy(self->transient, &ctx);
    traverse_ctx_destroy(self->transient, &traverse);
    arena_reset(self->transient);

    // apply subs to global environment
    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    // update type specialisations: replace generic constructors with specialised constructors.
    update_specialized_types(self);

    // ensure main function has the correct type
    if (check_main_function(self, main)) return 1;
    arena_reset(self->transient);

    log(self, "-- final subs");
    log_subs(self);
    log(self, "-- final env --");
    log_env(self);
    arena_reset(self->transient);

    remove_generic_toplevels(self);
    arena_reset(self->transient);
    tree_shake_toplevels(self, main);
    arena_reset(self->transient);

    log(self, "-- final toplevels");
    log_toplevels(self);
    arena_reset(self->transient);

    if (self->errors.size) {
        return 1;
    }

    if (out_result) {
        out_result->registry          = self->registry;
        out_result->env               = self->env;
        out_result->toplevels         = self->toplevels;
        out_result->nodes             = nodes;
        out_result->synthesized_nodes = (ast_node_sized)sized_all(self->synthesized_nodes);
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
                fprintf(stderr, "%s:%u: %s: %.*s: %.*s\n", node->file, node->line,
                        tl_error_tag_to_string(err->tag), str_ilen(message), str_buf(&message),
                        str_ilen(node_str), str_buf(&node_str));
            }

            else
                fprintf(stderr, "error: %s: %.*s\n", tl_error_tag_to_string(err->tag), str_ilen(message),
                        str_buf(&message));
        }
    }
}

//

static void log(tl_infer const *self, char const *restrict fmt, ...) {
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
    hashmap_iterator iter = {0};
    while (map_iter(self->toplevels, &iter)) {
        ast_node const *node = *(ast_node **)iter.data;
        str             str  = v2_ast_node_to_string(self->transient, node);
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
        tl_polytype_substitute(self->arena, (tl_polytype *)node->type, self->subs); // const_cast
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
    else fatal("logic error");
}

str toplevel_name(ast_node const *node) {
    return toplevel_name_node((ast_node *)node)->symbol.name;
}

//

static void log_constraint(tl_infer *self, tl_polytype const *left, tl_polytype const *right,
                           ast_node const *node) {
    if (!self->verbose) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    log(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

static void log_type_error(tl_infer *self, tl_polytype const *left, tl_polytype const *right) {
    if (!self->verbose) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    log(self, "error: constraints are not compatible:  %s versus %s", str_cstr(&left_str),
        str_cstr(&right_str));
}
static void log_type_error_mm(tl_infer *self, tl_monotype const *left, tl_monotype const *right) {
    tl_polytype l = tl_polytype_wrap((tl_monotype *)left), r = tl_polytype_wrap((tl_monotype *)right);
    return log_type_error(self, &l, &r);
}

static void log_subs(tl_infer *self) {
    if (self->verbose) tl_type_subs_log(self->transient, self->subs);
}
