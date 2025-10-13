#include "v2_infer.h"
#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "error.h"
#include "str.h"
#include "v2_type.h"

#include "ast.h"
#include "hashmap.h"

#include "types.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct {
    enum tl_error_tag tag;
    ast_node const   *node;
    str               message;
} tl_infer_error;

typedef struct {
    array_header;
    tl_infer_error *v;
} tl_infer_error_array;

struct tl_infer {
    allocator           *transient;
    allocator           *arena;

    tl_type_registry    *registry;
    tl_type_env         *env;
    tl_type_subs        *subs;

    hashmap             *toplevels; // str => ast_node*
    hashmap             *instances; // u64 hash => str specialised name in env
    tl_infer_error_array errors;

    u32                  next_var_name;
    u32                  next_instantiation;

    int                  verbose;
    int                  indent_level;
};

//

static void log(tl_infer const *self, char const *restrict fmt, ...);
static void log_toplevels(tl_infer const *);
static void log_env(tl_infer const *);
static void log_subs(tl_infer *);

//

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self           = new (alloc, tl_infer);

    self->transient          = arena_create(alloc, 4096);
    self->arena              = arena_create(alloc, 16 * 1024);
    self->registry           = tl_type_registry_create(self->arena);
    self->env                = tl_type_env_create(self->arena, self->transient);
    self->subs               = tl_type_subs_create(self->arena);
    self->toplevels          = null;
    self->instances          = map_create(self->arena, sizeof(str), 512);
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

static tl_type_v2 *make_type_annotation(tl_infer *self, ast_node *ann, hashmap **map) {
    if (ast_nil == ann->tag) {
        return tl_type_registry_create_type_poly(self->registry, S("Nil"), null);
    }

    // if (ast_ellipsis == ann->tag) {
    //     tl_type **found = type_registry_find_name(self->type_registry, S("ellipsis"));
    //     if (found) return *found;
    //     fatal("ellipsis type not found");
    // }

    if (ast_symbol == ann->tag) {
        // either a prim or user type, or a generic/quantifier
        str               ann_str = ann->symbol.name;
        tl_type_v2 const *found   = tl_type_env_lookup(self->env, ann_str);
        if (found) {
            // If it's an any type, assign it a new quantifier
            // if (type_any == (*found)->tag) return tl_type_context_new_quantifier(self);
            return tl_polytype_clone(self->arena, found);
        }

        // previously seen in the annotation? then assign same type
        {
            tl_type_v2 **map_found = str_map_get(*map, ann_str);
            if (map_found) return tl_polytype_clone(self->arena, *map_found);
        }

        // unknown symbol, consider it as a quantifier
        tl_type_v2 *out = tl_polytype_create_fresh_qv(self->arena, self->subs);
        str_map_set(map, ann_str, &out);
        return out;
    }

    // if (ast_tuple == ann->tag) {
    //     struct ast_tuple *v        = ast_node_tuple(ann);
    //     tl_type_array     elements = {.alloc = self->type_arena};
    //     array_reserve(elements, v->n_elements);

    //     for (u32 i = 0; i < v->n_elements; ++i) {
    //         tl_type *res = make_type_annotation(self, v->elements[i], map);
    //         array_push(elements, res);
    //     }

    //     return tl_type_create_tuple(self->type_arena, (tl_type_sized)sized_all(elements));
    // }

    if (ast_arrow == ann->tag) {
        tl_type_v2 *left  = make_type_annotation(self, ann->arrow.left, map);
        tl_type_v2 *right = make_type_annotation(self, ann->arrow.right, map);

        if (!tl_monotype_is_list(left->type)) {
            left = tl_polytype_absorb_mono(self->arena, tl_monotype_create_list(self->arena, left->type));
        }

        tl_polytype_list_append(self->arena, left, right);
        return left;
    }

    // if (ast_address_of == ann->tag) {
    //     tl_type *target     = make_type_annotation(self, ann->address_of.target, map);
    //     tl_type *ptr        = tl_type_create(self->type_arena, type_pointer);
    //     ptr->pointer.target = target;
    //     return ptr;
    // }

    // fatal("unknown annotation type: '%s'", ast_tag_to_string(ann->tag));
    return null; // FIXME
}

static void process_annotation(tl_infer *self, ast_node *node) {
    ast_node const *name = toplevel_name_node(node);

    if (!name->symbol.annotation) return;

    hashmap    *map                 = map_create(self->transient, sizeof(tl_type_v2 *), 8);
    tl_type_v2 *ann                 = make_type_annotation(self, name->symbol.annotation, &map);
    node->symbol.annotation_type_v2 = ann;

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
                if (node->let.name->symbol.annotation) continue;

                // apply annotation
                node->let.name->symbol.annotation = (*p)->symbol.annotation;
                process_annotation(self, node->let.name);

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

        else {
            array_push(errors, ((tl_infer_error){.tag = tl_err_invalid_toplevel, .node = node}));
            continue;
        }
    }

    *out_errors = errors;
    return tops;
}

// -- tree shake --

static ast_node *toplevel_get(tl_infer *, str);

typedef struct {
    tl_infer *self;
    hashmap  *names; // str set
} tree_shake_ctx;

void do_tree_shake(void *ctx_, ast_node *node) {
    tree_shake_ctx *ctx = ctx_;

    if (ast_node_is_nfa(node)) {
        str name = toplevel_name(node);

        // add all symbol arguments because they could be function pointers
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(arg)) continue;
            if (str_eq(name, arg->symbol.name)) continue;
            str_hset_insert(&ctx->names, arg->symbol.name);
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
    void    *user;
} traverse_ctx;

typedef int (*traverse_cb)(tl_infer *, traverse_ctx *, ast_node *);

