#include "v2_infer.h"
#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "dbg.h"
#include "error.h"
#include "hash.h"
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

    tl_type_context      context;
    tl_type_env         *env;
    tl_type_subs        *subs; // corresponds to E in algorithm J

    hashmap             *toplevels; // str => ast_node*
    hashmap             *instances; // u64 hash => str specialised name in env
    tl_infer_error_array errors;

    u32                  next_var_name;
    u32                  next_instantiation;

    int                  verbose;
    int                  indent_level;
};

//

static str          v2_ast_node_to_string(allocator *, ast_node const *);
static str          next_variable_name(tl_infer *);
static tl_type_v2   instantiate(tl_infer *, tl_type_v2);
static tl_type_v2   make_arrow(tl_infer *, ast_node_sized, ast_node const *);
static tl_monotype *arrow_rightmost(tl_monotype *);
static str          next_instantiation(tl_infer *, str);
static void         add_generic(tl_infer *, ast_node *);
static void         collect_free_variables(tl_infer *, ast_node *, hashmap **, str_array *);

static void         toplevel_add(tl_infer *, str, ast_node *);
static ast_node    *toplevel_get(tl_infer *, str);
static ast_node    *toplevel_iter(tl_infer *, hashmap_iterator *);

static void         log(tl_infer const *self, char const *restrict fmt, ...);
static void         log_toplevels(tl_infer const *);
static void         log_env(tl_infer const *, tl_type_env const *);
static void         log_subs(tl_infer const *, tl_type_subs const *);

//

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self           = new (alloc, tl_infer);

    self->transient          = arena_create(alloc, 4096);
    self->arena              = arena_create(alloc, 16 * 1024);
    self->context            = tl_type_context_empty();
    self->env                = tl_type_env_create(self->arena);
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
    self->verbose = verbose;
}

