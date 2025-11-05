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

    u32                  next_var_name;
    u32                  next_instantiation;

    int                  verbose;
    int                  indent_level;
};

typedef struct {
    u64 name_hash;
    u64 type_hash;
} name_and_type;

typedef struct {
    hashmap *call_chain;     // hset str
    hashmap *lex;            // hset str (names in local lexical scope)
    hashmap *type_arguments; // map str -> tl_monotype*
    hashmap *seen_node;      // hset ast_node* FIXME: needed?
    void    *user;
    int      is_intrinsic_argument;
    int      is_field_name;
} traverse_ctx;

typedef int (*traverse_cb)(tl_infer *, traverse_ctx *, ast_node *);

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

tl_infer *tl_infer_create(allocator *alloc, tl_infer_opts const *opts) {
    tl_infer *self           = new (alloc, tl_infer);

    self->opts               = *opts;

    self->transient          = arena_create(alloc, 4096);
    self->arena              = arena_create(alloc, 16 * 1024);
    self->env                = tl_type_env_create(self->arena, self->transient);
    self->subs               = tl_type_subs_create(self->arena);
    self->registry           = tl_type_registry_create(self->arena, self->subs);

    self->synthesized_nodes  = (ast_node_array){.alloc = self->arena};

    self->toplevels          = null;
    self->instances          = map_new(self->arena, name_and_type, str, 512);
    self->instance_names     = hset_create(self->arena, 512);
    self->hash_includes      = (str_array){.alloc = self->arena};
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
    if ((*p)->instance_names) hset_destroy(&(*p)->instance_names);

    arena_destroy(alloc, &(*p)->transient);
    arena_destroy(alloc, &(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose      = verbose;
    self->env->verbose = verbose;
}

static tl_monotype *get_tv_or_fresh(tl_type_registry *self, str name, hashmap **map, tl_type_subs *subs) {
    // map may be null, in which case type arguments are not supported
    tl_monotype *found = null;
    if (map) found = str_map_get_ptr(*map, name);

    if (found) {
        return found;
    }

    tl_type_variable tv = tl_type_subs_fresh(subs);
    found               = tl_monotype_create_tv(self->alloc, tv);
    if (map) str_map_set_ptr(map, name, found);
    return found;
}

tl_monotype *tl_type_registry_parse(tl_type_registry *self, ast_node const *node, tl_type_subs *subs,
                                    hashmap **map) {
    // map : map_new(self->transient, str, tl_monotype*, 8);
    // used to ensure same symbol gets same type variable.
    // map may be null in which case type arguments are not supported, as in the case of simple type
    // aliases, which alias an identifier to a type

    // TODO: it's weird that this is here, but it depends on type_registry, which does not exist until the
    // inference stage.

    if (ast_node_is_nil(node)) {
        return tl_type_registry_nil(self);
    }

    if (ast_node_is_symbol(node)) {
        // Note: special case the symbol 'any' to return an any type
        str name = ast_node_str(node);

        if (str_eq(name, S("any"))) {
            return tl_monotype_create_any(self->alloc);
        } else {
            // or else check if it's a known type
            tl_monotype *out = tl_type_registry_instantiate(self, name);
            if (out) return out;
        }

        tl_monotype *tv = get_tv_or_fresh(self, name, map, subs);
        return tv;
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
                mono = get_tv_or_fresh(self, ast_node_str(args.v[i]), map, subs);
            }
            array_push(args_mono, mono);
        }

        tl_monotype_sized args_mono_ = array_sized(args_mono);
        if (str_eq(ast_node_str(node->named_application.name), S("Union"))) {
            return tl_type_registry_instantiate_union(self, args_mono_);
        } else {
            return tl_type_registry_instantiate_with(self, ast_node_str(node->named_application.name),
                                                     args_mono_);
        }
    }

    if (ast_node_is_arrow(node)) {
        // arrow types are always ([{ a }]) -> b
        ast_node const *tup = node->arrow.left;
        assert(ast_node_is_tuple(tup));
        ast_node_sized    tuple = {.size = tup->tuple.n_elements, .v = tup->tuple.elements};
        tl_monotype_array arr   = {.alloc = self->alloc};
        forall(i, tuple) {
            tl_monotype const *t = tl_type_registry_parse(self, tuple.v[i], subs, map);
            array_push(arr, t);
        }
        tl_monotype_sized arr_sized = array_sized(arr);
        tl_monotype      *left      = tl_monotype_create_tuple(self->alloc, arr_sized);
        tl_monotype      *right     = tl_type_registry_parse(self, node->arrow.right, subs, map);
        return tl_monotype_create_arrow(self->alloc, left, right);
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

    hashmap               *type_argument_map = map_new(self->transient, str, tl_monotype *, 8);
    tl_type_variable_array type_argument_tvs = {.alloc = self->arena};
    for (u32 i = 0; i < n_type_arguments; ++i) {
        ast_node const *ta = type_arguments[i];
        assert(ast_node_is_symbol(ta));
        str                ta_name = ast_node_str(ta);
        tl_type_variable   fresh   = tl_type_subs_fresh(self->subs);
        tl_monotype const *mono    = tl_monotype_create_tv(self->arena, fresh);
        str_map_set_ptr(&type_argument_map, ta_name, mono);

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

        // enum types have fields with no type information

        // field type, could be type argument, or type constructor, or null
        if (annotations) {
            tl_monotype const *field           = null;
            ast_node const    *field_type_node = annotations[i];
            if (ast_node_is_symbol(field_type_node)) {
                tl_monotype const *found =
                  str_map_get_ptr(type_argument_map, ast_node_str(field_type_node));
                if (found) field = found;
            }

            if (!field)
                field =
                  tl_type_registry_parse(self->registry, field_type_node, self->subs, &type_argument_map);

            if (!field) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
                return;
            }

            array_push(field_types, field);
        }
    }

    str_sized              field_names_       = array_sized(field_names);
    tl_monotype_sized      field_types_       = array_sized(field_types);
    tl_type_variable_sized type_argument_tvs_ = array_sized(type_argument_tvs);

    tl_polytype           *poly =
      tl_type_constructor_def_create(self->registry, name, type_argument_tvs_, field_names_, field_types_);

    tl_type_env_insert(self->env, name, poly);
    ast_node_type_set(node, poly);
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

static void process_annotation(tl_infer *self, ast_node *node, traverse_ctx *ctx) {
    ast_node const *name = toplevel_name_node(node);

    if (!name->symbol.annotation) return;
    {
        str ann_str = v2_ast_node_to_string(self->transient, name->symbol.annotation);
        log(self, "process_annotation: %.*s : ast:%s", str_ilen(name->symbol.name),
            str_buf(&name->symbol.name), str_cstr(&ann_str));
    }
    if (name->symbol.annotation_type) {
        str poly_str = tl_polytype_to_string(self->transient, name->symbol.annotation_type);
        log(self, "process_annotation exists: %.*s : %s", str_ilen(name->symbol.name),
            str_buf(&name->symbol.name), str_cstr(&poly_str));

        // return;
    }

    hashmap *map;
    if (ctx) map = ctx->type_arguments;
    else map = map_new(self->transient, str, tl_monotype *, 8);

    tl_monotype *ann  = tl_type_registry_parse(self->registry, name->symbol.annotation, self->subs, &map);

    tl_polytype *poly = tl_polytype_absorb_mono(self->arena, ann);
    // tl_polytype_generalize(poly, self->env, self->subs);
    node->symbol.annotation_type = poly;

    str poly_str                 = tl_polytype_to_string(self->transient, poly);
    log(self, "process_annotation: %.*s : %s", str_ilen(name->symbol.name), str_buf(&name->symbol.name),
        str_cstr(&poly_str));

    if (!ctx) map_destroy(&map);
}

static void collect_type_arguments(tl_infer *self, ast_node *node, hashmap **map) {
    // populate type variable map used by tl_type_registry_parse with type arguments, if any
    (void)self;
    // map : map_new(self->transient, str, tl_monotype*, 8);

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    while ((arg = ast_arguments_next(&iter))) {
        if (!arg->type) continue;
        assert(arg->type);

        tl_monotype *mono = arg->type->type;
        if (tl_monotype_is_type_literal(mono)) {
            str          arg_name = ast_node_str(arg);
            tl_monotype *target   = tl_monotype_type_literal_target(mono);
            str_map_set_ptr(map, arg_name, target);
            str target_str = tl_monotype_to_string(self->transient, target);
            log(self, "collect_type_argument: %s : %s", str_cstr(&arg_name), str_cstr(&target_str));
        }
    }
}

static int toplevel_hash_command(tl_infer *self, ast_node *node) {
    assert(ast_node_is_hash_command(node));

    // skip #ifc .. #endc blocks
    if (ast_node_is_ifc_block(node)) return 0;

    str_sized words = node->hash_command.words;

    if (words.size < 2) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_arity, .node = node}));
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

