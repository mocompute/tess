#include "v2_infer.h"
#include "alloc.h"
#include "ast_tags.h"
#include "dbg.h"
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
    tl_infer_error_array errors;

    u32                  next_var_name;

    int                  verbose;
    int                  indent_level;
};

//

static str  v2_ast_node_to_string(allocator *, ast_node const *);
static void assign_expression_types(tl_infer *self, ast_node *node);

static void log(tl_infer const *self, char const *restrict fmt, ...);
static void log_toplevels(tl_infer const *);
static void log_env(tl_infer const *);
static void log_subs(tl_infer const *);

//

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self      = new (alloc, tl_infer);

    self->transient     = arena_create(alloc, 4096);
    self->arena         = arena_create(alloc, 16 * 1024);

    self->toplevels     = null;
    self->context       = tl_type_context_empty();
    self->env           = tl_type_env_create(alloc);
    self->subs          = tl_type_subs_create(alloc);

    self->errors        = (tl_infer_error_array){.alloc = self->arena};

    self->next_var_name = 1;

    self->verbose       = 0;
    self->indent_level  = 0;

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    tl_type_subs_destroy(alloc, &(*p)->subs);
    tl_type_env_destroy(alloc, &(*p)->env);
    map_destroy(&(*p)->toplevels);

    arena_destroy(alloc, &(*p)->transient);
    arena_destroy(alloc, &(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose = verbose;
}

static hashmap *load_toplevel(tl_infer *self, allocator *alloc, ast_node_sized nodes,
                              tl_infer_error_array *out_errors) {
    hashmap             *tops   = map_create(alloc, sizeof(ast_node *), 1024);
    tl_infer_error_array errors = {.alloc = alloc};

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        // dbg("processing: %s\n", ast_node_to_string(alloc, node));
        if (ast_symbol == node->tag) {
            str        name_str = node->symbol.name;
            ast_node **p        = str_map_get(tops, name_str);
            if (p) {
                // merge annotation if existing node is a let node; otherwise error
                if (ast_let != (*p)->tag) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                if (node->symbol.annotation) (*p)->let.name->symbol.annotation = node->symbol.annotation;
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&tops, name_str, &node);
                }
            }
        }

        else if (ast_let == node->tag) {
            str        name_str = ast_node_str(node->let.name);
            ast_node **p        = str_map_get(tops, name_str);

            // assign expresion type
            assign_expression_types(self, node);

            if (p) {
                // merge type if the existing node is a symbol; otherwise error
                if (ast_symbol != (*p)->tag) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                // ignore prior type annotation if the current symbol is annotated: later declaration
                // overrides
                if (node->let.name->symbol.annotation) continue;

                // apply annotation
                node->let.name->symbol.annotation = (*p)->symbol.annotation;

                // replace prior symbol entry with let node
                *p = node;
            } else {
                str_map_set(&tops, name_str, &node);
            }
        }

        else if (ast_user_type_definition == node->tag) {
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

static int type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_error, .node = node}));
    return 1;
}

static int is_type_variable(tl_type_v2 const *self) {
    return tl_mono == self->tag && tl_var == self->mono.tag;
}

static int can_apply_constraint(tl_monotype existing, tl_monotype apply) {
    switch (existing.tag) {
    case tl_nil:
    case tl_cons:
        if (!tl_monotype_eq(apply, existing)) return 0; // type error
        return 1;

    case tl_var: return 1;

    case tl_arrow:
        return tl_arrow == apply.tag && can_apply_constraint(*existing.arrow.left, *apply.arrow.left) &&
               can_apply_constraint(*existing.arrow.right, *apply.arrow.right);
    }
}