static traverse_ctx *traverse_ctx_create(allocator *alloc) {
    traverse_ctx *out = new (alloc, traverse_ctx);
    out->call_chain   = hset_create(alloc, 16);
    out->user         = null;

    return out;
}

static void traverse_ctx_destroy(allocator *alloc, traverse_ctx **p) {
    if ((*p)->call_chain) hset_destroy(&(*p)->call_chain);

    alloc_free(alloc, *p);
    *p = null;
}

typedef struct {
    hashmap *specials; // str => str
} infer_ctx;

static infer_ctx *infer_ctx_create(allocator *alloc) {
    infer_ctx *out = new (alloc, infer_ctx);
    out->specials  = map_create(alloc, sizeof(str), 16);

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

static void log_constraint(tl_infer *, tl_type_v2 const *, tl_type_v2 const *, ast_node const *);
static void log_type_error(tl_infer *, tl_type_v2 const *, tl_type_v2 const *);
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

static int constrain_mono(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    type_error_cb_ctx error_ctx = {.self = self, .node = node};
    return tl_type_subs_unify_mono(self->subs, left, right, type_error_cb, &error_ctx);
}

static int constrain(tl_infer *self, infer_ctx *ctx, tl_type_v2 *left, tl_type_v2 *right,
                     ast_node const *node) {
    (void)ctx;

    if (left == right) return 0;
    log_constraint(self, left, right, node);

    tl_monotype *lhs = null, *rhs = null;

    if (left->quantifiers.size) lhs = tl_polytype_instantiate(self->arena, left, self->subs);
    else lhs = left->type;
    if (right->quantifiers.size) rhs = tl_polytype_instantiate(self->arena, right, self->subs);
    else rhs = right->type;

    return constrain_mono(self, lhs, rhs, node);
}

static void ensure_tv(tl_infer *self, str const *name, tl_type_v2 **type) {
    if (!type) return;
    if (*type) return;
    if (name) *type = tl_polytype_clone(self->arena, (tl_type_env_lookup(self->env, *name)));
    if (*type) return;

    *type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

static void        rename_variables(tl_infer *, ast_node *, hashmap **);
static str         specialize_fun(tl_infer *, infer_ctx *, ast_node *, tl_monotype *);
static tl_type_v2 *make_arrow(tl_infer *, ast_node_sized, ast_node *);

static int         is_name_instanatiated(tl_infer *self, ast_node *name) {
    assert(ast_node_is_symbol(name));
    tl_polytype const *poly = tl_type_env_lookup(self->env, name->symbol.name);
    return poly && !poly->quantifiers.size;
}

static ast_node *clone_generic(allocator *alloc, ast_node const *node) {
    ast_node *clone = ast_node_clone(alloc, node);
    ast_node *name  = toplevel_name_node(clone);
    assert(ast_node_is_symbol(name));
    name->symbol.annotation_type_v2 = null;
    name->symbol.annotation         = null;
    return clone;
}

static void toplevel_add(tl_infer *, str, ast_node *);

static int  traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    if (null == node) return 0;

    switch (node->tag) {
    case ast_let: {

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            ensure_tv(self, null, &param->type_v2);
            if (cb(self, ctx, param)) return 1;
        }

        if (traverse_ast(self, ctx, node->let.body, cb)) return 1;

    } break;

    case ast_let_in: {

        // process node first, because there may be side effects required before traversing body.
        if (cb(self, ctx, node)) return 1;

        // traverse value first, then traverse body
        if (traverse_ast(self, ctx, node->let_in.value, cb)) return 1;

        ensure_tv(self, null, &node->let_in.name->type_v2);
        if (cb(self, ctx, node->let_in.name)) return 1;

        if (traverse_ast(self, ctx, node->let_in.body, cb)) return 1;

    } break;

    case ast_named_function_application: {

        str name = node->named_application.name->symbol.name;

        // do not process recursive calls
        if (str_hset_contains(ctx->call_chain, name)) {
            log(self, "detected recursive call to '%.*s'", str_ilen(name), str_buf(&name));
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

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            ensure_tv(self, null, &param->type_v2);
            if (cb(self, ctx, param)) return 1;
        }

        if (traverse_ast(self, ctx, node->lambda_function.body, cb)) return 1;

        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_lambda_function_application: {

        ensure_tv(self, null, &node->type_v2);

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

        // FIXME: complete the misisng traversals for the various ast types below

    case ast_nil:
    case ast_any:
    case ast_address_of:
    case ast_arrow:
    case ast_assignment:
    case ast_bool:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_let_match_in:
    case ast_string:
    case ast_symbol:
    case ast_u64:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_tuple:
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
    case ast_address_of: fatal("FIXME: pointer types");

    case ast_string:     {
        tl_type_v2 *ty = tl_type_registry_create_type_poly(self->registry, S("String"), null);
        ensure_tv(self, null, &node->type_v2);
        if (constrain(self, ctx, node->type_v2, ty, node)) return 1;
    } break;

    case ast_f64: {
        tl_type_v2 *ty = tl_type_registry_create_type_poly(self->registry, S("Float"), null);
        ensure_tv(self, null, &node->type_v2);
        if (constrain(self, ctx, node->type_v2, ty, node)) return 1;
    } break;

    case ast_i64: {
        tl_type_v2 *ty = tl_type_registry_create_type_poly(self->registry, S("Int"), null);
        ensure_tv(self, null, &node->type_v2);
        if (constrain(self, ctx, node->type_v2, ty, node)) return 1;
    } break;

    case ast_u64: {
        tl_type_v2 *ty =
          tl_type_registry_create_type_poly(self->registry, S("Int"), null); // FIXME unsigned
        ensure_tv(self, null, &node->type_v2);
        if (constrain(self, ctx, node->type_v2, ty, node)) return 1;
    } break;

    case ast_bool: {
        tl_type_v2 *ty = tl_type_registry_create_type_poly(self->registry, S("Bool"), null);
        ensure_tv(self, null, &node->type_v2);
        if (constrain(self, ctx, node->type_v2, ty, node)) return 1;
    } break;

    case ast_let_in: {
        ensure_tv(self, null, &node->type_v2);
        ensure_tv(self, null, &node->let_in.name->type_v2);
        ensure_tv(self, null, &node->let_in.value->type_v2);
        ensure_tv(self, null, &node->let_in.body->type_v2);

        if (ast_node_is_lambda_function(node->let_in.value)) {

            str name = node->let_in.name->symbol.name;

            // define a generic lambda
            if (add_generic(self, node)) return 1;

            // add let-in node to toplevels (because we need the name and the body)
            toplevel_add(self, name, node);

            // Do not infer the node value - add_generic takes care of that.
            // Instead, trigger runtime problems if the name's type is referenced using the expression type
            // (rather than the type_env type).
            node->let_in.name->type_v2 = null;

            if (constrain(self, ctx, node->type_v2, node->let_in.body->type_v2, node)) return 1;

        } else {

            if (constrain(self, ctx, node->let_in.name->type_v2, node->let_in.value->type_v2, node))
                return 1;

            // add value to environment
            tl_type_env_insert(self->env, node->let_in.name->symbol.name, node->let_in.value->type_v2);

            if (constrain(self, ctx, node->type_v2, node->let_in.body->type_v2, node)) return 1;
        }
    } break;

    case ast_let: {

        ast_arguments_iter iter  = ast_node_arguments_iter(node);
        tl_type_v2        *arrow = make_arrow(self, iter.nodes, node->let.body);
        tl_polytype_substitute(self->arena, arrow, self->subs);
        tl_type_env_insert(self->env, node->let.name->symbol.name, arrow);

    } break;

    case ast_symbol: {
        tl_type_v2 const *global = tl_type_env_lookup(self->env, node->symbol.name);

        if (global) {
            tl_type_v2 *global_copy = tl_polytype_clone(self->arena, global);
            if (node->type_v2) {
                if (constrain(self, ctx, node->type_v2, global_copy, node)) return 1;
            } else {
                node->type_v2 = global_copy;
            }
        }

        else {
            ensure_tv(self, &node->symbol.name, &node->type_v2);
        }

        // add to environment
        if (!global) tl_type_env_insert(self->env, node->symbol.name, node->type_v2);

    } break;

    case ast_named_function_application: {

        ensure_tv(self, null, &node->type_v2);

        str               name = node->named_application.name->symbol.name;
        tl_type_v2 const *type = tl_type_env_lookup(self->env, name);
        if (!type) {
            array_push(self->errors,
                       ((tl_infer_error){.tag = tl_err_function_not_found, .message = name, .node = node}));
            return 1;
        }

        // instantiate generic function type being applied
        ast_arguments_iter iter     = ast_node_arguments_iter(node);
        tl_monotype       *inst     = tl_polytype_instantiate(self->arena, type, self->subs);
        str                inst_str = tl_monotype_to_string(self->transient, inst);
        tl_polytype       *app      = make_arrow(self, iter.nodes, node);
        str                app_str  = tl_polytype_to_string(self->transient, app);
        log(self, "application: callsite '%.*s' (%.*s) arrow: %.*s", str_ilen(name), str_buf(&name),
            str_ilen(inst_str), str_buf(&inst_str), str_ilen(app_str), str_buf(&app_str));
        tl_polytype wrap = tl_polytype_wrap(inst);

        // and constrain it with the callsite types (arguments -> result)
        if (constrain(self, ctx, &wrap, app, node)) return 1;

    } break;

    case ast_lambda_function_application: {

        // Instantiate and save type since it will never be generic.
        tl_monotype *inst =
          tl_polytype_instantiate(self->arena, node->lambda_application.lambda->type_v2, self->subs);
        node->lambda_application.lambda->type_v2 = tl_polytype_absorb_mono(self->arena, inst);

        // constrain arrow types
        ast_arguments_iter iter     = ast_node_arguments_iter(node);
        tl_polytype       *app      = make_arrow(self, iter.nodes, node);
        tl_polytype        wrap     = tl_polytype_wrap(inst);
        str                inst_str = tl_monotype_to_string(self->transient, inst);
        str                app_str  = tl_polytype_to_string(self->transient, app);
        log(self, "application: anon lambda %.*s callsite arrow: %.*s", str_ilen(inst_str),
            str_buf(&inst_str), str_ilen(app_str), str_buf(&app_str));
        if (constrain(self, ctx, &wrap, app, node)) return 1;

        // constain node type to the lambda body type
        if (constrain(self, ctx, node->type_v2,
                      node->lambda_application.lambda->lambda_function.body->type_v2, node))
            return 1;

    } break;

    case ast_lambda_function: {

        if (!node->type_v2) {
            ast_arguments_iter iter  = ast_node_arguments_iter(node);
            tl_type_v2        *arrow = make_arrow(self, iter.nodes, node->lambda_function.body);
            tl_polytype_generalize(arrow, self->env, self->subs);
            node->type_v2 = arrow;
        }

        // Note: it is an error to set an expression type on the lambda function node when the lambda is
        // let-in defined.

    } break;

    case ast_if_then_else: {

        tl_type_v2 *bool_type = tl_type_registry_create_type_poly(self->registry, S("Bool"), null);
        if (constrain(self, ctx, node->if_then_else.condition->type_v2, bool_type, node)) return 1;
        if (constrain(self, ctx, node->if_then_else.yes->type_v2, node->if_then_else.no->type_v2, node))
            return 1;
        ensure_tv(self, null, &node->type_v2);
        if (constrain(self, ctx, node->type_v2, node->if_then_else.yes->type_v2, node)) return 1;
    } break;

    case ast_arrow:
    case ast_assignment:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_let_match_in:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_tuple:
    case ast_user_type:            break;
    }

    // apply newly created constraint substitutions
    tl_type_subs_apply(self->subs, self->env);
    return 0;
}

static ast_node *get_infer_target(ast_node *node) {
    if (ast_node_is_let(node)) {
        return node;
    }

    else if (ast_node_is_let_in_lambda(node)) {
        return node->let_in.value;
    }

    else if (ast_node_is_symbol(node)) {
        return null;
    }

    return null;
}

static int specialize_traverse_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {

    if (!ast_node_is_nfa(node)) return 0;

    infer_ctx        *ctx  = traverse_ctx->user;

    str               name = node->named_application.name->symbol.name;
    tl_type_v2 const *type = tl_type_env_lookup(self->env, name);
    if (!type) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_function_not_found, .message = name, .node = node}));
        return 1;
    }

    // instantiate generic function type being applied
    ast_arguments_iter iter     = ast_node_arguments_iter(node);
    ast_node          *fun_node = toplevel_get(self, name);
    if (!fun_node) return 0; // too early

    tl_polytype *app = make_arrow(self, iter.nodes, node);

    // Important: resolve type variables by calling polytype_substitute.
    tl_polytype_substitute(self->arena, app, self->subs);

    str app_str = tl_polytype_to_string(self->transient, app);
    log(self, "specialize application: callsite '%.*s' arrow: %.*s", str_ilen(name), str_buf(&name),
        str_ilen(app_str), str_buf(&app_str));

    // Important: If type at the callsite is not fully concrete, do not specialise. It must remain
    // polymorphic until all concrete type information is known.
    if (!tl_polytype_is_concrete(app)) return 0;

    // check if we already have a specialisation by name, because specialize_fun de-duplicates using name +
    // type, e.g the identity function
    str *existing;
    if ((existing = str_map_get(ctx->specials, name))) {
        str inst_name                                 = *existing;

        node->named_application.name->symbol.original = name;
        node->named_application.name->symbol.name     = inst_name;

        // infer the instance (again), but don't recurse through its applications
        ast_node *infer_target = get_infer_target(toplevel_get(self, inst_name));
        if (infer_target) {
            if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) return 1;
        }
    } else {
        str       inst_name = specialize_fun(self, ctx, fun_node, app->type);
        ast_node *special   = toplevel_get(self, inst_name);

        str_map_set(&ctx->specials, name, &inst_name);

        node->named_application.name->symbol.original = name;
        node->named_application.name->symbol.name     = inst_name;

        // now infer and specialize the newly specialised fun
        ast_node *infer_target = get_infer_target(special);
        if (infer_target) {
            if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) return 1;
            if (traverse_ast(self, traverse_ctx, infer_target, specialize_traverse_cb)) return 1;
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
static void rename_variables(tl_infer *self, ast_node *node, hashmap **lex) {

    if (null == node) return;

    // ensure all types are removed: important for the post-clone rename of functions being specialized.
    node->type_v2 = null;

    switch (node->tag) {

    case ast_address_of:  rename_variables(self, node->address_of.target, lex); break;
    case ast_dereference: rename_variables(self, node->dereference.target, lex); break;

    case ast_dereference_assign:
        rename_variables(self, node->dereference_assign.target, lex);
        rename_variables(self, node->dereference_assign.value, lex);
        break;

    case ast_if_then_else:
        rename_variables(self, node->if_then_else.condition, lex);
        rename_variables(self, node->if_then_else.yes, lex);
        rename_variables(self, node->if_then_else.no, lex);
        break;

    case ast_let_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, lex);

        str name                           = node->let_in.name->symbol.name;
        str newvar                         = next_variable_name(self);
        node->let_in.name->symbol.original = node->let_in.name->symbol.name;
        node->let_in.name->symbol.name     = newvar;
        log(self, "rename %.*s => %.*s", str_ilen(node->let_in.name->symbol.original),
            str_buf(&node->let_in.name->symbol.original), str_ilen(node->let_in.name->symbol.name),
            str_buf(&node->let_in.name->symbol.name));

        // establish lexical scope of the let-in binding and recurse
        hashmap *save = map_copy(*lex);
        str_map_set(lex, name, &newvar);

        rename_variables(self, node->let_in.body, lex);

        // restore prior scope
        map_destroy(lex);
        *lex = save;
    } break;

    case ast_let_match_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, lex);

        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->let_match_in.lt->labelled_tuple.n_assignments; ++i) {
            ast_node *ass = node->let_match_in.lt->labelled_tuple.assignments[i];
            assert(ast_node_is_assignment(ass));
            ast_node *name_node = ass->assignment.name;
            assert(ast_node_is_symbol(name_node));
            str name                   = name_node->symbol.name;
            str newvar                 = next_variable_name(self);
            name_node->symbol.original = name_node->symbol.name;
            name_node->symbol.name     = newvar;

            str_map_set(lex, name, &newvar);
        }

        rename_variables(self, node->let_match_in.body, lex);

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
        node->type_v2 = null;
    } break;

    case ast_lambda_function: {
        // establish lexical scope for formal parameters and recurse
        hashmap           *save = map_copy(*lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str name               = param->symbol.name;
            str newvar             = next_variable_name(self);
            param->symbol.original = param->symbol.name;
            param->symbol.name     = newvar;
            str_map_set(lex, name, &newvar);
            rename_variables(self, param, lex);
        }

        rename_variables(self, node->lambda_function.body, lex);

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
            str name               = param->symbol.name;
            str newvar             = next_variable_name(self);
            param->symbol.original = param->symbol.name;
            param->symbol.name     = newvar;
            str_map_set(lex, name, &newvar);
            rename_variables(self, param, lex);
        }

        rename_variables(self, node->let.body, lex);

        map_destroy(lex);
        *lex = save;

    } break;

    case ast_begin_end:
        for (u32 i = 0; i < node->begin_end.n_expressions; ++i)
            rename_variables(self, node->begin_end.expressions[i], lex);
        break;

    case ast_lambda_function_application: {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, lex);

        // establishes scope for lambda body
        rename_variables(self, node->lambda_application.lambda, lex);
    } break;

    case ast_named_function_application: {
        rename_variables(self, node->named_application.name, lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, lex);

    } break;

    case ast_user_type_get:        rename_variables(self, node->user_type_get.struct_name, lex); break;
    case ast_user_type_set:        rename_variables(self, node->user_type_set.struct_name, lex); break;

    case ast_assignment:
    case ast_labelled_tuple:
    case ast_tuple:
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