static tl_type_v2 *make_type_annotation(tl_infer *self, ast_node *ann, hashmap **map) {
    if (ast_nil == ann->tag) {
        return tl_type_env_lookup(self->env, S("Nil"));
    }

    // if (ast_ellipsis == ann->tag) {
    //     tl_type **found = type_registry_find_name(self->type_registry, S("ellipsis"));
    //     if (found) return *found;
    //     fatal("ellipsis type not found");
    // }

    if (ast_symbol == ann->tag) {
        // either a prim or user type, or a generic/quantifier
        str         ann_str = ann->symbol.name;
        tl_type_v2 *found   = tl_type_env_lookup(self->env, ann_str);
        if (found) {
            // If it's an any type, assign it a new quantifier
            // if (type_any == (*found)->tag) return tl_type_context_new_quantifier(self);
            return found;
        }

        // previously seen in the annotation? then assign same type
        {
            tl_type_v2 **map_found = str_map_get(*map, ann_str);
            if (map_found) return *map_found;
        }

        // unknown symbol, consider it as a quantifier
        tl_type_v2 *out = new (self->arena, tl_type_v2);
        *out            = tl_type_init_mono(tl_type_context_new_quantifier(&self->context));
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

        // FIXME
        if (!left || !right) return null;

        // promote simple arrows like a -> b to tuple form: (a, ) -> b
        // if (!is_any_tuple(left)) {
        //     if (type_nil == left->tag) {
        //         left = tl_type_create_tuple(self->type_arena, (tl_type_sized){.size = 0});
        //     } else {
        //         tl_type_sized elements = {.v    = alloc_malloc(self->type_arena, sizeof
        //         elements.v[0]),
        //                                   .size = 1};
        //         elements.v[0]          = left;
        //         left                   = tl_type_create_tuple(self->type_arena, elements);
        //     }
        // }

        tl_type_v2 *out = new (self->arena, tl_type_v2);
        *out            = tl_type_init_mono(tl_monotype_alloc_arrow(self->arena, left->mono, right->mono));
        return out;
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
    assert(ast_symbol == node->tag);
    if (!node->symbol.annotation) return;

    hashmap    *map                 = map_create(self->transient, sizeof(tl_type_v2 *), 8);
    tl_type_v2 *ann                 = make_type_annotation(self, node->symbol.annotation, &map);
    node->symbol.annotation_type_v2 = ann;

    map_destroy(&map);
}

static hashmap *load_toplevel(tl_infer *self, allocator *alloc, ast_node_sized nodes,
                              tl_infer_error_array *out_errors) {
    hashmap             *tops   = map_create(alloc, sizeof(ast_node *), 1024);
    tl_infer_error_array errors = {.alloc = alloc};
    (void)self;

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        dbg("processing: %s\n", ast_node_to_string(alloc, node));
        if (ast_symbol == node->tag) {
            str        name_str = node->symbol.name;
            ast_node **p        = str_map_get(tops, name_str);
            if (p) {
                // merge annotation if existing node is a let node; otherwise error
                if (ast_let != (*p)->tag) {
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
                    log(self, "toplevel 7: add %.*s", str_ilen(name_str), str_buf(&name_str));
                    str_map_set(&tops, name_str, &node);
                    process_annotation(self, node);
                }
            }
        }

        else if (ast_let == node->tag) {
            str        name_str = ast_node_str(node->let.name);
            ast_node **p        = str_map_get(tops, name_str);

            // assign expresion type
            // assign_expression_types(self, node);

            if (p) {
                // merge type if the existing node is a symbol; otherwise error
                if (ast_symbol != (*p)->tag) {
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
                log(self, "toplevel 8: add %.*s", str_ilen(name_str), str_buf(&name_str));
                str_map_set(&tops, name_str, &node);
                process_annotation(self, node->let.name);
            }
        }

        else if (ast_user_type_definition == node->tag) {
            str        name_str = ast_node_str(node->user_type_def.name);
            ast_node **p        = str_map_get(tops, name_str);

            if (p) {
                array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            } else {
                log(self, "toplevel 9: add %.*s", str_ilen(name_str), str_buf(&name_str));
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

// -- inference --

typedef struct {
    hashmap      *lex;                      // str => tl_type_v2*
    tl_type_subs *subs;                     // corresponds to E in algorithm J
    tl_type_env  *local_env;                // local environment
    int           instantiate_applications; // whether to instantiate generic applications
    int           add_to_env;               // whether to add variable types to the environment
    tl_type_v2    lambda_type;              // for inferring lambda_function only
} infer_ctx;

static infer_ctx *infer_ctx_create(allocator *alloc) {
    infer_ctx *out                = new (alloc, infer_ctx);
    out->lex                      = map_create(alloc, sizeof(tl_type_v2 *), 16);
    out->subs                     = tl_type_subs_create(alloc);
    out->local_env                = tl_type_env_create(alloc);
    out->instantiate_applications = 0;
    out->add_to_env               = 0;
    return out;
}

static void infer_ctx_destroy(allocator *alloc, infer_ctx **p) {
    if ((*p)->lex) map_destroy(&(*p)->lex);
    if ((*p)->subs) tl_type_subs_destroy(alloc, &(*p)->subs);
    if ((*p)->local_env) tl_type_env_destroy(alloc, &(*p)->local_env);

    alloc_free(alloc, *p);
    *p = null;
}

static int type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_error, .node = node}));
    return 1;
}

static int is_type_variable(tl_type_v2 const *self) {
    return tl_mono == self->tag && tl_var == self->mono.tag;
}

static int is_tv_eq(tl_type_variable tv, tl_monotype const *mono) {
    return tl_var == mono->tag && tv == mono->var;
}

static int constraint_is_compatible(tl_monotype existing, tl_monotype apply) {
    switch (existing.tag) {
    case tl_nil:
    case tl_cons:
        if (!tl_monotype_eq(apply, existing)) return 0; // type error
        return 1;

    case tl_var:   return 1;
    case tl_quant: return 0;

    case tl_arrow:
        return tl_arrow == apply.tag && constraint_is_compatible(*existing.arrow.lhs, *apply.arrow.lhs) &&
               constraint_is_compatible(*existing.arrow.rhs, *apply.arrow.rhs);
    }
}

static int unify(tl_infer *self, infer_ctx *ctx, tl_type_variable tv, tl_monotype mono) {
    // unify tv with mono in the context of ctx->subs: if mono is a type variable that exists in the
    // context of subs, replace it with the existing substitution. Similarly recursively for type
    // constructor arguments and arrow arms.

    if (tl_monotype_occurs(tl_monotype_init_tv(tv), mono)) return 0;

    switch (mono.tag) {

    case tl_nil:  break;

    case tl_cons: {
        // attempt to match type constructor arguments against existing type vars
        forall(i, mono.cons.args) {
            if (tl_var == mono.cons.args.v[i].tag) {
                tl_monotype *found = tl_type_subs_get(ctx->subs, mono.cons.args.v[i].var);
                if (found) mono.cons.args.v[i] = *found;
            }
        }
    } break;

    case tl_var: {
        // if mono is a tv, unify it with existing tv, if any
        tl_monotype *mono_match = tl_type_subs_get(ctx->subs, mono.var);
        if (mono_match) return unify(self, ctx, tv, *mono_match);
    } break;

    case tl_quant: fatal("attempt to unify quantifier");

    case tl_arrow: {
        // if either arm is an existing tv, use its substitution instead, because we want to add a
        // minimal amount of new tvs to the context
        tl_monotype *left_match  = null;
        tl_monotype *right_match = null;

        if (tl_var == mono.arrow.lhs->tag) left_match = tl_type_subs_get(ctx->subs, mono.arrow.lhs->var);
        if (tl_var == mono.arrow.rhs->tag) right_match = tl_type_subs_get(ctx->subs, mono.arrow.rhs->var);

        if (left_match) mono.arrow.lhs = left_match;
        if (right_match) mono.arrow.rhs = right_match;

    } break;
    }

    // add the substitution
    tl_type_subs_add(ctx->subs, tv, mono);

    // apply the substitution, if possible, to the rest of substitutions
    if (tl_var != mono.tag) {
        // any existing subs with tv on the right hand side should be replaced with the new non-tv
        // mono
        hashmap_iterator iter = {0};
        while (map_iter(ctx->subs->map, &iter)) {
            tl_monotype *rhs = iter.data;

            if (tl_var == rhs->tag && rhs->var == tv) *rhs = mono;

            else if (tl_arrow == rhs->tag) {
                tl_type_v2_arrow *a = &rhs->arrow;

                // Note: clone required here to prevent recursive arrows
                if (is_tv_eq(tv, a->lhs)) *a->lhs = tl_monotype_clone(self->arena, mono);
                if (is_tv_eq(tv, a->rhs)) *a->rhs = tl_monotype_clone(self->arena, mono);
            }

            else if (tl_cons == rhs->tag) {
                tl_type_constructor_inst *c = &rhs->cons;
                forall(i, c->args) {
                    if (is_tv_eq(tv, &c->args.v[i])) c->args.v[i] = tl_monotype_clone(self->arena, mono);
                }
            }
        }
    }

    return 0;
}

static void log_constraint(tl_infer *self, tl_type_v2 const *left, tl_type_v2 const *right,
                           ast_node const *node) {
    if (!self->verbose) return;
    str left_str  = tl_type_v2_to_string(self->transient, left);
    str right_str = tl_type_v2_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    log(self, "constrain: %.*s : %.*s from %.*s", str_ilen(left_str), str_buf(&left_str),
        str_ilen(right_str), str_buf(&right_str), str_ilen(node_str), str_buf(&node_str));
}

static void log_type_error(tl_infer *self, tl_type_v2 const *left, tl_type_v2 const *right) {
    if (!self->verbose) return;
    str left_str  = tl_type_v2_to_string(self->transient, left);
    str right_str = tl_type_v2_to_string(self->transient, right);
    log(self, "error: constraints are not compatible:  %.*s versus %.*s", str_ilen(left_str),
        str_buf(&left_str), str_ilen(right_str), str_buf(&right_str));
}
static void log_type_error_mm(tl_infer *self, tl_monotype const *left, tl_monotype const *right) {
    tl_type_v2 l = tl_type_init_mono(*left), r = tl_type_init_mono(*right);
    return log_type_error(self, &l, &r);
}

static int constrain(tl_infer *, infer_ctx *, tl_type_v2 const *, tl_type_v2 const *, ast_node const *);
static int constrain_mm(tl_infer *, infer_ctx *, tl_monotype const *, tl_monotype const *,
                        ast_node const *);

static int constrain_tv(tl_infer *self, infer_ctx *ctx, tl_type_variable tv, tl_monotype mono,
                        ast_node const *node) {
    tl_monotype *exist = tl_type_subs_get(ctx->subs, tv);
    if (!exist) return unify(self, ctx, tv, mono);

    // There exists a substitution on tv. Decide how to compose with the new substitution.
    if (constraint_is_compatible(*exist, mono)) {
        // the new constraint does not conflict
        switch (exist->tag) {
        case tl_var:
        case tl_arrow: return constrain_mm(self, ctx, exist, &mono, node);

        case tl_nil:
        case tl_cons:
            // the compat check decided the types were equal, so we can ignore it
            return 0;

        case tl_quant: fatal("logic error"); // unreachable
        }
    } else if (constraint_is_compatible(mono, *exist)) {
        // the mirror does not conflict: mono could be a tv we can add to subs
        if (tl_var == mono.tag) {
            if (!tl_type_subs_get(ctx->subs, mono.var)) {
                return constrain_tv(self, ctx, mono.var, tl_monotype_init_tv(tv), node);
            } else {
                // the tv also exists: check all three right-hand
                // constraints: the two existing ones, and the new
                // one. They must all be compatible.
                tl_monotype *exist2 = tl_type_subs_get(ctx->subs, mono.var);
                assert(exist2);
                if (!constraint_is_compatible(mono, *exist2)) {
                    log_type_error_mm(self, &mono, exist2);
                    return type_error(self, node);
                }

                // since we don't have room, we can't add this new substitution. We can try to eliminate a
                // substitution by applying it to our environment. But I don't know if this is sound yet.
                // For example: exist: 1 == 2, 2 == 3, 3 == 1 : add 3 == 4
                fatal("oops");
            }
        } else {
            // a compatible nil, cons or arrow: this can be ignored
            return 0;
        }
    } else {
        log_type_error_mm(self, exist, &mono);
        return type_error(self, node);
    }
}

static nodiscard int constrain(tl_infer *self, infer_ctx *ctx, tl_type_v2 const *left,
                               tl_type_v2 const *right, ast_node const *node) {
    if (tl_mono != left->tag || tl_mono != right->tag) fatal("cannot constrain type scheme");
    if (tl_monotype_occurs(left->mono, right->mono)) return 0;
    log_constraint(self, left, right, node);

    if (is_type_variable(left)) return constrain_tv(self, ctx, left->mono.var, right->mono, node);
    else if (is_type_variable(right)) return constrain_tv(self, ctx, right->mono.var, left->mono, node);

    if (left->mono.tag != right->mono.tag) {
        log_type_error(self, left, right);
        return type_error(self, node);
    }

    switch (left->mono.tag) {
    case tl_nil: return 0;
    case tl_cons:
        if (left->mono.cons.args.size != right->mono.cons.args.size) {
            log_type_error(self, left, right);
            return type_error(self, node);
        }

        forall(i, left->mono.cons.args) {
            if (constrain_mm(self, ctx, &left->mono.cons.args.v[i], &right->mono.cons.args.v[i], node))
                return 1;
        }
        break;

    case tl_var:
    case tl_quant: fatal("logic error");

    case tl_arrow: {
        if (constrain_mm(self, ctx, left->mono.arrow.lhs, right->mono.arrow.lhs, node)) return 1;
        if (constrain_mm(self, ctx, left->mono.arrow.rhs, right->mono.arrow.rhs, node)) return 1;

    } break;
    }

    return 0;
}

static nodiscard int constrain_mm(tl_infer *self, infer_ctx *ctx, tl_monotype const *left,
                                  tl_monotype const *right, ast_node const *node) {
    tl_type_v2 left_ty  = tl_type_init_mono(*left);
    tl_type_v2 right_ty = tl_type_init_mono(*right);
    return constrain(self, ctx, &left_ty, &right_ty, node);
}

static void ensure_tv(tl_infer *self, tl_type_v2 **type) {
    if (!type) return;
    if (*type) return;
    *type  = new (self->arena, tl_type_v2);
    **type = tl_type_init_mono(tl_monotype_init_tv(tl_type_context_new_variable(&self->context)));
}

static str instantiate_fun_and_infer(tl_infer *, infer_ctx *, ast_node *, tl_monotype);

static int infer(tl_infer *self, infer_ctx *ctx, ast_node *node) {
    // - update ctx->subs by collecting and applying constraints found recursively starting at node.
    // - adds symbol : type information to ctx->env.
    // - can be run multiple times on the same node

    if (null == node) return 0;

    switch (node->tag) {
    case ast_nil:
    case ast_any:        break;
    case ast_address_of: fatal("FIXME: pointer types");

    case ast_string:     {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("String"));
        ensure_tv(self, &node->type_v2);
        return constrain(self, ctx, node->type_v2, ty, node);
    } break;

    case ast_f64: {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("Float"));
        ensure_tv(self, &node->type_v2);
        return constrain(self, ctx, node->type_v2, ty, node);
    } break;

    case ast_i64: {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("Int"));
        ensure_tv(self, &node->type_v2);
        return constrain(self, ctx, node->type_v2, ty, node);
    } break;

    case ast_u64: {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("Int")); // FIXME unsigned int
        ensure_tv(self, &node->type_v2);
        return constrain(self, ctx, node->type_v2, ty, node);
    } break;

    case ast_let_in: {
        if (ast_lambda_function == node->let_in.value->tag) {

            str name = node->let_in.name->symbol.name;

            // define a generic lambda in local scope
            add_generic(self, node);

            // add let-in node to toplevels (because we need the name and the body)
            if (!toplevel_get(self, name)) {
                toplevel_add(self, name, node);
            }

            // do not infer the node value - add_generic takes care of that

            hashmap *save = map_copy(ctx->lex);

            ensure_tv(self, &node->let_in.name->type_v2);
            str_map_set(&ctx->lex, node->let_in.name->symbol.name, &node->let_in.name->type_v2);

            if (infer(self, ctx, node->let_in.body)) return 1;

            ensure_tv(self, &node->type_v2);
            if (constrain(self, ctx, node->type_v2, node->let_in.body->type_v2, node)) return 1;

            map_destroy(&ctx->lex);
            ctx->lex = save;

            // add_generic will have added generic lambda to its local environment, using the
            // ctx->lambda_type field

        } else {

            if (infer(self, ctx, node->let_in.value)) return 1;

            hashmap *save = map_copy(ctx->lex);

            ensure_tv(self, &node->let_in.name->type_v2);
            str_map_set(&ctx->lex, node->let_in.name->symbol.name, &node->let_in.name->type_v2);

            if (infer(self, ctx, node->let_in.body)) return 1;

            ensure_tv(self, &node->type_v2);
            if (constrain(self, ctx, node->type_v2, node->let_in.body->type_v2, node)) return 1;

            map_destroy(&ctx->lex);
            ctx->lex = save;

            // add to local environment
            if (ctx->add_to_env) {
                tl_type_env_add(ctx->local_env, node->let_in.name->symbol.name,
                                *node->let_in.value->type_v2);
            }
        }
    } break;

    case ast_let: {
        hashmap *save = map_copy(ctx->lex);
        for (u32 i = 0; i < node->let.n_parameters; ++i) {
            ast_node *param = node->let.parameters[i];
            ensure_tv(self, &param->type_v2);
            if (ast_nil != param->tag) str_map_set(&ctx->lex, param->symbol.name, &param->type_v2);
        }

        if (infer(self, ctx, node->let.body)) return 1;

        map_destroy(&ctx->lex);
        ctx->lex = save;

        // add to local environment
        if (ctx->add_to_env) {
            tl_type_v2 arrow =
              make_arrow(self, (ast_node_sized){.size = node->let.n_parameters, .v = node->let.parameters},
                         node->let.body);

            tl_type_env_add(ctx->local_env, node->let.name->symbol.name, arrow);
        }

    } break;

    case ast_symbol: {
        tl_type_v2  *global = tl_type_env_lookup(self->env, node->symbol.name);
        tl_type_v2 **found  = null;
        if (global) {
            node->type_v2 = global;
        } else if ((found = str_map_get(ctx->lex, node->symbol.name)) && *found) {
            node->type_v2 = *found;
        } else {
            ensure_tv(self, &node->type_v2);
        }

        // add to local environment
        if (ctx->add_to_env) tl_type_env_add(ctx->local_env, node->symbol.name, *node->type_v2);

    } break;

    case ast_named_function_application: {

        if (!ctx->instantiate_applications) {
            ensure_tv(self, &node->type_v2);

            // even if we are not instantiating fun applications this pass, we can still infer the
            // arguments to aid in free variable discovery infer the arguments
            for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
                if (infer(self, ctx, node->named_application.arguments[i])) return 1;
            }

            return 0;
        }

        // name can be: local_env fun, global env fun, toplevel fun
        str         name = node->named_application.name->symbol.name;
        tl_type_v2 *fun  = tl_type_env_lookup(ctx->local_env, name);
        if (!fun) fun = tl_type_env_lookup(self->env, name);

        // name and type must exist in toplevels
        ast_node *fun_node = toplevel_get(self, name);
        if (!fun_node || !fun) {
            array_push(self->errors, ((tl_infer_error){.tag  = tl_err_function_not_found,
                                                       .node = node->named_application.name}));
            return 1;
        }

        tl_type_v2 inst = instantiate(self, *fun);
        assert(tl_mono == inst.tag && tl_arrow == inst.mono.tag);

        // infer the arguments
        for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
            if (infer(self, ctx, node->named_application.arguments[i])) return 1;
        }

        // constrain arrow types
        ensure_tv(self, &node->type_v2);
        tl_type_v2 app = make_arrow(self,
                                    (ast_node_sized){.size = node->named_application.n_arguments,
                                                     .v    = node->named_application.arguments},
                                    node);

        // constrain and apply substitutions to determine most specific type before instantiating the
        // generic function: otherwise the inst arrow will just be new typevars and deduplication of
        // instantiations won't be effective.
        if (constrain(self, ctx, &inst, &app, node)) return 1;
        tl_type_v2_apply_subs(&inst, ctx->subs);

        // now infer an *instantiated* function body (or use a prior instantiation)
        str name_inst = instantiate_fun_and_infer(self, ctx, fun_node, inst.mono);

        // constrain instantiated result type
        tl_type_v2 inst_right = tl_type_init_mono(*arrow_rightmost(&inst.mono));
        if (constrain(self, ctx, &inst_right, node->type_v2, node)) return 1;

        // replace name with instantiated name
        node->named_application.name->symbol.original = node->named_application.name->symbol.name;
        node->named_application.name->symbol.name     = name_inst;
    } break;

    case ast_lambda_function_application: {

        tl_type_v2 inst = *node->lambda_application.lambda->type_v2;
        assert(tl_mono == inst.tag && tl_arrow == inst.mono.tag);

        // constrain arrow types
        tl_type_v2 app = make_arrow(self,
                                    (ast_node_sized){.size = node->lambda_application.n_arguments,
                                                     .v    = node->lambda_application.arguments},
                                    node);
        if (constrain(self, ctx, &inst, &app, node)) return 1;

    } break;

    case ast_lambda_function: {
        hashmap *save = map_copy(ctx->lex);

        for (u32 i = 0; i < node->let.n_parameters; ++i) {
            ast_node *param = node->let.parameters[i];
            ensure_tv(self, &param->type_v2);
            if (ast_nil != param->tag) str_map_set(&ctx->lex, param->symbol.name, &param->type_v2);
        }

        if (infer(self, ctx, node->lambda_function.body)) return 1;

        map_destroy(&ctx->lex);
        ctx->lex         = save;

        tl_type_v2 arrow = make_arrow(self,
                                      (ast_node_sized){.size = node->lambda_function.n_parameters,
                                                       .v    = node->lambda_function.parameters},
                                      node->lambda_function.body);

        ensure_tv(self, &node->type_v2);
        if (constrain(self, ctx, &arrow, node->type_v2, node)) return 1;
        ctx->lambda_type = arrow;

    } break;

    case ast_arrow:
    case ast_assignment:
    case ast_bool:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
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
    case ast_tuple:
    case ast_user_type:            break;
    }

    tl_type_env_subs_apply(ctx->local_env, ctx->subs);
    return 0;
}