static int unify(tl_infer *self, tl_type_variable tv, tl_monotype mono) {
    switch (mono.tag) {

    case tl_nil:  break;
    case tl_cons: {
        // attempt to match type constructor arguments against existing type vars
        forall(i, mono.cons.args) {
            if (tl_var == mono.cons.args.v[i].tag) {
                tl_monotype *found = tl_type_subs_get(self->subs, mono.cons.args.v[i].var);
                if (found) mono.cons.args.v[i] = *found;
            }
        }
    } break;

    case tl_var: {
        // if mono is a tv, unify it with existing tv, if any
        tl_monotype *mono_match = tl_type_subs_get(self->subs, mono.var);
        if (mono_match) {
            tl_type_subs_add(self->subs, tv, *mono_match);
            return 0;
        }
    } break;

    case tl_arrow: {
        // if either arm is an existing tv, use it instead
        tl_monotype *left_match  = null;
        tl_monotype *right_match = null;

        if (tl_var == mono.arrow.left->tag) {
            left_match = tl_type_subs_get(self->subs, mono.arrow.left->var);
        }
        if (tl_var == mono.arrow.right->tag) {
            right_match = tl_type_subs_get(self->subs, mono.arrow.right->var);
        }

        if (left_match) mono.arrow.left = left_match;
        if (right_match) mono.arrow.right = right_match;

    } break;
    }

    tl_type_subs_add(self->subs, tv, mono);

    return 0;
}

static nodiscard int constrain(tl_infer *self, tl_type_v2 const *left, tl_type_v2 const *right,
                               ast_node const *node) {
    tl_type_variable tv;
    tl_monotype      mono;

    if (tl_mono != left->tag || tl_mono != right->tag) fatal("cannot constrain type scheme");

    if (tl_monotype_occurs(left->mono, right->mono)) return 0;

    if (is_type_variable(left)) {
        tv   = left->mono.var;
        mono = right->mono;
    } else if (is_type_variable(right)) {
        tv   = right->mono.var;
        mono = left->mono;
    } else {
        return tl_monotype_eq(left->mono, right->mono) ? 0 : 1;
    }

    tl_monotype *exist = tl_type_subs_get(self->subs, tv);
    if (exist) {
        if (!can_apply_constraint(*exist, mono)) {
            str exist_str = tl_monotype_to_string(self->transient, exist);
            str new_str   = tl_monotype_to_string(self->transient, &mono);
            log(self, "new constraint %.*s is not compatible with existing constraint %.*s",
                str_ilen(new_str), str_buf(&new_str), str_ilen(exist_str), str_buf(&exist_str));
            return type_error(self, node);
        } else if (tl_var == exist->tag) {
            tl_type_v2 exist_ty = tl_type_init_mono(*exist);
            tl_type_v2 mono_ty  = tl_type_init_mono(mono);
            return constrain(self, &exist_ty, &mono_ty, node);
        } else {
            fatal("what to do with new constraint on same tv...");
        }
    }
    return unify(self, tv, mono);
}

static nodiscard int infer(tl_infer *self, ast_node *node) {
    // update subs by collecting and applying constraints found recursively starting at node.

    if (null == node) return 0;

    switch (node->tag) {
    case ast_nil:
    case ast_any:                break;
    case ast_address_of:         fatal("FIXME: pointer types");

    case ast_arrow:
    case ast_assignment:
    case ast_bool:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:                break;

    case ast_string:             {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("String"));
        return constrain(self, node->type_v2, ty, node);
    } break;

    case ast_f64: {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("Float"));
        return constrain(self, node->type_v2, ty, node);
    } break;

    case ast_i64: {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("Int"));
        return constrain(self, node->type_v2, ty, node);
    } break;

    case ast_u64: {
        tl_type_v2 *ty = tl_type_env_lookup(self->env, S("Int")); // FIXME unsigned int
        return constrain(self, node->type_v2, ty, node);
    } break;

    case ast_let_in:
        if (infer(self, node->let_in.value)) return 1;
        if (infer(self, node->let_in.body)) return 1;
        if (constrain(self, node->type_v2, node->let_in.body->type_v2, node)) return 1;

        if (constrain(self, tl_type_env_lookup(self->env, node->let_in.name->symbol.name),
                      node->let_in.value->type_v2, node))
            return 1;
        break;

    case ast_let:

        if (str_eq(node->let.name->symbol.name, S("main"))) {
            // FIXME just force () -> Int
            tl_monotype left     = {0}; // nil
            tl_monotype right    = tl_type_env_lookup(self->env, S("Int"))->mono;
            tl_monotype arrow    = tl_monotype_alloc_arrow(self->arena, left, right);
            tl_type_v2  arrow_ty = tl_type_init_mono(arrow);
            tl_type_v2  right_ty = tl_type_init_mono(right);

            if (constrain(self, node->type_v2, &arrow_ty, node)) return 1;
            if (infer(self, node->let.body)) return 1;
            if (constrain(self, node->let.body->type_v2, &right_ty, node)) return 1;
        } else {
            return infer(self, node->let.body);
            // FIXME do arrow
        }
        break;

    case ast_symbol:
        if (constrain(self, tl_type_env_lookup(self->env, node->symbol.name), node->type_v2, node))
            return 1;
        break;

    case ast_if_then_else:
    case ast_let_match_in:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_lambda_function:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_tuple:
    case ast_user_type:                   break;
    }
    return 0;
}