static void collect_free_variables(tl_infer *self, ast_node *node, hashmap **lex, str_array *fvs) {

    if (null == node) return;

    switch (node->tag) {

    case ast_address_of:  collect_free_variables(self, node->address_of.target, lex, fvs); break;
    case ast_dereference: collect_free_variables(self, node->dereference.target, lex, fvs); break;

    case ast_dereference_assign:
        collect_free_variables(self, node->dereference_assign.target, lex, fvs);
        collect_free_variables(self, node->dereference_assign.value, lex, fvs);
        break;

    case ast_if_then_else:
        collect_free_variables(self, node->if_then_else.condition, lex, fvs);
        collect_free_variables(self, node->if_then_else.yes, lex, fvs);
        collect_free_variables(self, node->if_then_else.no, lex, fvs);
        break;

    case ast_let: {
        // establish lexical scope for formal parameters and recurse
        hashmap           *save = map_copy(*lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str name = param->symbol.name;
            str_map_set(lex, name, &name);
        }

        collect_free_variables(self, node->let.body, lex, fvs);

        map_destroy(lex);
        *lex = save;

    } break;

    case ast_let_in: {

        // recurse on value prior to adding name to lexical scope
        collect_free_variables(self, node->let_in.value, lex, fvs);

        str name = node->let_in.name->symbol.name;

        // establish lexical scope of the let-in binding and recurse
        hashmap *save = map_copy(*lex);
        str_map_set(lex, name, &name);

        collect_free_variables(self, node->let_in.body, lex, fvs);

        // restore prior scope
        map_destroy(lex);
        *lex = save;
    } break;

    case ast_let_match_in: {

        // recurse on value prior to adding name to lexical scope
        collect_free_variables(self, node->let_in.value, lex, fvs);

        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->let_match_in.lt->labelled_tuple.n_assignments; ++i) {
            ast_node *ass = node->let_match_in.lt->labelled_tuple.assignments[i];
            assert(ast_node_is_assignment(ass));
            ast_node *name_node = ass->assignment.name;
            assert(ast_node_is_symbol(name_node));
            str name = name_node->symbol.name;

            str_map_set(lex, name, &name);
        }

        collect_free_variables(self, node->let_match_in.body, lex, fvs);

        map_destroy(lex);
        *lex = save;
    } break;

    case ast_symbol: {
        str              *found;
        tl_type_v2 const *type     = tl_type_env_lookup(self->env, node->symbol.name);
        int               is_arrow = type && tl_monotype_is_arrow(type->type);

        // Note: arrow types in the environment are global functions and are not free variables. Note that
        // even local let-in-lambda functions are also in the environment, but their names will never clash
        // with function names.
        if (is_arrow || (found = str_map_get(*lex, node->symbol.name))) {
            ;
        } else {
            // a free variable
            array_set_insert(*fvs, node->symbol.name);
        }

        // if symbol has a type which carries fvs, we also collect those.
        // TODO so many indirections
        if (is_arrow && !tl_polytype_is_scheme(type)) {
            str_sized type_fvs = tl_monotype_fvs(type->type);
            forall(i, type_fvs) {
                array_set_insert(*fvs, type_fvs.v[i]);
            }
        }
    } break;

    case ast_lambda_function: {
        // establish lexical scope for formal parameters and recurse
        hashmap           *save = map_copy(*lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str name = param->symbol.name;
            str_map_set(lex, name, &name);
        }

        collect_free_variables(self, node->lambda_function.body, lex, fvs);

        map_destroy(lex);
        *lex = save;
    } break;

    case ast_begin_end:
        for (u32 i = 0; i < node->begin_end.n_expressions; ++i)
            collect_free_variables(self, node->begin_end.expressions[i], lex, fvs);
        break;

    case ast_lambda_function_application:
        collect_free_variables(self, node->lambda_application.lambda, lex, fvs);
        break;

    case ast_named_function_application: {
        collect_free_variables(self, node->named_application.name, lex, fvs);
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) collect_free_variables(self, arg, lex, fvs);

    } break;

    case ast_user_type_get:        collect_free_variables(self, node->user_type_get.struct_name, lex, fvs); break;
    case ast_user_type_set:        collect_free_variables(self, node->user_type_set.struct_name, lex, fvs); break;

    case ast_assignment:
    case ast_labelled_tuple:
    case ast_tuple:
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