static void rename_variables(tl_infer *self, ast_node *node, hashmap **lex) {
    // transform variables recursively in node so that every occurrence is unique, respecting lexical
    // scope created by lambda functions, let functions, and let in expressions so that variables have
    // the same name when they refer to the same bound value.

    if (null == node) return;

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
            assert(ast_assignment == ass->tag);
            ast_node *name_node = ass->assignment.name;
            assert(ast_symbol == name_node->tag);
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
        } else {
            // a free variable
        }
    } break;

    case ast_lambda_function: {
        // establish lexical scope for formal parameters and recurse
        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            ast_node *param = node->lambda_function.parameters[i];
            if (ast_nil == param->tag) break;
            assert(ast_symbol == param->tag);
            str name               = param->symbol.name;
            str newvar             = next_variable_name(self);
            param->symbol.original = param->symbol.name;
            param->symbol.name     = newvar;
            str_map_set(lex, name, &newvar);
        }

        rename_variables(self, node->lambda_function.body, lex);

        map_destroy(lex);
        *lex = save;
    } break;

    case ast_let: {
        // establish lexical scope for formal parameters and recurse
        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->let.n_parameters; ++i) {
            ast_node *param = node->let.parameters[i];
            if (ast_nil == param->tag) break;
            assert(ast_symbol == param->tag);
            str name               = param->symbol.name;
            str newvar             = next_variable_name(self);
            param->symbol.original = param->symbol.name;
            param->symbol.name     = newvar;
            str_map_set(lex, name, &newvar);
        }

        rename_variables(self, node->let.body, lex);

        map_destroy(lex);
        *lex = save;

    } break;

    case ast_begin_end:
        for (u32 i = 0; i < node->begin_end.n_expressions; ++i)
            rename_variables(self, node->begin_end.expressions[i], lex);
        break;

    case ast_lambda_function_application:
        rename_variables(self, node->lambda_application.lambda, lex);
        break;

    case ast_named_function_application: {
        rename_variables(self, node->named_application.name, lex);

        for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
            ast_node *param = node->named_application.arguments[i];
            if (ast_nil == param->tag) break;
            rename_variables(self, param, lex);
        }
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
        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->let.n_parameters; ++i) {
            ast_node *param = node->let.parameters[i];
            if (ast_nil == param->tag) break;
            assert(ast_symbol == param->tag);
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
            assert(ast_assignment == ass->tag);
            ast_node *name_node = ass->assignment.name;
            assert(ast_symbol == name_node->tag);
            str name = name_node->symbol.name;

            str_map_set(lex, name, &name);
        }

        collect_free_variables(self, node->let_match_in.body, lex, fvs);

        map_destroy(lex);
        *lex = save;
    } break;

    case ast_symbol: {
        str *found;
        if ((found = str_map_get(*lex, node->symbol.name))) {
            ;
        } else {
            // a free variable
            array_set_insert(*fvs, node->symbol.name);
        }

        // if symbol has a type which carries fvs, we also collect those.
        // TODO so many indirections
        if (node->type_v2 && tl_mono == node->type_v2->tag && tl_arrow == node->type_v2->mono.tag) {
            forall(i, node->type_v2->mono.arrow.fvs)
              array_set_insert(*fvs, node->type_v2->mono.arrow.fvs.v[i]);
        }
    } break;

    case ast_lambda_function: {
        // establish lexical scope for formal parameters and recurse
        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            ast_node *param = node->lambda_function.parameters[i];
            if (ast_nil == param->tag) break;
            assert(ast_symbol == param->tag);
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
        for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
            ast_node *param = node->named_application.arguments[i];
            if (ast_nil == param->tag) break;
            collect_free_variables(self, param, lex, fvs);
        }
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