static str next_variable_name(tl_infer *self) {
    char buf[32];
    snprintf(buf, sizeof buf, "v%u", self->next_var_name++);
    return str_init(self->arena, buf);
}

static void assign_new_type_variable(tl_infer *self, str name) {
    tl_type_v2 type = tl_type_init_mono(tl_monotype_init_tv(tl_type_context_new_variable(&self->context)));
    tl_type_env_add(self->env, name, type);
}

static void assign_new_expression_variable(tl_infer *self, ast_node *node) {
    node->type_v2  = new (self->arena, tl_type_v2);
    *node->type_v2 = tl_type_init_mono(tl_monotype_init_tv(tl_type_context_new_variable(&self->context)));
}

static void rename_variables(tl_infer *self, ast_node *node, hashmap **lex) {
    // transform variables recursively in node so that every occurrence is unique, respecting lexical
    // scope created by lambda functions, let functions, and let in expressions so that variables have
    // the same name when they refer to the same bound value.

    if (null == node) return;

    switch (node->tag) {

    case ast_assignment:
        rename_variables(self, node->assignment.name, lex);
        rename_variables(self, node->assignment.value, lex);
        break;

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
        str name                           = node->let_in.name->symbol.name;
        str newvar                         = next_variable_name(self);
        node->let_in.name->symbol.original = node->let_in.name->symbol.name;
        node->let_in.name->symbol.name     = newvar;

        // establish lexical scope of the let-in binding and recurse
        hashmap *save = map_copy(*lex);
        str_map_set(lex, name, &newvar);
        assign_new_type_variable(self, newvar);
        rename_variables(self, node->let_in.body, lex);

        // restore prior scope
        map_destroy(lex);
        *lex = save;
    } break;

    case ast_let_match_in: {
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
            assign_new_type_variable(self, newvar);
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
            fatal("symbol found outside lexical scope");
        }
    } break;

    case ast_lambda_function: {
        // establish lexical scope for formal parameters and recurse
        hashmap *save = map_copy(*lex);

        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            ast_node *param = node->lambda_function.parameters[i];
            assert(ast_symbol == param->tag);
            str name               = param->symbol.name;
            str newvar             = next_variable_name(self);
            param->symbol.original = param->symbol.name;
            param->symbol.name     = newvar;
            str_map_set(lex, name, &newvar);
            assign_new_type_variable(self, newvar);
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
            assert(ast_symbol == param->tag);
            str name               = param->symbol.name;
            str newvar             = next_variable_name(self);
            param->symbol.original = param->symbol.name;
            param->symbol.name     = newvar;
            str_map_set(lex, name, &newvar);
            assign_new_type_variable(self, newvar);
        }

        rename_variables(self, node->let.body, lex);

        map_destroy(lex);
        *lex = save;

    } break;

    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_labelled_tuple:
    case ast_tuple:
    case ast_lambda_function_application:
    case ast_named_function_application:
        // FIXME

        fatal("fixme");
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