typedef struct {
    str          name;
    tl_monotype *type;
} name_and_type;

static str next_instantiation(tl_infer *, str);

static str specialize_fun(tl_infer *self, infer_ctx *ctx, ast_node *node, tl_monotype *arrow) {
    (void)ctx;
    str name = toplevel_name(node);

    // de-duplicate instances. Note however that in many cases the arrow type will contain unique type
    // vars that have not been inferred yet.
    name_and_type key      = {.name = name, .type = arrow};
    str          *existing = map_get(self->instances, &key, sizeof key);
    if (existing) return *existing;

    // instantiate unique name
    str name_inst = next_instantiation(self, name);
    map_set(&self->instances, &key, sizeof key, &name_inst);

    // add to type environment
    tl_type_env_insert_mono(self->env, name_inst, arrow);

    // clone function source ast and rename variables
    ast_node *generic_node = clone_generic(self->transient, toplevel_get(self, name));
    hashmap  *rename_lex   = map_create(self->transient, sizeof(str), 16);
    rename_variables(self, generic_node, &rename_lex);
    map_destroy(&rename_lex);

    ast_node      *body   = null;
    ast_node_sized params = {0};
    if (ast_node_is_let(generic_node)) {
        body                                    = generic_node->let.body;
        params                                  = ast_node_sized_from_ast_array(generic_node);
        generic_node->let.name->symbol.original = generic_node->let.name->symbol.name;
        generic_node->let.name->symbol.name     = name_inst;
    } else if (ast_node_is_let_in_lambda(generic_node)) {
        body   = generic_node->let_in.value->lambda_function.body;
        params = ast_node_sized_from_ast_array(generic_node->let_in.value);
        generic_node->let_in.name->symbol.original = generic_node->let_in.name->symbol.name;
        generic_node->let_in.name->symbol.name     = name_inst;
    } else if (ast_node_is_symbol(generic_node)) {
        // no body
        ;
    } else {
        fatal("logic error");
    }

    if (body) {
        // assign concrete types to parameters
        assert(tl_list == arrow->tag);

        tl_monotype *args = arrow->list.head;
        forall(i, params) {
            assert(args);
            ast_node *param = params.v[i];
            param->type_v2  = tl_polytype_absorb_mono(self->arena, tl_monotype_clone(self->arena, args));
            args            = args->next;
        }

        tl_monotype const *inst_result = tl_monotype_list_last(arrow);
        body->type_v2 = tl_polytype_absorb_mono(self->arena, tl_monotype_clone(self->arena, inst_result));

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

static tl_type_v2 *make_arrow(tl_infer *self, ast_node_sized args, ast_node *result) {

    ensure_tv(self, null, &result->type_v2);

    if (args.size == 0) {
        tl_monotype *lhs   = tl_type_registry_create_type(self->registry, S("Nil"), null);
        tl_monotype *rhs   = tl_monotype_clone(self->arena, result->type_v2->type);
        tl_monotype *arrow = tl_monotype_create_arrow(self->arena, lhs, rhs);

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
            tl_monotype *mono = tl_type_registry_create_type(self->registry, S("Nil"), null);
            if (!mono) fatal("runtime error");
            args.v[0]->type_v2 = tl_polytype_absorb_mono(self->arena, mono);
        } else ensure_tv(self, null, &args.v[0]->type_v2);

        tl_monotype *lhs   = tl_monotype_clone(self->arena, args.v[0]->type_v2->type);
        tl_monotype *rhs   = tl_monotype_clone(self->arena, result->type_v2->type);
        tl_monotype *arrow = tl_monotype_create_arrow(self->arena, lhs, rhs);
        {
            str str = tl_monotype_to_string(self->transient, arrow);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_polytype_absorb_mono(self->arena, arrow);
    }

    else {

        // more than 1 arg, we need to create a flat list (not recursive)
        ensure_tv(self, null, &args.v[0]->type_v2);
        tl_monotype *head = tl_monotype_clone(self->arena, args.v[0]->type_v2->type);
        tl_monotype *hd   = head;
        for (u32 i = 1; i < args.size; ++i) {
            ensure_tv(self, null, &args.v[i]->type_v2);
            tl_monotype *arg = tl_monotype_clone(self->arena, args.v[i]->type_v2->type);
            hd->next         = arg;
            hd               = arg;
        }
        hd->next         = result->type_v2->type;

        tl_monotype *out = tl_monotype_create_list(self->arena, head);
        {
            str str = tl_monotype_to_string(self->transient, out);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }

        return tl_polytype_absorb_mono(self->arena, out);
    }
}

static void add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow) {
    // collect free variables from infer target and add to the generic's arrow type
    str_array fvs = {.alloc = self->arena};
    {
        hashmap *lex = map_create(self->transient, sizeof(str), 16);
        collect_free_variables(self, node, &lex, &fvs);
        map_destroy(&lex);

        array_shrink(fvs);
        log(self, "-- free variables: %u --", fvs.size);
        forall(i, fvs) {
            log(self, "%.*s", str_ilen(fvs.v[i]), str_buf(&fvs.v[i]));
        }
    }

    // add free variables to arrow type and put into global environment
    if (fvs.size) {
        tl_monotype_absorb_fvs(self->arena, arrow->type, (str_sized)sized_all(fvs));
        tl_monotype_sort_fvs(arrow->type);
    }
}

static int generic_declaration(tl_infer *self, str name, ast_node const *name_node, ast_node *node) {
    // no function body, so let's treat this as a type declaration
    if (!name_node->symbol.annotation_type_v2) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
        return 1;
    }

    // must quantify arrow types
    if (tl_monotype_is_arrow(node->symbol.annotation_type_v2->type))
        tl_polytype_generalize(node->symbol.annotation_type_v2, self->env, self->subs);
    tl_type_env_insert(self->env, name, node->symbol.annotation_type_v2);
    return 0;
}