static int start_infer_global(tl_infer *self, ast_node *node) {
    infer_ctx *ctx = infer_ctx_create(self->transient);

    // after generics are created, we can process callsites for instantiation
    ctx->instantiate_applications = 1;

    // this infer operates on global environment
    ctx->local_env = self->env;
    ctx->subs      = self->subs;

    log(self, "-- start_infer_global --");
    int res = infer(self, ctx, node);

    // don't destroy self's environment
    ctx->subs      = null;
    ctx->local_env = null;
    infer_ctx_destroy(self->transient, &ctx);

    return res;
}

static u64 hash_name_and_type(str name, tl_monotype type) {
    return str_hash64_combine(tl_monotype_hash64(type), name);
}

static str instantiate_fun_and_infer(tl_infer *self, infer_ctx *ctx, ast_node *node, tl_monotype arrow) {
    str       name;
    ast_node *body = null;

    if (ast_let == node->tag) {
        name = node->let.name->symbol.name;
        body = node->let.body;
    } else if (ast_node_is_let_in_lambda(node)) {
        name = node->let_in.name->symbol.name;
        body = node->let_in.value->lambda_function.body;
    } else {
        fatal("logic error");
    }

    // de-duplicate instances. Note however that in many cases the arrow type will contain unique type
    // vars that have not been inferred yet.
    u64  hash     = hash_name_and_type(name, arrow);
    str *existing = map_get(self->instances, &hash, sizeof hash);
    if (existing) return *existing;

    // instantiate unique name
    str name_inst = next_instantiation(self, name);
    map_set(&self->instances, &hash, sizeof hash, &name_inst);

    // add to type environment
    tl_type_env_add(ctx->local_env, name_inst, tl_type_init_mono(arrow));

    if (infer(self, ctx, body)) fatal("error handling");

    return name_inst;
}

