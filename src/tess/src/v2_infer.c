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

    hashmap             *toplevels;
    tl_infer_error_array errors;

    u32                  next_var_name;

    int                  verbose;
    int                  indent_level;
};

//

static str  v2_ast_node_to_string(allocator *, ast_node const *);

static void log(tl_infer const *self, char const *restrict fmt, ...);
static void log_toplevels(tl_infer const *);

//

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self      = new (alloc, tl_infer);

    self->transient     = arena_create(alloc, 4096);
    self->arena         = arena_create(alloc, 16 * 1024);

    self->toplevels     = null;
    self->context       = tl_type_context_empty();
    self->env           = tl_type_env_create(alloc);

    self->errors        = (tl_infer_error_array){.alloc = self->arena};

    self->next_var_name = 1;

    self->verbose       = 0;
    self->indent_level  = 0;

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

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

static hashmap *load_toplevel(allocator *alloc, ast_node_sized nodes, tl_infer_error_array *out_errors) {
    hashmap             *tops   = map_create(alloc, sizeof(ast_node *), 1024);
    tl_infer_error_array errors = {.alloc = alloc};

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

static void infer_W(tl_infer *self, ast_node *node, tl_type_subs *out_subs, tl_type_v2 *out_type) {
    tl_type_subs subs = {.froms = {.alloc = self->arena}, .tos = {.alloc = self->arena}};
    tl_type_v2   type = {0};

    switch (node->tag) {
    case ast_i64: type = *tl_type_env_lookup(self->env, S("Int")); break;

    case ast_let: {
        if (str_eq(node->let.name->symbol.name, S("main"))) {
            // FIXME just force () -> Int
            tl_monotype left  = {0}; // nil
            tl_monotype right = tl_type_env_lookup(self->env, S("Int"))->mono;
            tl_monotype arrow = tl_monotype_alloc_arrow(self->arena, left, right);
            type              = tl_type_init_mono(arrow);
        } else {
            // FIXME
        }
    } break;

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
    case ast_if_then_else:
    case ast_let_in:
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
    case ast_lambda_function:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_tuple:
    case ast_user_type:                   fatal("not implemented");
    }

    *out_type = type;
    *out_subs = subs;
}

static str next_variable_name(tl_infer *self) {
    char buf[32];
    snprintf(buf, sizeof buf, "v%u", self->next_var_name++);
    return str_init(self->arena, buf);
}

static void rename_symbol(tl_infer *self, ast_node *node, hashmap *lex) {
    assert(ast_symbol == node->tag);
    str *found;
    if ((found = str_map_get(lex, node->symbol.name))) {
        node->symbol.original = node->symbol.name;
        node->symbol.name     = *found;
    } else {
        str newvar            = next_variable_name(self);
        node->symbol.original = node->symbol.name;
        node->symbol.name     = newvar;
    }
}

static void rename_variables(tl_infer *self, ast_node *node, hashmap **lex) {
    // transform variables recursively in node so that every
    // occurrence is unique, respecting lexical scope created by
    // lambda functions, let functions, and let in expressions to
    // variables have the same name when they refer to the same bound
    // value.

    if (null == node) return;

    if (ast_let_in == node->tag) {
        str name                           = node->let_in.name->symbol.name;
        str newvar                         = next_variable_name(self);
        node->let_in.name->symbol.original = node->symbol.name;
        node->let_in.name->symbol.name     = newvar;

        // establish lexical scope of the let-in binding and recurse
        hashmap *save = map_copy(*lex);
        str_map_set(lex, name, &newvar);
        rename_variables(self, node->let_in.body, lex);

        // restore prior scope
        map_destroy(lex);
        *lex = save;
    }

    else if (ast_let_match_in == node->tag) {
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
    }

    else if (ast_let == node->tag) {

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
        }

        rename_variables(self, node->let.body, lex);

        map_destroy(lex);
        *lex = save;
    }

    else if (ast_lambda_function == node->tag) {
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
        }

        rename_variables(self, node->lambda_function.body, lex);

        map_destroy(lex);
        *lex = save;
    }
}

int tl_infer_run(tl_infer *self, ast_node_sized nodes) {

    log(self, "-- start inference --");

    self->toplevels = load_toplevel(self->arena, nodes, &self->errors);

    if (self->errors.size) {
        return 1;
    }

    ast_node **found_main = str_map_get(self->toplevels, S("main"));
    if (!found_main) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_no_main_function}));
        return 1;
    }
    ast_node *main = *found_main;

    // rename variables for lexical scope
    {
        hashmap *lex = map_create(self->arena, sizeof(str), 16);
        rename_variables(self, main, &lex);
        map_destroy(&lex);
    }

    tl_type_v2  *type = new (self->arena, tl_type_v2);
    tl_type_subs subs;
    infer_W(self, main, &subs, type);
    tl_type_subs_apply(&subs, &(tl_type_v2_array){.size = 1, .v = type});

    main->type_v2 = type;

    log_toplevels(self);

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