static int add_generic(tl_infer *self, ast_node *node) {
    if (!node) return 0;

    ast_node    *infer_target = get_infer_target(node);
    ast_node    *name_node    = toplevel_name_node(node);
    tl_polytype *provisional  = null;

    if (ast_node_is_let(node)) {
        provisional = make_arrow(self, ast_node_sized_from_ast_array(node), node->let.body);
    }

    else if (ast_node_is_let_in_lambda(node)) {
        provisional = make_arrow(self, ast_node_sized_from_ast_array(infer_target),
                                 node->let_in.value->lambda_function.body);
    }

    else if (ast_node_is_symbol(node)) {
        // toplevel symbol node, e.g. for declaration of intrinsics, or forward type annotations. They will
        // take precedence to any later declarations, so let's be careful
    }

    else {
        fatal("logic error");
    }

    str name      = name_node->symbol.name;
    str orig_name = name_node->symbol.original;

    // do not process a second time
    if (tl_type_env_lookup(self->env, name)) fatal("runtime error");

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
    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    infer_ctx    *ctx      = infer_ctx_create(self->transient);
    traverse->user         = ctx;
    if (traverse_ast(self, traverse, infer_target, infer_traverse_cb)) {
        log(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
            str_ilen(orig_name), str_buf(&orig_name));
        return 1;
    }

    // Must apply subs before quantifying, because we want to replace any tvs (that would otherwise be
    // quantified) with primitives if possible, or the same root of an equivalence class
    tl_type_subs_apply(self->subs, self->env);

    // get the arrow type from the annotation, or else from the result of inference
    tl_type_v2 *arrow = null;
    if (name_node->symbol.annotation_type_v2) {
        arrow = name_node->symbol.annotation_type_v2;
    } else {
        tl_polytype const *tmp = tl_type_env_lookup(self->env, name);
        if (!tmp) fatal("runtime error");
        arrow = tl_polytype_clone(self->arena, tmp);
    }
    tl_polytype_generalize(arrow, self->env, self->subs);

    // collect free variables from infer target and add to the generic's arrow type
    add_free_variables_to_arrow(self, infer_target, arrow);
    tl_type_env_insert(self->env, name, arrow);

    log(self, "-- global env --");
    log_env(self);
    log(self, "-- subs");
    log_subs(self);

    log(self, "-- done add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    infer_ctx_destroy(self->transient, &ctx);
    traverse_ctx_destroy(self->transient, &traverse);
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

static ast_node *toplevel_iter(tl_infer *, hashmap_iterator *);

void             remove_generic_toplevels(tl_infer *self) {
    str_array        names = {.alloc = self->transient};

    ast_node        *node;
    hashmap_iterator iter = {0};
    while ((node = toplevel_iter(self, &iter))) {
        str               name;
        tl_type_v2 const *type = null;
        if (ast_node_is_symbol(node)) {
            name = node->symbol.name;
            type = tl_type_env_lookup(self->env, name);
        } else if (ast_node_is_let_in_lambda(node)) {
            name = node->let_in.name->symbol.name;
            type = tl_type_env_lookup(self->env, name);
        } else if (ast_node_is_let(node)) {
            name = node->let.name->symbol.name;
            type = tl_type_env_lookup(self->env, name);
        } else fatal("logic error");
        if (str_eq(S("main"), name)) continue;
        if (tl_polytype_is_scheme(type)) array_push(names, name);
    }

    forall(i, names) {
        str_map_erase(self->toplevels, names.v[i]);
    }
    array_free(names);
}

static void toplevel_del(tl_infer *self, str name);

void        tree_shake_toplevels(tl_infer *self, ast_node const *start) {
    hashmap         *used   = tree_shake(self, start);

    str_array        remove = {.alloc = self->transient};

    hashmap_iterator iter   = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
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
    tl_type_v2 const *type = tl_type_env_lookup(self->env, S("main"));
    if (!type) fatal("main function with no type");

    tl_type_v2 const *body_type = main->let.body->type_v2;
    if (!body_type || tl_polytype_is_scheme(body_type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_main_function_bad_type, .node = main}));
        return 1;
    }

    return 0;
}