static str next_variable_name(tl_infer *self) {
    char buf[32];
    snprintf(buf, sizeof buf, "v%u", self->next_var_name++);
    return str_init(self->arena, buf);
}

static str next_instantiation(tl_infer *self, str name) {
    char buf[128];
    snprintf(buf, sizeof buf, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
    return str_init(self->arena, buf);
}

static void remove_known_variables(tl_infer *self, tl_type_env *env) {
    for (u32 i = 0; i < env->names.size;) {
        str name = env->names.v[i];
        if (tl_type_env_lookup(self->env, name)) {
            tl_type_env_erase(env, i);
        } else {
            ++i;
        }
    }

    tl_type_env_reindex(env);
}

static void remove_formal_parameters(tl_type_env *env, ast_node *node) {

    ast_node_sized params = ast_node_sized_from_ast_array(node);
    if (ast_let != node->tag && ast_lambda_function != node->tag) fatal("logic error");

    for (u32 i = 0; i < env->names.size;) {
        str name = env->names.v[i];

        forall(j, params) {
            if (ast_symbol != node->let.parameters[j]->tag) continue;
            str param = node->let.parameters[j]->symbol.name;
            if (str_eq(name, param)) {
                tl_type_env_erase(env, i);
                goto erased;
            }
        }

        ++i;

    erased:;
    }

    tl_type_env_reindex(env);
}

void do_remove_types(void *ctx, ast_node *node) {
    (void)ctx;
    node->type_v2 = null;
}

void remove_types(ast_node *node) {
    ast_node_dfs(null, node, do_remove_types);
}

void add_quantifier(tl_type_scheme *scheme, tl_monotype *type) {
    switch (type->tag) {
    case tl_nil:
    case tl_var: break;

    case tl_cons:
        //
        forall(i, type->cons.args) {
            add_quantifier(scheme, &type->cons.args.v[i]);
        }
        break;

    case tl_quant:
        //
        array_set_insert(scheme->quantifiers, type->quant);
        break;

    case tl_arrow: {
        add_quantifier(scheme, type->arrow.lhs);
        add_quantifier(scheme, type->arrow.rhs);
    } break;
    }
}

static void promote_to_type_scheme(allocator *alloc, tl_type_v2 *type) {
    if (tl_scheme == type->tag) return;

    tl_monotype mono = type->mono;

    *type            = tl_type_init_scheme((tl_type_scheme){.type = mono, .quantifiers = {.alloc = alloc}});
    add_quantifier(&type->scheme, &mono);
}

static void quantify_one_tv(tl_infer *self, tl_monotype *type, hashmap **seen) {
    assert(tl_var == type->tag);

    tl_monotype *found = map_get(*seen, type, sizeof *type);
    if (found) {
        *type = *found;
    } else {
        tl_monotype q = tl_type_context_new_quantifier(&self->context);
        map_set(seen, type, sizeof *type, &q);
        *type = q;
    }
}

static void quantify_one(tl_infer *self, tl_monotype *type, hashmap **seen) {
    switch (type->tag) {
    case tl_nil:
    case tl_quant: break;
    case tl_var:   quantify_one_tv(self, type, seen); break;

    case tl_cons:
        //
        forall(j, type->cons.args) {
            quantify_one(self, &type->cons.args.v[j], seen);
        }
        break;

    case tl_arrow:
        //
        quantify_one(self, type->arrow.lhs, seen);
        quantify_one(self, type->arrow.rhs, seen);
        break;
    }
}

static void quantify_env_types(tl_infer *self, tl_type_env *env) {
    hashmap *seen = map_create(self->transient, sizeof(tl_monotype), 8);

    forall(i, env->types) {
        tl_type_v2 *type = &env->types.v[i];
        if (tl_scheme == type->tag) continue;
        quantify_one(self, &type->mono, &seen);
        promote_to_type_scheme(self->arena, type);
    }

    map_destroy(&seen);
}

static tl_type_v2 make_arrow(tl_infer *self, ast_node_sized args, ast_node const *result) {
    assert(tl_mono == result->type_v2->tag);

    if (args.size == 0) {
        tl_monotype lhs   = tl_monotype_init_nil();
        tl_monotype rhs   = result->type_v2->mono;
        tl_monotype arrow = tl_monotype_alloc_arrow(self->arena, lhs, rhs);

        {
            str str = tl_monotype_to_string(self->transient, &arrow);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_type_init_mono(arrow);
    }

    else if (args.size == 1) {
        ensure_tv(self, &args.v[0]->type_v2);
        tl_monotype lhs   = args.v[0]->type_v2->mono;
        tl_monotype rhs   = result->type_v2->mono;
        tl_monotype arrow = tl_monotype_alloc_arrow(self->arena, lhs, rhs);
        {
            str str = tl_monotype_to_string(self->transient, &arrow);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_type_init_mono(arrow);
    }

    else {
        ensure_tv(self, &args.v[0]->type_v2);
        tl_monotype lhs = args.v[0]->type_v2->mono;
        tl_type_v2 rhs = make_arrow(self, (ast_node_sized){.size = args.size - 1, .v = &args.v[1]}, result);
        tl_monotype arrow = tl_monotype_alloc_arrow(self->arena, lhs, rhs.mono);
        {
            str str = tl_monotype_to_string(self->transient, &arrow);
            log(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_type_init_mono(arrow);
    }
}

static tl_monotype *arrow_rightmost(tl_monotype *arrow) {
    if (tl_arrow != arrow->tag) fatal("logic error");

    if (tl_arrow == arrow->arrow.rhs->tag) return arrow_rightmost(arrow->arrow.rhs);
    return arrow->arrow.rhs;
}

static void replace_quant(hashmap *map, tl_monotype *mono) {
    switch (mono->tag) {
    case tl_nil:
    case tl_var: break;

    case tl_cons:
        //
        forall(i, mono->cons.args) replace_quant(map, &mono->cons.args.v[i]);
        break;

    case tl_quant: {
        tl_type_variable *tv = map_get(map, &mono->quant, sizeof mono->quant);
        if (tv) {
            mono->tag = tl_var;
            mono->var = *tv;
        } else {
            fatal("quantifier not found");
        }
    } break;

    case tl_arrow:
        replace_quant(map, mono->arrow.lhs);
        replace_quant(map, mono->arrow.rhs);
        break;
    }
}

static tl_type_v2 instantiate(tl_infer *self, tl_type_v2 generic) {

    assert(tl_scheme == generic.tag);

    tl_type_v2     src = tl_type_v2_clone(self->arena, generic);
    tl_type_scheme s   = src.scheme;

    hashmap       *map = map_create(self->transient, sizeof(tl_type_variable), s.quantifiers.size);
    forall(i, s.quantifiers) {
        tl_type_variable tv = tl_type_context_new_variable(&self->context);
        map_set(&map, &s.quantifiers.v[i], sizeof s.quantifiers.v[0], &tv);
    }

    tl_monotype out = s.type;

    replace_quant(map, &out);
    map_destroy(&map);

    return tl_type_init_mono(out);
}

static void add_generic(tl_infer *self, ast_node *node) {
    if (!node) return;

    ast_node *infer_target = null;
    str       name;
    str       orig_name;
    if (ast_let == node->tag) {
        name         = node->let.name->symbol.name;
        orig_name    = node->let.name->symbol.original;
        infer_target = node;
    } else if (ast_node_is_let_in_lambda(node)) {
        name         = node->let_in.name->symbol.name;
        orig_name    = node->let_in.name->symbol.original;
        infer_target = node->let_in.value;
    } else {
        fatal("logic error");
    }

    // ignore subsequent calls
    if (tl_type_env_lookup(self->env, name)) return;

    log(self, "-- add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    // FIXME: is there an annotated type??

    // run local inference: this function is not yet in the global environment.
    infer_ctx *ctx  = infer_ctx_create(self->transient);
    ctx->add_to_env = 1;
    if (infer(self, ctx, infer_target)) fatal("error handling");

    if (ast_lambda_function == infer_target->tag) {
        // since the infer target is unnamed, the lambda function could not add itself to the
        // environment. We must do so here before the quantified variable analysis.
        tl_type_env_add(ctx->local_env, name, ctx->lambda_type);
    }

    log(self, "-- local env --");
    log_env(self, ctx->local_env);

    log(self, "-- local subs --");
    log_subs(self, ctx->subs);

    // now determine which names in the local environment are not free (i.e. they exist in the global
    // environment or as formal parameters). Whatever is left must be quantified. This assumes the
    // function under analysis has been added with an arrow type to the local environment.
    remove_known_variables(self, ctx->local_env);
    remove_formal_parameters(ctx->local_env, infer_target);

    log(self, "-- local env after removal --");
    log_env(self, ctx->local_env);

    quantify_env_types(self, ctx->local_env);

    log(self, "-- local env after quantification --");
    log_env(self, ctx->local_env);

    // collect free variables from infer target and add to the generic's arrow type
    str_array fvs = {.alloc = self->arena};
    {
        hashmap *lex = map_create(self->transient, sizeof(str), 16);
        collect_free_variables(self, infer_target, &lex, &fvs);
        map_destroy(&lex);

        array_shrink(fvs);
        log(self, "-- free variables: %u --", fvs.size);
        forall(i, fvs) {
            log(self, "%.*s", str_ilen(fvs.v[i]), str_buf(&fvs.v[i]));
        }
    }

    // add all function types to GLOBAL environment
    tl_type_v2 *arrow = tl_type_env_lookup(ctx->local_env, name);
    assert(arrow && tl_scheme == arrow->tag && tl_arrow == arrow->scheme.type.tag);
    arrow->scheme.type.arrow.fvs = fvs;
    tl_type_v2_arrow_sort_fvs(&arrow->scheme.type.arrow);
    tl_type_env_add(self->env, name, *arrow);

    // remove type information from function tree
    remove_types(infer_target);

    log(self, "-- global env --");
    log_env(self, self->env);

    log(self, "-- done add_generic  --");

    infer_ctx_destroy(self->transient, &ctx);
}

int check_missing_free_variables(tl_infer *self) {
    int error = 0;

    forall(i, self->env->names) {
        str               name = self->env->names.v[i];
        tl_type_v2 const *type = &self->env->types.v[i];

        str_sized         fvs  = tl_type_v2_free_variables(type);
        forall(j, fvs) {
            if (!str_map_contains(self->env->index, fvs.v[j])) {

                ast_node *node = toplevel_get(self, name);
                array_push(self->errors,
                           ((tl_infer_error){
                             .tag = tl_err_free_variable_not_found, .node = node, .message = fvs.v[j]}));
                ++error;
            }
        }
    }
    return error;
}

int tl_infer_run(tl_infer *self, ast_node_sized nodes) {
    log(self, "-- start inference --");

    self->toplevels = load_toplevel(self, self->arena, nodes, &self->errors);

    if (self->errors.size) {
        return 1;
    }

    // rename lexical variables
    {
        hashmap *lex = map_create(self->transient, sizeof(str), 16);
        forall(i, nodes) {
            rename_variables(self, nodes.v[i], &lex);
        }
        map_destroy(&lex);
    }

    // now go through the toplevel let nodes and create generic functions
    {
        hashmap_iterator iter = {0};
        ast_node        *node = null;
        while ((node = toplevel_iter(self, &iter))) add_generic(self, node);
    }

    ast_node **found_main = str_map_get(self->toplevels, S("main"));
    if (!found_main) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_no_main_function}));
        return 1;
    }
    ast_node *main = *found_main;

    if (start_infer_global(self, main)) return 1;

    log(self, "-- toplevels");
    log_toplevels(self);
    log(self, "-- env");
    log_env(self, self->env);
    log(self, "-- subs");
    log_subs(self, self->subs);

    // apply subs to global environment
    tl_type_env_subs_apply(self->env, self->subs);

    log(self, "-- final env --");
    log_env(self, self->env);

    if (check_missing_free_variables(self)) return 1;

    // TODO self->subs is no longer useful
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

static void log_env(tl_infer const *self, tl_type_env const *env) {
    forall(i, env->names) {
        str const        *name     = &env->names.v[i];
        tl_type_v2 const *type     = &env->types.v[i];
        str               type_str = tl_type_v2_to_string(self->transient, type);
        log(self, "%.*s : %.*s", str_ilen(*name), str_buf(name), str_ilen(type_str), str_buf(&type_str));
        str_deinit(self->transient, &type_str);
    }
}

static void log_subs(tl_infer const *self, tl_type_subs const *subs) {
    hashmap_iterator iter = {0};
    while (map_iter(subs->map, &iter)) {
        tl_type_variable const *tv       = iter.key_ptr;
        tl_monotype const      *mono     = iter.data;
        str                     tv_str   = tl_type_variable_to_string(self->transient, tv);
        str                     mono_str = tl_monotype_to_string(self->transient, mono);

        log(self, "%.*s => %.*s", str_ilen(tv_str), str_buf(&tv_str), str_ilen(mono_str),
            str_buf(&mono_str));
        str_deinit(self->transient, &mono_str);
        str_deinit(self->transient, &tv_str);
    }
}

static str v2_ast_node_to_string(allocator *alloc, ast_node const *node) {
    char buf[64];

    switch (node->tag) {
    case ast_f64:    snprintf(buf, sizeof buf, "%f", node->f64.val); return str_init(alloc, buf);
    case ast_i64:    snprintf(buf, sizeof buf, "%" PRIi64, node->i64.val); return str_init(alloc, buf);
    case ast_u64:    snprintf(buf, sizeof buf, "%" PRIu64, node->u64.val); return str_init(alloc, buf);
    case ast_string: return str_cat_3(alloc, S("\""), node->symbol.name, S("\""));
    case ast_bool:   return node->bool_.val ? str_copy(alloc, S("true")) : str_copy(alloc, S("false"));
    case ast_nil:    return S("()");
    case ast_any:    return S("any");

    case ast_symbol: {
        str out = node->symbol.name;
        if (node->symbol.annotation_type_v2) {
            out = str_cat_4(alloc, out, S(" (v2: "),
                            tl_type_v2_to_string(alloc, node->symbol.annotation_type_v2), S(")"));
        } else if (node->symbol.annotation) {
            out =
              str_cat_4(alloc, out, S(" ("), v2_ast_node_to_string(alloc, node->symbol.annotation), S(")"));
        }
        if (node->type_v2)
            out = str_cat_3(alloc, out, S(" : "), tl_type_v2_to_string(alloc, node->type_v2));
        return out;
    }

    case ast_let: {
        str out = str_copy(alloc, S("let "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let.name));
        if (node->type_v2)
            out = str_cat_3(alloc, out, S(" : "), tl_type_v2_to_string(alloc, node->type_v2));
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
static void toplevel_add(tl_infer *self, str name, ast_node *node) {
    str_map_set(&self->toplevels, name, &node);
}

static ast_node *toplevel_get(tl_infer *self, str name) {
    ast_node **found = str_map_get(self->toplevels, name);
    return found ? *found : null;
}

static ast_node *toplevel_iter(tl_infer *self, hashmap_iterator *iter) {
    if (map_iter(self->toplevels, iter)) {
        return *(ast_node **)iter->data;
    }
    return null;
}