void assign_expression_types(tl_infer *self, ast_node *node) {
    if (null == node) return;

    // every node gets a type, and some nodes need to recursively dispatch to their components

    assign_new_expression_variable(self, node);

    switch (node->tag) {
    case ast_address_of:  assign_expression_types(self, node->address_of.target); break;
    case ast_assignment:  assign_expression_types(self, node->assignment.value); break;
    case ast_dereference: assign_expression_types(self, node->dereference.target); break;
    case ast_dereference_assign:
        assign_expression_types(self, node->dereference_assign.target);
        assign_expression_types(self, node->dereference_assign.value);
        break;

    case ast_if_then_else:
        assign_expression_types(self, node->if_then_else.condition);
        assign_expression_types(self, node->if_then_else.yes);
        assign_expression_types(self, node->if_then_else.no);
        break;

    case ast_let_in:
        assign_expression_types(self, node->let_in.name);
        assign_expression_types(self, node->let_in.value);
        assign_expression_types(self, node->let_in.body);
        break;

    case ast_let_match_in:
        assign_expression_types(self, node->let_match_in.body);
        for (u32 i = 0; i < node->let_match_in.lt->labelled_tuple.n_assignments; ++i) {
            assign_expression_types(self, node->let_match_in.lt->labelled_tuple.assignments[i]);
        }
        break;

    case ast_user_type_set: assign_expression_types(self, node->user_type_set.value); break;

    case ast_begin_end:
        for (u32 i = 0; i < node->begin_end.n_expressions; ++i) {
            assign_expression_types(self, node->begin_end.expressions[i]);
        }
        break;

    case ast_labelled_tuple:
        for (u32 i = 0; i < node->labelled_tuple.n_assignments; ++i) {
            assign_expression_types(self, node->labelled_tuple.assignments[i]);
        }
        break;

    case ast_lambda_function: assign_expression_types(self, node->lambda_function.body); break;
    case ast_lambda_function_application:
        assign_expression_types(self, node->lambda_application.lambda);
        break;

    case ast_let:                        assign_expression_types(self, node->let.body); break;

    case ast_named_function_application: assign_expression_types(self, node->named_application.name); break;
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_string:
    case ast_symbol:
    case ast_u64:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_arrow:
    case ast_nil:
    case ast_any:
    case ast_tuple:
    case ast_user_type:                  break;
    }
}

int tl_infer_run(tl_infer *self, ast_node_sized nodes) {

    log(self, "-- start inference --");

    self->toplevels = load_toplevel(self, self->arena, nodes, &self->errors);

    if (self->errors.size) {
        return 1;
    }

    ast_node **found_main = str_map_get(self->toplevels, S("main"));
    if (!found_main) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_no_main_function}));
        return 1;
    }
    ast_node *main = *found_main;

    // rename variables for lexical scope and assign type variables
    {
        hashmap *lex = map_create(self->arena, sizeof(str), 16);
        rename_variables(self, main, &lex);
        map_destroy(&lex);
    }

    assign_expression_types(self, main);

    if (infer(self, main)) return 1;

    log(self, "toplevels");
    log_toplevels(self);
    log(self, "env");
    log_env(self);
    log(self, "subs");
    log_subs(self);

    return 0;
}

void tl_infer_report_errors(tl_infer *self) {
    if (self->errors.size) {
        forall(i, self->errors) {
            tl_infer_error *err  = &self->errors.v[i];
            ast_node const *node = err->node;

            if (node)
                fprintf(stderr, "%s:%u: %s: %s\n", node->file, node->line, tl_error_tag_to_string(err->tag),
                        ast_node_to_string_for_error(self->transient, node));

            else fprintf(stderr, "error: %s\n", tl_error_tag_to_string(err->tag));
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
    forall(i, self->env->names) {
        str const        *name     = &self->env->names.v[i];
        tl_type_v2 const *type     = &self->env->types.v[i];
        str               type_str = tl_type_v2_to_string(self->transient, type);
        log(self, "%.*s : %.*s", str_ilen(*name), str_buf(name), str_ilen(type_str), str_buf(&type_str));
        str_deinit(self->transient, &type_str);
    }
}

static void log_subs(tl_infer const *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->subs->map, &iter)) {
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
    switch (node->tag) {
    case ast_symbol: {
        str out = node->symbol.name;
        if (node->type_v2)
            out = str_cat_3(alloc, out, S(" : "), tl_type_v2_to_string(alloc, node->type_v2));
        return out;
    }

    case ast_let: {
        str out = node->let.name->symbol.name;
        if (node->type_v2)
            out = str_cat_3(alloc, out, S(" : "), tl_type_v2_to_string(alloc, node->type_v2));
        return out;
    }

    case ast_nil:                         return S("nil");
    case ast_any:                         return S("any");
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
    case ast_named_function_application:
    case ast_tuple:
    case ast_user_type:                   return str_copy(alloc, S("FIXME: not yet implemented"));
    }
}

//