static void apply_subs_to_ast(tl_infer *);

int         tl_infer_run(tl_infer *self, ast_node_sized nodes, tl_infer_result *out_result) {
    log(self, "-- start inference --");

    self->toplevels = load_toplevel(self, self->arena, nodes, &self->errors);

    if (self->errors.size) {
        return 1;
    }

    // Performs alpha-conversion on the AST to ensure all bound variables have globally unique names while
    // preserving lexical scope. This simplifies later passes by removing name collision concerns.
    {
        hashmap *lex = map_create(self->transient, sizeof(str), 16);
        forall(i, nodes) {
            rename_variables(self, nodes.v[i], &lex);
        }
        map_destroy(&lex);
    }

    // now go through the toplevel let nodes and create generic functions: don't call add_generic from
    // inside the iteration because infer will add lambda functions to the toplevel.
    forall(i, nodes) add_generic(self, nodes.v[i]);

    if (self->errors.size) return 1;

    // check if free variables are present
    if (check_missing_free_variables(self)) return 1;

    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);

    log(self, "-- inference complete --");
    log(self, "-- toplevels");
    log_toplevels(self);
    log(self, "-- subs");
    log_subs(self);
    log(self, "-- env");
    log_env(self);

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

    traverse_ast(self, traverse, main, specialize_traverse_cb);

    infer_ctx_destroy(self->transient, &ctx);
    traverse_ctx_destroy(self->transient, &traverse);

    log(self, "-- toplevels");
    log_toplevels(self);
    log(self, "-- subs");
    log_subs(self);
    log(self, "-- env");
    log_env(self);

    // apply subs to global environment
    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);

    // ensure main function has the correct type
    if (check_main_function(self, main)) return 1;

    log(self, "-- final subs");
    log_subs(self);
    log(self, "-- final env --");
    log_env(self);

    remove_generic_toplevels(self);
    tree_shake_toplevels(self, main);

    log(self, "-- final toplevels");
    log_toplevels(self);

    if (self->errors.size) {
        return 1;
    }

    if (out_result) {
        out_result->env       = self->env;
        out_result->toplevels = self->toplevels;
    }
    return 0;
}