static void load_toplevel(tl_infer *self, ast_node_sized nodes) {

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
                    process_annotation(self, (*p)->let.name, null);
                }
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&self->toplevels, name_str, &node);
                    process_annotation(self, node, null);
                }
            }
        }

        else if (ast_node_is_type_alias(node)) {
            str          name = toplevel_name(node);
            tl_monotype *ann =
              tl_type_registry_parse(self->registry, node->type_alias.target, self->subs, null);
            assert(ann);
            tl_polytype *poly = tl_polytype_absorb_mono(self->arena, ann);
            tl_polytype_generalize(poly, self->env, self->subs);
            {
                str poly_str = tl_polytype_to_string(self->transient, poly);
                log(self, "type_alias: %s = %s", str_cstr(&name), str_cstr(&poly_str));
            }
            tl_type_registry_type_alias_insert(self->registry, name, poly);
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
                if (!node->let.name->symbol.annotation) {
                    // apply annotation
                    node->let.name->symbol.annotation = (*p)->symbol.annotation;
                    process_annotation(self, node->let.name, null);
                }

                // replace prior symbol entry with let node
                *p = node;
            } else {
                str_map_set(&self->toplevels, name_str, &node);
                process_annotation(self, node->let.name, null);
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
            process_annotation(self, node->let_in.name, null);
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
            if (ast_node_is_assignment(arg)) arg = arg->assignment.value;
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
    } else if (ast_node_is_let_in(node)) {
        ast_node *value = node->let_in.value;
        if (ast_node_is_symbol(value)) {
            str name = ast_node_str(value); // caution: the value name, not the let's name

            // if it is a toplevel, recurse through it
            ast_node *next = toplevel_get(self, name);
            if (next) {
                ast_node_dfs(ctx, next, do_tree_shake);
            }
            str_hset_insert(&ctx->names, name);
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

static traverse_ctx *traverse_ctx_create(allocator *alloc) {
    traverse_ctx *out          = new (alloc, traverse_ctx);
    out->seen_node             = hset_create(alloc, 1024);
    out->call_chain            = hset_create(alloc, 16);
    out->lex                   = hset_create(alloc, 16);
    out->type_arguments        = map_create_ptr(alloc, 16);
    out->user                  = null;
    out->is_intrinsic_argument = 0;
    out->is_field_name         = 0;

    return out;
}

static void traverse_ctx_destroy(allocator *alloc, traverse_ctx **p) {
    if ((*p)->seen_node) map_destroy(&(*p)->seen_node);
    if ((*p)->type_arguments) map_destroy(&(*p)->type_arguments);
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

static void log_constraint(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
static void log_type_error(tl_infer *, tl_polytype *, tl_polytype *);
static void log_type_error_mm(tl_infer *, tl_monotype *, tl_monotype *);

typedef struct {
    tl_infer       *self;
    ast_node const *node;
} type_error_cb_ctx;

static void type_error_cb(void *ctx_, tl_monotype *left, tl_monotype *right) {
    type_error_cb_ctx *ctx = ctx_;
    log_type_error_mm(ctx->self, left, right);
    type_error(ctx->self, ctx->node);
}

static int constrain_mono(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    type_error_cb_ctx error_ctx = {.self = self, .node = node};
    return tl_type_subs_unify_mono(self->subs, left, right, type_error_cb, &error_ctx);
}

static int constrain(tl_infer *self, infer_ctx *ctx, tl_polytype *left, tl_polytype *right,
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

static int constrain_pm(tl_infer *self, infer_ctx *ctx, tl_polytype *left, tl_monotype *right,
                        ast_node const *node) {

    tl_polytype wrap = tl_polytype_wrap(right);
    return constrain(self, ctx, left, &wrap, node);
}

static void ensure_tv(tl_infer *self, str const *name, tl_polytype **type) {
    if (!type) return;
    if (*type) return;

    if (name) *type = tl_polytype_clone(self->arena, (tl_type_env_lookup(self->env, *name)));
    if (*type) return;

    *type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

static void         rename_variables(tl_infer *, ast_node *, hashmap **, int);
static str          specialize_fun(tl_infer *, ast_node *, tl_monotype *);
static tl_polytype *make_arrow(tl_infer *, ast_node_sized, ast_node *);
static tl_polytype *make_arrow_with(tl_infer *, ast_node_sized, ast_node *, tl_polytype *);
static int          traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb);

//

static ast_node *clone_generic(tl_infer *self, ast_node const *node) {
    ast_node *clone = ast_node_clone(self->arena, node);
    ast_node *name  = toplevel_name_node(clone);
    assert(ast_node_is_symbol(name));
    name->symbol.annotation_type = null;
    name->symbol.annotation      = null;

    // rename variables: also erases type information
    hashmap *rename_lex = map_new(self->transient, str, str, 16);
    rename_variables(self, clone, &rename_lex, 0);
    map_destroy(&rename_lex);

    return clone;
}

static int traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    if (null == node) return 0;

    hset_insert(&ctx->seen_node, &node, sizeof(ast_node *));

    switch (node->tag) {
    case ast_let: {

        map_reset(ctx->type_arguments);

        hashmap           *save = map_copy(ctx->lex);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            assert(ast_node_is_symbol(param));
            str_hset_insert(&ctx->lex, param->symbol.name);
            ensure_tv(self, null, &param->type);
            if (cb(self, ctx, param)) return 1;
        }

        // collect type arguments after traversing/inferring arguments
        collect_type_arguments(self, node, &ctx->type_arguments);

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

        // traverse value first, then traverse name and body
        if (traverse_ast(self, ctx, node->let_in.value, cb)) return 1;

        // collect type arguments after traversing/inferring arguments
        if (ast_node_is_let_in_lambda(node)) collect_type_arguments(self, node, &ctx->type_arguments);

        if (traverse_ast(self, ctx, node->let_in.name, cb)) return 1;
        if (traverse_ast(self, ctx, node->let_in.body, cb)) return 1;

        // process node again: for specialised types, typing the name depends on typing the value.
        if (cb(self, ctx, node)) return 1;

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

        // traverse arguments, but do not process arguments of intrinsic calls
        int save = ctx->is_intrinsic_argument;
        if (is_intrinsic(name)) ctx->is_intrinsic_argument = 1;

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter)))
            if (traverse_ast(self, ctx, arg, cb)) return 1;

        if (cb(self, ctx, node)) return 1;

        ctx->is_intrinsic_argument = save;

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

    case ast_body:
        forall(i, node->body.expressions) {
            if (traverse_ast(self, ctx, node->body.expressions.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_binary_op:
        // don't traverse op, it's just an operator
        if (traverse_ast(self, ctx, node->binary_op.left, cb)) return 1;

        // when traversing to the right of . and ->, we could encounter field names that should not be
        // considered free variables, so signal that in the traverse_ctx
        {
            int is_symbol = ast_node_is_symbol(node->binary_op.right);
            int save      = 0;
            if (is_symbol) {
                save               = ctx->is_field_name;
                ctx->is_field_name = 1;
            }
            if (traverse_ast(self, ctx, node->binary_op.right, cb)) return 1;
            if (is_symbol) ctx->is_field_name = save;
        }

        if (cb(self, ctx, node)) return 1;
        break;

    case ast_unary_op:
        // don't traverse op, it's just an operator
        if (traverse_ast(self, ctx, node->unary_op.operand, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_assignment:
        ctx->is_field_name = 1;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_return:
        //
        if (traverse_ast(self, ctx, node->return_.value, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_while:
        if (traverse_ast(self, ctx, node->while_.condition, cb)) return 1;
        if (traverse_ast(self, ctx, node->while_.body, cb)) return 1;
        if (cb(self, ctx, node)) return 1;
        break;

        // FIXME: complete the misisng traversals for the various ast types below

    case ast_hash_command:
    case ast_nil:
    case ast_continue:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_string:
    case ast_symbol:
    case ast_u64:
    case ast_type_alias:
    case ast_user_type_definition:

        // operate on the leaf node
        if (cb(self, ctx, node)) return 1;

        break;
    }

    return 0;
}

static tl_monotype *instantiate_type_literal(tl_infer *self, ast_node *node) {
    if (ast_node_is_symbol(node)) {

        str          name = ast_node_str(node);
        tl_monotype *inst =
          tl_type_registry_get_cached_instance(self->registry, name, (tl_monotype_sized){0});
        if (!inst) return null;

        // set node type to type literal
        tl_monotype *ty =
          tl_type_registry_type_literal(self->registry, tl_monotype_clone(self->arena, inst));
        ast_node_type_set(node, tl_polytype_absorb_mono(self->arena, ty));

        return inst;
    }
    if (!ast_node_is_nfa(node)) return null;

    // FIXME: portion duplicated with specialize_type_identifier()

    str            name      = ast_node_str(node->named_application.name);
    ast_node_sized node_args = ast_node_sized_from_ast_array(node);
    // zero argument nfa cannot be a type identifier
    if (!node_args.size) return null;
    tl_polytype *poly = tl_type_registry_get(self->registry, name);
    if (!poly) return null;
    assert(tl_monotype_is_inst(poly->type));
    if (node_args.size != poly->quantifiers.size) return null;

    tl_monotype_array arg_types = {.alloc = self->transient};
    forall(i, node_args) {
        tl_monotype *arg_ty = instantiate_type_literal(self, node_args.v[i]);
        if (!arg_ty) return null;
        array_push(arg_types, arg_ty);
    }

    tl_monotype_sized arg_types_ = array_sized(arg_types);
    tl_monotype      *inst       = tl_type_registry_instantiate_with(self->registry, name, arg_types_);
    if (!inst) fatal("runtime error");

    return tl_type_registry_type_literal(self->registry, tl_monotype_clone(self->arena, inst));
}

static int add_generic(tl_infer *, ast_node *);

static int infer_traverse_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    infer_ctx *ctx = traverse_ctx->user;

    if (null == node) return 0;

    switch (node->tag) {
    case ast_nil: {
        ensure_tv(self, null, &node->type);
        tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
        // tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
        if (constrain_pm(self, ctx, node->type, weak, node)) return 1;
    } break;

    case ast_string: {
        tl_monotype *ty = tl_type_registry_string(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_f64: {
        tl_monotype *ty = tl_type_registry_float(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_i64: {
        tl_monotype *ty = tl_type_registry_int(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_u64: {
        tl_monotype *ty = tl_type_registry_int(self->registry); // FIXME unsigned
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_bool: {
        tl_monotype *ty = tl_type_registry_bool(self->registry);
        ensure_tv(self, null, &node->type);
        if (constrain_pm(self, ctx, node->type, ty, node)) return 1;
    } break;

    case ast_body: {
        ensure_tv(self, null, &node->type);
        if (node->body.expressions.size) {
            u32       sz   = node->body.expressions.size;
            ast_node *last = node->body.expressions.v[sz - 1];
            ensure_tv(self, null, &last->type);
            if (constrain(self, ctx, node->type, last->type, node)) return 1;
        }
    } break;

    case ast_return: {
        ensure_tv(self, null, &node->type);
        if (constrain(self, ctx, node->type, node->return_.value->type, node)) return 1;
    } break;

    case ast_binary_op: {
        ast_node *left = node->binary_op.left, *right = node->binary_op.right;
        ensure_tv(self, null, &node->type);
        ensure_tv(self, null, &left->type);
        ensure_tv(self, null, &right->type);

        char const *op = str_cstr(&node->binary_op.op->symbol.name);
        if (is_arithmetic_operator(op)) {
            // operands and result must all be same type
            if (constrain(self, ctx, node->type, left->type, node)) return 1;
            if (constrain(self, ctx, left->type, right->type, node)) return 1;
        } else if (is_logical_operator(op) || is_relational_operator(op)) {
            // operands must be same type, and result must be boolean
            tl_monotype *bool_type = tl_type_registry_bool(self->registry);
            if (constrain_pm(self, ctx, node->type, bool_type, node)) return 1;
            if (constrain(self, ctx, left->type, right->type, node)) return 1;
        } else if (is_index_operator(op)) {
            // index must be integral and result must be Ptr's type argument
            tl_monotype *int_type = tl_type_registry_int(self->registry);
            if (constrain_pm(self, ctx, right->type, int_type, node)) return 1;

            if (left->type) {
                tl_monotype_substitute(self->arena, left->type->type, self->subs, null);
                if (tl_monotype_has_ptr(left->type->type)) {
                    tl_monotype *target = tl_monotype_ptr_target(left->type->type);
                    if (constrain_pm(self, ctx, node->type, target, node)) return 1;
                }
            }
        } else if (is_struct_access_operator(op)) {
            // TODO: move this to utility function
            tl_monotype *struct_type = null;

            // handle -> vs . access
            if (0 == strcmp("->", op)) {
                if (!tl_monotype_has_ptr(left->type->type)) {
                    array_push(self->errors, (tl_infer_error){.tag = tl_err_expected_pointer});
                    return 1;
                }
                struct_type = (tl_monotype *)tl_monotype_ptr_target(left->type->type);
            } else {
                struct_type = (tl_monotype *)left->type->type;
            }
            // Note: must substitute to resolve type of chained field access, eg: foo.bar.baz
            tl_monotype_substitute(self->arena, struct_type, self->subs, null);
            if (tl_monotype_is_inst(struct_type)) {
                // Note: this handling of nfas supports terms like: `obj.fun_ptr()` where a field called
                // fun_ptr is a function pointer.
                ast_node *nfa = null;
                if (ast_node_is_nfa(right)) {
                    nfa   = right;
                    right = right->named_application.name;
                }
                ensure_tv(self, null, &right->type);
                if (ast_node_is_symbol(right)) {
                    str                       field_name = right->symbol.name;
                    tl_type_constructor_inst *inst       = struct_type->cons_inst;
                    tl_type_constructor_def  *def        = inst->def;

                    i32                       found      = -1;
                    forall(i, def->field_names) {
                        if (str_eq(field_name, def->field_names.v[i])) {
                            if (i > INT32_MAX) fatal("overflow");
                            found = (i32)i;
                            break;
                        }
                    }
                    if (found != -1) {
                        // Enums: they have no types and no instance arguments. Detect those and give them
                        // an Int type.
                        if (!inst->args.size) {
                            tl_monotype *int_ty = tl_type_registry_int(self->registry);
                            if (constrain_pm(self, ctx, right->type, int_ty, node)) return 1;
                            if (constrain_pm(self, ctx, node->type, int_ty, node)) return 1;
                            if (constrain(self, ctx, node->type, right->type, node)) return 1;
                            break;
                        }

                        if ((u32)found >= inst->args.size) fatal("out of range");
                        tl_monotype *field_type = inst->args.v[found];
                        if (nfa) {
                            tl_monotype *result_type = tl_monotype_arrow_result(field_type);
                            // right = nfa's name
                            if (constrain_pm(self, ctx, right->type, field_type, node)) return 1;
                            if (constrain_pm(self, ctx, nfa->type, result_type, node)) return 1;
                            if (constrain_pm(self, ctx, node->type, result_type, node)) return 1;
                        } else {
                            if (constrain_pm(self, ctx, right->type, field_type, node)) return 1;
                            if (constrain_pm(self, ctx, node->type, field_type, node)) return 1;
                            if (constrain(self, ctx, node->type, right->type, node)) return 1;
                        }
                    }

                } else {
                    fatal("unreachable");
                }
            } else {
                if (constrain(self, ctx, node->type, right->type, node)) return 1;
            }
        } else fatal("unknown operator type");

    } break;

    case ast_unary_op: {
        ast_node *operand = node->unary_op.operand;
        ensure_tv(self, null, &operand->type);
        ensure_tv(self, null, &node->type);

        str op = ast_node_str(node->unary_op.op);
        if (str_eq(op, S("*"))) {
            if (tl_monotype_has_ptr(operand->type->type)) {
                assert(!tl_polytype_is_scheme(operand->type));
                tl_monotype *target = tl_monotype_ptr_target(operand->type->type);
                if (constrain_pm(self, ctx, node->type, target, node)) return 1;
            } else {
                // pointer required
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
                return 1;
            }
        } else if (str_eq(op, S("&"))) {
            // TODO: do we need a weak type variable here?
            if (!tl_polytype_is_scheme(operand->type)) {
                tl_monotype *ptr = tl_type_registry_ptr(self->registry, operand->type->type);
                if (constrain_pm(self, ctx, node->type, ptr, node)) return 1;
            } else {
                tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
                tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
                if (constrain_pm(self, ctx, node->type, ptr, node)) return 1;
            }
        } else if (str_eq(op, S("!"))) {
            tl_monotype *bool_type = tl_type_registry_bool(self->registry);
            if (constrain_pm(self, ctx, node->type, bool_type, node)) return 1;
        } else if (str_eq(op, S("~"))) {
            if (constrain(self, ctx, node->type, operand->type, node)) return 1;
        } else {
            fatal("unknown unary operator");
        }

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
                node->let_in.value->type = tl_polytype_nil(self->arena, self->registry);
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
        tl_polytype       *arrow = make_arrow(self, iter.nodes, node->let.body);
        if (!arrow) return 1;
        tl_polytype_substitute(self->arena, (tl_polytype *)arrow, self->subs); // const cast
        tl_type_env_insert(self->env, node->let.name->symbol.name, arrow);

    } break;

    case ast_symbol: {

        tl_polytype *global = tl_type_env_lookup(self->env, node->symbol.name);

        if (global) {
            tl_polytype *global_copy = tl_polytype_clone(self->arena, global);
            if (node->type) {
                if (constrain(self, ctx, node->type, global_copy, node)) return 1;
            } else {
                ast_node_type_set(node, global_copy);
            }
        }

        else {
            ensure_tv(self, null, &node->type);
        }

        // if symbol has a type annotation, constrain it
        if (node->symbol.annotation) {
            process_annotation(self, node, traverse_ctx);
            if (constrain(self, ctx, node->symbol.annotation_type, node->type, node)) return 1;
        }

        // add to environment

        // Important: this is necessary in
        // particular for formal parameters which are type literals,
        // because the type is propagated via the environment.
        if (!traverse_ctx->is_field_name)
            if (!global) tl_type_env_insert(self->env, node->symbol.name, node->type);

    } break;

    case ast_named_function_application: {

        ensure_tv(self, null, &node->type);

        str          name     = ast_node_str(node->named_application.name);
        str          original = ast_node_name_original(node->named_application.name);
        tl_polytype *type     = tl_type_env_lookup(self->env, name);
        if (!type) {
            return 0; // mututal recursion, undeclared std_* functions, etc
        }

        if (tl_polytype_is_type_constructor(type)) {
            // a type constructor or type literal

            tl_monotype *literal = instantiate_type_literal(self, node);
            if (literal) {
                if (constrain_pm(self, ctx, node->type, literal, node)) return 1;
                // set node type directly after constraint
                ast_node_type_set(node, tl_polytype_absorb_mono(self->arena, literal));
                break;
            }

            tl_monotype_array  args = {.alloc = self->arena};
            ast_arguments_iter iter = ast_node_arguments_iter(node);
            ast_node          *arg;
            while ((arg = ast_arguments_next(&iter))) {
                ensure_tv(self, null, &arg->type);
                assert(!tl_polytype_is_scheme(arg->type));
                array_push(args, arg->type->type);
            }
            array_shrink(args);

            tl_monotype *inst = tl_type_registry_instantiate(self->registry, name);
            if (!inst) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_arity, .node = node}));
                return 1;
            }

            {
                str          inst_str = tl_monotype_to_string(self->transient, inst);
                tl_polytype *app      = make_arrow(self, iter.nodes, null);
                if (!app) return 1;
                str app_str = tl_polytype_to_string(self->transient, app);
                log(self, "type constructor: callsite '%s' (%s) arrow: %s", str_cstr(&name),
                    str_cstr(&inst_str), str_cstr(&app_str));
            }

            iter  = ast_node_arguments_iter(node);
            u32 i = 0;
            while ((arg = ast_arguments_next(&iter))) {
                if (i >= inst->cons_inst->args.size) fatal("runtime error");

                if (ast_node_is_assignment(arg)) {
                    // check if name exists in type def
                    if (!tl_polytype_type_constructor_has_field(type, ast_node_str(arg->assignment.name))) {
                        array_push(self->errors,
                                   ((tl_infer_error){.tag = tl_err_field_not_found, .node = arg}));
                        return 1;
                    }
                }

                if (constrain_pm(self, ctx, arg->type, inst->cons_inst->args.v[i], node)) return 1;
                ++i;
            }

            if (constrain_pm(self, ctx, node->type, inst, node)) return 1;

        } else {
            // a function type

            // if the type is not generic, try to recover the generic one
            if (tl_polytype_is_concrete(type)) {
                if (!str_is_empty(original)) {
                    tl_polytype *found = tl_type_env_lookup(self->env, original);
                    if (found) type = found;
                }
            }

            // instantiate generic function type being applied
            ast_arguments_iter iter     = ast_node_arguments_iter(node);
            tl_monotype       *inst     = tl_polytype_instantiate(self->arena, type, self->subs);
            str                inst_str = tl_monotype_to_string(self->transient, inst);
            tl_polytype       *app      = make_arrow(self, iter.nodes, node);
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
        tl_monotype *inst =
          tl_polytype_instantiate(self->arena, node->lambda_application.lambda->type, self->subs);
        node->lambda_application.lambda->type = tl_polytype_absorb_mono(self->arena, inst);

        // constrain arrow types
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        tl_polytype       *app  = make_arrow(self, iter.nodes, node);
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
            tl_polytype       *arrow = make_arrow(self, iter.nodes, node->lambda_function.body);
            if (!arrow) return 1;
            tl_polytype_generalize((tl_polytype *)arrow, self->env, self->subs); // const cast
            ast_node_type_set(node, arrow);
        }

    } break;

    case ast_if_then_else: {

        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, ctx, node->if_then_else.condition->type, bool_type, node)) return 1;

        // a nil type in else arm indicates the arm should not be generated, so don't type check it
        if (node->if_then_else.no)
            if (constrain(self, ctx, node->if_then_else.yes->type, node->if_then_else.no->type, node))
                return 1;

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

    case ast_user_type_definition: {
    } break;

    case ast_assignment:
        ensure_tv(self, null, &node->type);
        ensure_tv(self, null, &node->assignment.name->type);
        ensure_tv(self, null, &node->assignment.value->type);
        if (constrain(self, ctx, node->type, node->assignment.value->type, node)) return 1;

        // FIXME: this shouldn't be needed but I've observed a stale type in the assignment.name during
        // specialising, and so this is necessary when dealing with generics.
        if (tl_polytype_is_concrete(node->assignment.value->type)) {
            node->assignment.name->type = node->assignment.value->type;
        } else {
            if (constrain(self, ctx, node->type, node->assignment.name->type, node)) return 1;
        }

        break;

    case ast_while:
    case ast_continue: {
        ensure_tv(self, null, &node->type);
        tl_monotype *nil = tl_type_registry_nil(self->registry);
        if (constrain_pm(self, ctx, node->type, nil, node)) return 1;
    } break;

    case ast_hash_command:
    case ast_arrow:
    case ast_ellipsis:
    case ast_eof:
    case ast_type_alias:   break;
    }

    // apply newly created constraint substitutions
    tl_type_subs_apply(self->subs, self->env);
    return 0;
}

static str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args,
                                       tl_polytype **out_type) {

    if (out_type) *out_type = null;

    // specialize args first
    forall(i, args) {
        if (tl_monotype_is_inst(args.v[i]) && str_is_empty(args.v[i]->cons_inst->special_name)) {
            tl_polytype *poly = null;

            (void)specialize_type_constructor(self, args.v[i]->cons_inst->def->generic_name,
                                              args.v[i]->cons_inst->args, &poly);
            if (poly) args.v[i] = tl_polytype_concrete(poly);
        }
    }

    // do not specialize if args are not concrete
    if (!tl_monotype_sized_is_concrete(args)) {
        if (out_type) *out_type = null;
        return str_empty();
    }

    str          out_str   = str_empty();
    str          name_inst = next_instantiation(self, name); // may be cancelled later
    tl_monotype *inst      = tl_type_registry_specialize(self->registry, name, name_inst, args);
    if (!inst) goto cancel;

    name_and_type key      = {.name_hash = str_hash64(name), .type_hash = tl_monotype_hash64(inst)};
    str          *existing = map_get(self->instances, &key, sizeof key);
    if (existing) {
        if (out_type) *out_type = tl_type_env_lookup(self->env, *existing);
        out_str = *existing;
        goto cancel;
    }

    ast_node *utd = toplevel_get(self, name);
    if (!utd) goto cancel;

    map_set(&self->instances, &key, sizeof key, &name_inst);

    utd = ast_node_clone(self->arena, utd);
    ast_node_name_replace(utd->user_type_def.name, name_inst);
    utd->type = tl_polytype_absorb_mono(self->arena, inst);
    toplevel_add(self, name_inst, utd);
    tl_type_env_insert(self->env, name_inst, utd->type);
    array_push(self->synthesized_nodes, utd);

    if (out_type) *out_type = utd->type; // Note: this helps the transpiler
    return name_inst;

cancel:
    cancel_last_instantiation(self);
    return out_str;
}

static str specialize_type_identifier_na(tl_infer *self, str name, tl_monotype_sized args,
                                         tl_polytype **out_type) {
    str out = str_empty();
    if (out_type) *out_type = null;

    if (!args.size) {
        tl_monotype *inst =
          tl_type_registry_get_cached_instance(self->registry, name, (tl_monotype_sized){0});
        if (!inst) goto error;

        if (out_type) {
            tl_monotype *ty =
              tl_type_registry_type_literal(self->registry, tl_monotype_clone(self->arena, inst));
            *out_type = tl_polytype_absorb_mono(self->arena, ty);
        }

        return name;
    }

    // number of type arguments must match type definition
    tl_polytype *poly = tl_type_registry_get(self->registry, name);
    if (!poly) goto error;
    assert(tl_monotype_is_inst(poly->type));
    if (args.size != poly->quantifiers.size) goto error;

    tl_monotype *inst = tl_type_registry_instantiate_with(self->registry, name, args);
    if (!inst) fatal("runtime error");

    tl_polytype *special_type = null;
    str          name_inst = specialize_type_constructor(self, name, inst->cons_inst->args, &special_type);
    if (str_is_empty(name_inst)) fatal("runtime error");
    if (!special_type) fatal("runtime error");

    // set out type to type literal
    if (out_type) {
        tl_monotype *ty = tl_type_registry_type_literal(
          self->registry, tl_monotype_clone(self->arena, tl_polytype_concrete(special_type)));
        *out_type = tl_polytype_absorb_mono(self->arena, ty);
    }

    return name_inst;

error:

    return out;
}

static tl_monotype *specialize_type_identifer_unwrap(tl_infer *self, ast_node *node);

static tl_monotype *specialize_type_identifer(tl_infer *self, ast_node *node) {
    // specialize a type id, e.g. `Point(Int)`. Contrast to specialize_type_constructor, which specialises
    // based on a callsite like `Point(1, 2)`. Assuming Point(a) { x : a, y : a }.
    // return null if node is not a type identifier or other error occurs.

    tl_polytype *out_poly = null;

    if (ast_node_is_symbol(node)) {

        str name = ast_node_str(node);
        if (str_eq(name, S("Type"))) fatal("runtime error");
        (void)specialize_type_identifier_na(self, name, (tl_monotype_sized){0}, &out_poly);
        if (!out_poly) return null;
        ast_node_type_set(node, out_poly);
        return out_poly->type;
    }
    if (!ast_node_is_nfa(node)) return null;

    // FIXME: portion duplicated with instantiate_type_literal()

    str            name      = ast_node_name_original(node->named_application.name);
    ast_node_sized node_args = ast_node_sized_from_ast_array(node);
    // zero argument nfa cannot be a type identifier
    if (!node_args.size) return null;

    tl_polytype *poly = tl_type_registry_get(self->registry, name);
    if (!poly) return null;
    assert(tl_monotype_is_inst(poly->type));
    if (node_args.size != poly->quantifiers.size) return null;

    tl_monotype_array arg_types = {.alloc = self->transient};
    forall(i, node_args) {
        tl_monotype *arg_ty = specialize_type_identifer_unwrap(self, node_args.v[i]);
        if (!arg_ty) return null;
        array_push(arg_types, arg_ty);
    }

    tl_monotype_sized arg_types_ = array_sized(arg_types);
    str               name_inst  = specialize_type_identifier_na(self, name, arg_types_, &out_poly);

    if (!out_poly) fatal("runtime error");
    ast_node_type_set(node, out_poly);

    // update callsite
    ast_node_name_replace(node->named_application.name, name_inst);

    return out_poly->type;
}

static tl_monotype *specialize_type_identifer_unwrap(tl_infer *self, ast_node *node) {
    tl_monotype *type_id = null;
    if ((type_id = specialize_type_identifer(self, node))) {
        // unwrap Type literal
        tl_monotype *mono = type_id;
        if (tl_monotype_is_type_literal(mono)) {
            mono = tl_monotype_type_literal_target(mono);
        }
        return mono;
    }
    return null;
}

static int specialize_user_type(tl_infer *self, ast_node *node) {

    // divert if type constructor application is actually a type literal
    if (specialize_type_identifer(self, node)) return 0;

    if (!ast_node_is_nfa(node)) return 0;

    str                name = node->named_application.name->symbol.name;

    tl_monotype_array  arr  = {.alloc = self->transient};
    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    while ((arg = ast_arguments_next(&iter))) {

        tl_monotype *type_id = null;
        if ((type_id = specialize_type_identifer_unwrap(self, arg))) {
            array_push(arr, type_id);
            continue;
        }

        tl_polytype *poly = tl_polytype_clone(self->arena, arg->type);
        tl_polytype_substitute(self->arena, poly, self->subs);
        if (!tl_polytype_is_concrete(poly)) return 0; // FIXME

        tl_monotype *mono = poly->type;

        array_push(arr, mono);
    }

    tl_monotype_sized arr_sized    = array_sized(arr);
    tl_polytype      *special_type = null;
    str               name_inst    = specialize_type_constructor(self, name, arr_sized, &special_type);
    if (str_is_empty(name_inst)) fatal("runtime error");

    // update callsite
    ast_node_name_replace(node->named_application.name, name_inst);
    if (special_type) ast_node_type_set(node, special_type); // Note: this helps the transpiler

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

static str lookup_arrow(tl_infer *self, str name, tl_monotype *arrow);
static str specialize_arrow(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx, str name,
                            tl_monotype *arrow);
static int specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node);

static int specialize_one(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx, ast_node *arg,
                          tl_monotype *type) {
    str      *existing;
    str       arg_name = ast_node_str(arg);
    ast_node *top      = toplevel_get(self, arg_name);
    // Note: using the arrow type's name helps cases where a function pointer has been assigned to a
    // variable.

    if ((existing = str_map_get(ctx->specials, arg_name))) {
        str inst_name = *existing;
        ast_node_name_replace(arg, inst_name);
        return -1;
    } else {
        str inst_name = specialize_fun(self, top, type);
        str_map_set(&ctx->specials, arg_name, &inst_name);
        ast_node *special = toplevel_get(self, inst_name);
        ast_node_name_replace(arg, inst_name);
        ast_node *infer_target = get_infer_target(special);
        if (infer_target) {
            if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) return 1;
            if (traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb)) return 1;
        }
    }
    return 0;
}

static int specialize_let_in(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx, ast_node *node) {
    // Here we handle let fptr = id in ... function pointers. When this is called after the function being
    // pointed to has been specialised, the arrow types will be concrete. We use those types to look up
    // (using specialize_fun) the specialised version and replace the symbol name with the specialised name.
    // This ensures the transpiler refers to an existant concrete function rather than the generic template.

    assert(ast_node_is_let_in(node));
    tl_polytype *name_type  = node->let_in.name->type;
    tl_polytype *value_type = node->let_in.value->type;

    if (!name_type || !value_type || !tl_monotype_is_arrow(value_type->type)) return 0;
    if (!tl_polytype_is_concrete(name_type) || !tl_polytype_is_concrete(value_type)) return 0;
    if (!ast_node_is_symbol(node->let_in.value)) return 0;

    str value_name = ast_node_str(node->let_in.value);
    str inst_name  = specialize_arrow(self, ctx, traverse_ctx, value_name, value_type->type);
    if (str_is_empty(inst_name)) return 0;
    ast_node_name_replace(node->let_in.value, inst_name);
    return 0;
}

static int specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {

    infer_ctx *ctx = traverse_ctx->user;

    // check for nullary type constructors
    if (ast_node_is_symbol(node)) return specialize_user_type(self, node);
    // check for let_in nodes
    if (ast_node_is_let_in(node)) return specialize_let_in(self, ctx, traverse_ctx, node);
    // or else the remainder of this function handles nfas
    if (!ast_node_is_nfa(node)) return 0;

    str name = ast_node_str(node->named_application.name);
    log(self, "specialize_applications_cb: nfa '%.*s'", str_ilen(node->named_application.name->symbol.name),
        str_buf(&node->named_application.name->symbol.name));

    // do not process intrinsic calls or their arguments
    if (is_intrinsic(name) || traverse_ctx->is_intrinsic_argument) return 0;

    tl_polytype *type = tl_type_env_lookup(self->env, name);
    if (!type) {
        return 0; // mutual recursion or variable holding function pointer
    }

    if (tl_polytype_is_type_constructor(type)) return specialize_user_type(self, node);

    // instantiate generic function type being applied
    ast_arguments_iter iter     = ast_node_arguments_iter(node);
    ast_node          *fun_node = toplevel_get(self, name);
    if (!fun_node) return 0; // too early

    // Important: use _with variant to copy free variables info to the arrow, which is added to the
    // environment further down.
    tl_polytype *app = make_arrow_with(self, iter.nodes, node, type);
    if (!app) return 1;

    // Important: resolve type variables by calling polytype_substitute.
    tl_polytype_substitute(self->arena, app, self->subs); // const cast

    str app_str = tl_polytype_to_string(self->transient, app);
    log(self, "specialize application: callsite '%.*s' arrow: %.*s", str_ilen(name), str_buf(&name),
        str_ilen(app_str), str_buf(&app_str));

    // try to specialize
    int res = specialize_one(self, ctx, traverse_ctx, node->named_application.name, app->type);

    if (1 == res) return 1;
    if (0 == res) {
        // and recurse over any arguments which are toplevel functions

        iter = ast_node_arguments_iter(node);
        ast_node         *arg;
        tl_monotype_sized app_args = tl_monotype_arrow_args(app->type)->list.xs;
        u32               i        = 0;
        while ((arg = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(arg)) goto next;

            // take the updated arrow type from the specialization and constrain the args again
            assert(i < app_args.size);
            if (tl_polytype_is_scheme(arg->type))
                arg->type = tl_polytype_absorb_mono(
                  self->transient, tl_polytype_instantiate(self->arena, arg->type, self->subs));

            if (constrain_pm(self, ctx, arg->type, app_args.v[i], arg)) return 1;

            str       arg_name = ast_node_str(arg);
            ast_node *top      = toplevel_get(self, arg_name);
            if (!top) goto next;
            if (top->type && !tl_monotype_is_arrow(top->type->type)) goto next;

            if (specialize_one(self, ctx, traverse_ctx, arg, app_args.v[i])) return 1;

        next:
            ++i;
        }
        // remove name from specials after recursing through arguments, so it doesn't shadow subsequent uses
        // of the same name, eg: let id x = x in let x1 = id 0 in let x2 = id "hello" in x1
        str_map_erase(ctx->specials, name);
    }

    return 0;
}

static str lookup_arrow(tl_infer *self, str name, tl_monotype *arrow) {

    str inst_name = str_empty();
    if (!tl_monotype_is_concrete(arrow)) return inst_name;

    // de-duplicate instances: hashes give us structural equality (barring hash collisions), which we need
    // because types are frequently cloned.
    name_and_type key      = {.name_hash = str_hash64(name), .type_hash = tl_monotype_hash64(arrow)};
    str          *existing = map_get(self->instances, &key, sizeof key);
    if (existing) return *existing;
    return str_empty();
}

static str specialize_arrow(tl_infer *self, infer_ctx *ctx, traverse_ctx *traverse_ctx, str name,
                            tl_monotype *arrow) {
    // TODO: cleanup combine with specialize_one

    str inst_name = str_empty();
    if (!tl_monotype_is_concrete(arrow)) return inst_name;
    else {
        // does a specialized toplevel with this name and concrete type already exist?
        if (str_hset_contains(self->instance_names, name)) return name;
    }

    inst_name = lookup_arrow(self, name, arrow);
    if (!str_is_empty(inst_name)) return inst_name;

    ast_node *top = toplevel_get(self, name);
    str      *existing;
    if ((existing = str_map_get(ctx->specials, name))) {
        // exists in recursive context
        return *existing;
    } else {
        inst_name = specialize_fun(self, top, arrow);
        str_map_set(&ctx->specials, name, &inst_name);
        ast_node *special      = toplevel_get(self, inst_name);

        ast_node *infer_target = get_infer_target(special);
        if (infer_target) {
            if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) return str_empty();
            if (traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb))
                return str_empty();
        }
        return inst_name;
    }
}

// --

static str next_variable_name(tl_infer *);

// Performs alpha-conversion on the AST to ensure all bound variables have globally unique names while
// preserving lexical scope. This simplifies later passes by removing name collision concerns.

static void rename_let_in(tl_infer *self, ast_node *node, hashmap **lex) {
    // For toplevel definitions, rename them and keep them in lexical scope.
    if (!ast_node_is_let_in(node)) return;

    str name = node->let_in.name->symbol.name;
    if (is_c_symbol(name)) return;

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
    ast_node_type_set(node, null);

    switch (node->tag) {

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
        if (is_c_symbol(name)) break;
        if (level) {
            // do not rename toplevel symbols again (see rename_let_in)
            str newvar = next_variable_name(self);

            // establish lexical scope of the let-in binding and recurse
            save = map_copy(*lex);
            str_map_set(lex, name, &newvar);

            rename_variables(self, node->let_in.name, lex, level + 1);
        }

        rename_variables(self, node->let_in.body, lex, level + 1);

        // restore prior scope
        if (save) {
            map_destroy(lex);
            *lex = save;
        }
    } break;

    case ast_symbol: {
        str *found;
        if ((found = str_map_get(*lex, node->symbol.name))) {
            ast_node_name_replace(node, *found);
            log(self, "rename %.*s => %.*s", str_ilen(node->symbol.original),
                str_buf(&node->symbol.original), str_ilen(node->symbol.name), str_buf(&node->symbol.name));
        } else {
            // a free variable
        }

        // ensure renamed symbols do not carry a type
        node->type                   = null;
        node->symbol.annotation_type = null;

        // traverse into annotation too, to support type arguments.
        // Note: keep in sync with ast_let_in arm.
        rename_variables(self, node->symbol.annotation, lex, level + 1);
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

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) rename_variables(self, arr.v[i], lex, level + 1);
    } break;

    case ast_assignment:
        rename_variables(self, node->assignment.name, lex, level + 1);
        rename_variables(self, node->assignment.value, lex, level + 1);
        break;

    case ast_binary_op:
        rename_variables(self, node->binary_op.left, lex, level + 1);
        rename_variables(self, node->binary_op.right, lex, level + 1);
        break;

    case ast_unary_op: rename_variables(self, node->unary_op.operand, lex, level + 1); break;

    case ast_return:   rename_variables(self, node->return_.value, lex, level + 1); break;

    case ast_while:
        rename_variables(self, node->while_.condition, lex, level + 1);
        rename_variables(self, node->while_.body, lex, level + 1);
        break;

    case ast_body:
        //
        forall(i, node->body.expressions) {
            rename_variables(self, node->body.expressions.v[i], lex, level + 1);
        }
        break;

    case ast_hash_command:
    case ast_continue:
    case ast_string:
    case ast_nil:
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

static str  specialize_fun(tl_infer *self, ast_node *node, tl_monotype *arrow) {
    str name = toplevel_name(node);

    // de-duplicate instances: hashes give us structural equality (barring hash collisions), which we need
    // because types are frequently cloned.
    name_and_type key      = {.name_hash = str_hash64(name), .type_hash = tl_monotype_hash64(arrow)};
    str          *existing = map_get(self->instances, &key, sizeof key);
    if (existing) return *existing;

    // instantiate unique name
    str name_inst = next_instantiation(self, name);
    map_set(&self->instances, &key, sizeof key, &name_inst);
    str_hset_insert(&self->instance_names, name_inst);

    // clone function source ast and rename variables, which also erases type information
    ast_node *generic_node = clone_generic(self, toplevel_get(self, name));

    // recalculate free variables, because symbol names have been renamed
    tl_polytype wrap                      = tl_polytype_wrap(arrow);
    ((tl_monotype *)arrow)->list.fvs.size = 0; // const cast
    add_free_variables_to_arrow(self, generic_node, &wrap);

    // add to type environment
    if (!tl_monotype_is_concrete(arrow)) {
        // Note: functions like c_malloc etc will not have concrete types but still need to exist in the
        // environment.
        str arrow_str = tl_monotype_to_string(self->transient, arrow);
        log(self, "note: adding non-concrete type to environment: '%s' : %s", str_cstr(&name_inst),
             str_cstr(&arrow_str));
    }
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
        // assign concrete types to parameters based on callsite arguments
        assert(tl_list == arrow->tag);

        assert(arrow->list.xs.size == 2);
        assert(tl_tuple == arrow->list.xs.v[0]->tag);
        tl_monotype_sized callsite_args = arrow->list.xs.v[0]->list.xs;
        assert(callsite_args.size == params.size);

        forall(i, params) {
            ast_node *param = params.v[i];
            param->type =
              tl_polytype_absorb_mono(self->arena, tl_monotype_clone(self->arena, callsite_args.v[i]));
        }

        tl_monotype *inst_result = tl_monotype_sized_last(arrow->list.xs);
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

static tl_polytype *make_arrow(tl_infer *self, ast_node_sized args, ast_node *result) {

    if (result) ensure_tv(self, null, &result->type);

    if (args.size == 0 || (args.size == 1 && ast_node_is_nil(args.v[0]))) {
        // always use a tuple on the left side of arrow, even if zero elements
        tl_monotype *lhs   = tl_monotype_create_tuple(self->arena, (tl_monotype_sized){0});
        tl_monotype *rhs   = result ? tl_monotype_clone(self->arena, result->type->type) : null;
        tl_monotype *arrow = tl_monotype_create_arrow(self->arena, lhs, rhs);

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
            tl_monotype *arg_type = args.v[i]->type->type;
            tl_monotype *ty       = tl_monotype_clone(self->arena, arg_type);
            array_push(clone, ty);
        }

        tl_monotype *left  = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(clone));
        tl_monotype *right = null;
        if (result) {
            right = tl_monotype_clone(self->arena, result->type->type);
        } else {
            right = tl_type_registry_nil(self->registry);
        }

        tl_monotype *out = tl_monotype_create_arrow(self->arena, left, right);

        {
            str str = tl_monotype_to_string(self->transient, out);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }

        return tl_polytype_absorb_mono(self->arena, out);
    }
}

static tl_polytype *make_arrow_with(tl_infer *self, ast_node_sized args, ast_node *result,
                                    tl_polytype *type) {
    tl_polytype *out = make_arrow(self, args, result);
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
    if (!ast_node_is_symbol(node) || traverse_ctx->is_field_name) return 0;

    str name = ast_node_str(node);

    // don't collect symbols which are nullary type literals
    if (tl_type_registry_is_nullary_type(self->registry, name)) return 0;

    // don't collect symbols that start with c_
    if (0 == str_cmp_nc(name, "c_", 2)) return 0;

    collect_free_variables_ctx *ctx      = traverse_ctx->user;

    tl_polytype                *type     = tl_type_env_lookup(self->env, node->symbol.name);
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

    ast_node    *infer_target = get_infer_target(node);
    ast_node    *name_node    = toplevel_name_node(node);
    tl_polytype *provisional  = null;

    str          name         = name_node->symbol.name;
    str          orig_name    = name_node->symbol.original;

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
        // already loaded from load_toplevel
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

    process_annotation(self, name_node, null);

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
    tl_polytype *arrow = null;
    if (name_node->symbol.annotation_type) {
        arrow = name_node->symbol.annotation_type;
    } else {
        tl_polytype *tmp = tl_type_env_lookup(self->env, name);
        if (!tmp) fatal("runtime error");
        arrow = tl_polytype_clone(self->arena, tmp);
    }
    tl_polytype_generalize((tl_polytype *)arrow, self->env, self->subs); // const cast

    // collect free variables from infer target and add to the generic's arrow type
    add_free_variables_to_arrow(self, infer_target, (tl_polytype *)arrow); // const cast
    tl_type_env_insert(self->env, name, arrow);

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

void do_admit_generic_pointers(void *ctx, ast_node *node) {
    tl_monotype *nil = ctx;
    if (node->type) tl_monotype_force_tv_to_nil(node->type->type, nil);
}

void admit_generic_pointers(tl_infer *self) {

    // Note: special case for undecided Ptr(a) types: force them to Ptr(Nil) so that the transpiler will
    // accept them as void*.

    tl_monotype     *nil = tl_type_registry_nil(self->registry);
    ast_node        *node;
    hashmap_iterator iter = {0};
    while ((node = toplevel_iter(self, &iter))) {
        ast_node_dfs(nil, node, do_admit_generic_pointers);

        str          name = ast_node_str(toplevel_name_node(node));
        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("runtime error");

        if (!tl_polytype_is_concrete(type)) tl_monotype_force_tv_to_nil(type->type, nil);
    }
}

void do_resolve_unions(void *ctx, ast_node *node) {
    (void)ctx;

    if (!node->type) return;
    tl_monotype_force_union_resolve(node->type->type);
    if (ast_node_is_symbol(node) && node->symbol.annotation_type)
        tl_monotype_force_union_resolve(node->symbol.annotation_type->type);
}

void resolve_unions(tl_infer *self) {
    // Note: Union types may remain unresolved during type checking: resolve them by arbitrarily picking a
    // variant.

    {
        ast_node        *node;
        hashmap_iterator iter = {0};
        while ((node = toplevel_iter(self, &iter))) {
            ast_node_dfs(null, node, do_resolve_unions);
        }
    }
    {
        hashmap_iterator iter = {0};
        while (map_iter(self->env->map, &iter)) {
            tl_polytype **poly = iter.data;
            if (!poly || !(*poly)->type) continue;
            tl_monotype_force_union_resolve((*poly)->type);
        }
    }
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

        if (!tl_polytype_is_concrete(type)) array_push(names, name);
    }

    forall(i, names) {
        log(self, "remove_generic_toplevels: removing '%s'", str_cstr(&names.v[i]));
        toplevel_del(self, names.v[i]);
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

    forall(i, remove) {
        log(self, "tree_shake_toplevels: removing '%s'", str_cstr(&remove.v[i]));
        toplevel_del(self, remove.v[i]);
    }
    array_free(remove);
    map_destroy(&used);
}

static int check_main_function(tl_infer *self, ast_node *main) {
    // instantiate and infer main
    assert(ast_node_is_let(main));
    tl_polytype *type = tl_type_env_lookup(self->env, S("main"));
    if (!type) fatal("main function with no type");

    tl_polytype *body_type = main->let.body->type;
    if (!body_type || tl_polytype_is_scheme(body_type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_main_function_bad_type, .node = main}));
        return 1;
    }

    // remove free variables from main type
    if (tl_monotype_is_arrow(type->type)) {
        ((tl_monotype *)type->type)->list.fvs.size = 0;
    }

    return 0;
}

static tl_monotype *get_or_specialize_type(tl_infer *self, str type_name, tl_monotype_sized args) {
    tl_monotype *mono = tl_type_registry_get_cached_instance(self->registry, type_name, args);
    if (!mono) {
        // not found, specialize it if all args are concrete
        tl_polytype *specialized = null;
        if (!tl_monotype_sized_is_concrete(args)) return null;
        specialize_type_constructor(self, type_name, args, &specialized);
        if (!specialized) return null;
        mono = specialized->type;
    }

    return tl_monotype_clone(self->arena, mono);
}

tl_monotype *tl_infer_update_specialized_type(tl_infer *self, tl_monotype *mono) {
    // Note: this function pretty definitely breaks the isolation between tl_infer and the transpiler so
    // that makes me a little bit sad. But it makes sizeof(TypeConstructor) work.

    switch (mono->tag) {

    case tl_any:
    case tl_var:
    case tl_weak: break;

    case tl_cons_inst:
        // already specialized?
        if (!str_is_empty(mono->cons_inst->special_name)) return null;

        // first recurse through the type args
        forall(i, mono->cons_inst->args) {
            tl_monotype *replace = tl_infer_update_specialized_type(self, mono->cons_inst->args.v[i]);
            if (replace) mono->cons_inst->args.v[i] = replace;
        }

        str               type_name = mono->cons_inst->def->name;
        tl_monotype_sized type_args = mono->cons_inst->args;
        tl_monotype      *replace   = get_or_specialize_type(self, type_name, type_args);
        if (!replace) return null;
        return replace;

    case tl_list:
    case tl_tuple:
        forall(i, mono->list.xs) {
            tl_monotype *replace = tl_infer_update_specialized_type(self, mono->list.xs.v[i]);
            if (replace) mono->list.xs.v[i] = replace;
        }
        break;
    }
    return null;
}

static void update_types_one_type(tl_infer *self, tl_polytype **poly) {
    if (!poly || !*poly) return; // not all ast nodes will have types

    if (tl_polytype_is_type_constructor(*poly)) {
        tl_monotype *replace = tl_infer_update_specialized_type(self, (*poly)->type);
        if (replace) *poly = tl_polytype_absorb_mono(self->arena, replace);

    } else {
        // otherwise walk through every monotype referenced by this polytype
        tl_infer_update_specialized_type(self, (*poly)->type);
    }
}

static void fixup_arrow_name(tl_infer *self, ast_node *ident) {
    if (ast_node_is_symbol(ident)) {
        tl_monotype *type = ident->type->type;
        if (!tl_monotype_is_arrow(type)) return;
        str name      = ast_node_str(ident);
        str inst_name = lookup_arrow(self, name, type);
        if (!str_is_empty(inst_name)) {
            ast_node_name_replace(ident, inst_name);
        }
    }
}

static void update_types_arrow(tl_infer *self, ast_node *node) {
    if (ast_node_is_let_in(node)) {
        ast_node *ident = node->let_in.value;
        fixup_arrow_name(self, ident);
    }
}

static int update_types_cb(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    (void)ctx;
    update_types_one_type(self, &node->type);
    update_types_arrow(self, node);

    // propagate the types back up the ast, especially for type constructors
    switch (node->tag) {
    case ast_assignment: ast_node_type_set(node, node->assignment.value->type); break;

    case ast_body:       {
        u32 n = node->body.expressions.size;
        if (n) ast_node_type_set(node, node->body.expressions.v[n - 1]->type);
    } break;

    case ast_let_in:
        if (node->let_in.body) ast_node_type_set(node, node->let_in.body->type);
        break;

    case ast_nil:
    case ast_arrow:
    case ast_binary_op:
    case ast_bool:
    case ast_continue:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_if_then_else:
    case ast_return:
    case ast_string:
    case ast_symbol:
    case ast_u64:
    case ast_user_type_definition:
    case ast_while:
    case ast_lambda_function:
    case ast_lambda_function_application:
    case ast_let:
    case ast_named_function_application:
    case ast_tuple:
    case ast_unary_op:
    case ast_hash_command:
    case ast_type_alias:                  break;
    }
    return 0;
}

static void update_specialized_types(tl_infer *self) {

    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype **poly = iter.data;
        update_types_one_type(self, poly);
    }

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    iter                   = (hashmap_iterator){0};
    ast_node *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        traverse_ast(self, traverse, node, update_types_cb);
    }
    traverse_ctx_destroy(self->transient, &traverse);
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
    self->toplevels = ast_node_str_map_create(self->arena, 1024);
    load_toplevel(self, nodes);
    arena_reset(self->transient);
    if (self->errors.size) return 1;

    log(self, "-- toplevels");
    log_toplevels(self);

    // now go through the toplevel let nodes and create generic functions: don't call add_generic from
    // inside the iteration because infer will add lambda functions to the toplevel.
    forall(i, nodes) {
        if (ast_node_is_hash_command(nodes.v[i])) continue;
        if (ast_node_is_type_alias(nodes.v[i])) continue;
        add_generic(self, nodes.v[i]);
    }
    arena_reset(self->transient);

    if (self->errors.size) return 1;

    // check if free variables are present
    if (check_missing_free_variables(self)) return 1;
    arena_reset(self->transient);

    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    log(self, "-- inference complete --");
    log(self, "");
    log(self, "-- toplevels");
    log_toplevels(self);
    log(self, "-- subs");
    log_subs(self);
    log(self, "-- env");
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

    // Final phase: communiate type information top-down by following applications. This contrasts with the
    // bottom-up inference we just completed. At this point the program is well-typed and we are setting up
    // for the transpiler.
    log(self, "-- specialize phase");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    infer_ctx    *ctx      = infer_ctx_create(self->transient);
    traverse->user         = ctx;

    if (main) {
        traverse_ast(self, traverse, main, specialize_applications_cb);
    } else {
        // TODO: this isn't really the full story for building libraries. For now, we must stay will full
        // program analysis.
        ast_node *node = null;
        forall(i, nodes) {
            node = nodes.v[i];

            if (ast_node_is_let(node)) {
                ast_node *name = toplevel_name_node(node);
                if (!name->symbol.annotation_type) {
                    str fun_name = ast_node_str(name);
                    log(self, "skipping '%s' due to lack of annotation", str_cstr(&fun_name));
                    continue;
                }
            }
            traverse_ast(self, traverse, node, specialize_applications_cb);
        }
    }
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
    if (main) {
        if (check_main_function(self, main)) return 1;
        arena_reset(self->transient);
    }

    admit_generic_pointers(self);
    arena_reset(self->transient);

    resolve_unions(self);
    arena_reset(self->transient);

    remove_generic_toplevels(self);
    arena_reset(self->transient);

    if (main) {
        tree_shake_toplevels(self, main);
        arena_reset(self->transient);
    }

    log(self, "-- final subs");
    log_subs(self);
    log(self, "-- final env --");
    log_env(self);
    arena_reset(self->transient);
    log(self, "-- final toplevels");
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

int is_intrinsic(str name) {
    return (0 == str_cmp_nc(name, "_tl_", 4));
}

int is_c_symbol(str name) {
    return (0 == str_cmp_nc(name, "c_", 2));
}

int is_c_struct_symbol(str name) {
    return (0 == str_cmp_nc(name, "c_struct_", 9));
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
        else if (ast_node_is_nfa(node->type_alias.name))
            return node->type_alias.name->named_application.name;
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
    log(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

static void log_type_error(tl_infer *self, tl_polytype *left, tl_polytype *right) {
    if (!self->verbose) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    log(self, "error: constraints are not compatible:  %s versus %s", str_cstr(&left_str),
        str_cstr(&right_str));
}
static void log_type_error_mm(tl_infer *self, tl_monotype *left, tl_monotype *right) {
    tl_polytype l = tl_polytype_wrap((tl_monotype *)left), r = tl_polytype_wrap((tl_monotype *)right);
    return log_type_error(self, &l, &r);
}

static void log_subs(tl_infer *self) {
    if (self->verbose) tl_type_subs_log(self->transient, self->subs);
}