static str v2_ast_node_to_string(allocator *, ast_node const *);

void       tl_infer_report_errors(tl_infer *self) {
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

static str v2_ast_node_to_string(allocator *alloc, ast_node const *node) {
    char buf[64];

    str  ty_str = node->type_v2 ? tl_polytype_to_string(alloc, node->type_v2) : str_empty();

    switch (node->tag) {
    case ast_f64: snprintf(buf, sizeof buf, "%f", node->f64.val); return str_init(alloc, buf);
    case ast_i64:
        snprintf(buf, sizeof buf, "(%" PRIi64 " : %.*s)", node->i64.val, str_ilen(ty_str),
                 str_buf(&ty_str));
        return str_init(alloc, buf);

    case ast_u64:    snprintf(buf, sizeof buf, "%" PRIu64, node->u64.val); return str_init(alloc, buf);
    case ast_string: return str_cat_3(alloc, S("\""), node->symbol.name, S("\""));
    case ast_bool:   return node->bool_.val ? str_copy(alloc, S("true")) : str_copy(alloc, S("false"));
    case ast_nil:    return S("()");
    case ast_any:    return S("any");

    case ast_symbol: {
        str out = node->symbol.name;
        if (node->symbol.annotation_type_v2) {
            out = str_cat_4(alloc, out, S(" (v2: "),
                            tl_polytype_to_string(alloc, node->symbol.annotation_type_v2), S(")"));
        } else if (node->symbol.annotation) {
            out =
              str_cat_4(alloc, out, S(" ("), v2_ast_node_to_string(alloc, node->symbol.annotation), S(")"));
        }
        if (node->type_v2)
            out = str_cat_3(alloc, out, S(" : "), tl_polytype_to_string(alloc, node->type_v2));
        return out;
    }

    case ast_let: {
        str out               = str_copy(alloc, S("let "));
        out                   = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let.name));

        ast_node_sized params = ast_node_sized_from_ast_array((ast_node *)node);
        forall(i, params) {
            if (ast_nil == params.v[i]->tag) {
                out = str_cat(alloc, out, S(" ()"));
                break;
            } else {
                out = str_cat_3(alloc, out, S(" "), params.v[i]->symbol.name);
            }
        }

        if (node->type_v2)
            out = str_cat_3(alloc, out, S(" : "), tl_polytype_to_string(alloc, node->type_v2));
        out = str_cat_3(alloc, out, S(" = "), v2_ast_node_to_string(alloc, node->let.body));
        return out;
    }

    case ast_let_in: {
        str out = str_copy(alloc, S("let "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.name));
        out     = str_cat(alloc, out, S(" = "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.value));
        out     = str_cat(alloc, out, S(" in "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.body));
        return out;

    } break;

    case ast_named_function_application: {
        str out = str_copy(alloc, node->named_application.name->symbol.name);
        for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
            out = str_cat_3(alloc, out, S(" "),
                            v2_ast_node_to_string(alloc, node->named_application.arguments[i]));
        }

        return out;
    } break;

    case ast_arrow: {
        str out = str_cat_3(alloc, v2_ast_node_to_string(alloc, node->arrow.left), S(" -> "),
                            v2_ast_node_to_string(alloc, node->arrow.right));
        return out;
    } break;

    case ast_lambda_function: {
        str out = str_copy(alloc, S("fun"));

        // here we want to include the nil value
        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            out = str_cat_3(alloc, out, S(" "),
                            v2_ast_node_to_string(alloc, node->lambda_function.parameters[i]));
        }
        out = str_cat(alloc, out, S(" -> "));
        out = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->lambda_function.body));
        return out;

    } break;

    case ast_ellipsis:                    return str_copy(alloc, S("..."));

    case ast_address_of:
    case ast_assignment:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_eof:

    case ast_if_then_else:
    case ast_let_match_in:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_tuple:
    case ast_user_type:                   return str_init(alloc, ast_node_to_string(alloc, node));
    }
}

//

static void do_apply_subs(void *ctx, ast_node *node) {
    tl_infer *self = ctx;
    if (node->type_v2) {
        tl_polytype_substitute(self->arena, (tl_type_v2 *)node->type_v2, self->subs); // const_cast
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
    else if (ast_node_is_let_in_lambda(node)) return node->let_in.name;
    else if (ast_node_is_symbol(node)) return node;
    else if (ast_node_is_utd(node)) return node->user_type_def.name;
    else if (ast_node_is_nfa(node)) return node->named_application.name;
    else fatal("logic error");
}

str toplevel_name(ast_node const *node) {
    return toplevel_name_node((ast_node *)node)->symbol.name;
}

//

static void log_constraint(tl_infer *self, tl_type_v2 const *left, tl_type_v2 const *right,
                           ast_node const *node) {
    if (!self->verbose) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    log(self, "constrain: %.*s : %.*s from %.*s", str_ilen(left_str), str_buf(&left_str),
        str_ilen(right_str), str_buf(&right_str), str_ilen(node_str), str_buf(&node_str));
}

static void log_type_error(tl_infer *self, tl_type_v2 const *left, tl_type_v2 const *right) {
    if (!self->verbose) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    log(self, "error: constraints are not compatible:  %.*s versus %.*s", str_ilen(left_str),
        str_buf(&left_str), str_ilen(right_str), str_buf(&right_str));
}
static void log_type_error_mm(tl_infer *self, tl_monotype const *left, tl_monotype const *right) {
    tl_type_v2 l = tl_polytype_wrap((tl_monotype *)left), r = tl_polytype_wrap((tl_monotype *)right);
    return log_type_error(self, &l, &r);
}

static void log_subs(tl_infer *self) {
    if (self->verbose) tl_type_subs_log(self->transient, self->subs);
}
