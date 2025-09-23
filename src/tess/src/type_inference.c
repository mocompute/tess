#include "type_inference.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "error.h"
#include "hash.h"
#include "hashmap.h"
#include "string_t.h"
#include "type.h"
#include "type_registry.h"
#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define TYPE_ARENA_SIZE      32 * 1024
#define TRANSIENT_ARENA_SIZE 16 * 1024

typedef struct constraint {
    tl_type *left;
    tl_type *right;
    int      line;
} constraint;

typedef struct {
    array_header;
    constraint *v;
} constraint_array;

typedef struct {
    array_sized;
    constraint *v;
} constraint_sized;

typedef struct {
    enum tl_error_tag tag;
    ast_node const   *node;
} ti_error;

typedef struct {
    array_header;
    ti_error *v;
} ti_error_array;

struct ti_inferer {
    allocator       *type_arena;
    allocator       *transient;

    ast_node_array  *nodes;
    type_registry   *type_registry;

    constraint_array constraints;
    constraint_array substitutions;

    hashmap         *functions;    // char const* -> function_record (node: symbol, let, lambda)
    hashmap         *requirements; // u64 hash -> function_record (node: null or lambda_function)
    ast_node_array   specials;
    ti_error_array   errors;

    ast_node_sized   out_program; // only defined after successful run

    u32              next_var;         // for rename_variables
    u32              next_type_var;    // for assign_type_variables
    u32              next_specialized; // for specialized function names

    // flags which do not affect operation
    int verbose;
    int indent_level;
};

// -- ti_inferer --

typedef struct {
    ti_inferer *ti;
    allocator  *alloc;
    hashmap    *map;
} rename_variables_ctx;

typedef struct {
    ti_inferer *ti;
} collect_constraints_ctx;

typedef struct {
    ti_inferer     *self;
    ast_node_array *added;
    hashmap        *seen;
} generate_tuple_function_ctx;

typedef struct {
    string_t name; // or replacement, with rename_variables
    tl_type *type;
} lexical_info;

static size_t          ti_apply_substitutions_to_ast(ti_inferer *, constraint_sized);
static void            ti_assign_type_variables(ti_inferer *);
static void            ti_assign_arrow_types(ti_inferer *);

static int             ti_check_callsites(ti_inferer *);

static void            ti_collect_specialization_requirements(ti_inferer *);
static void            ti_create_specials(ti_inferer *);
static void            ti_patch_special_applications(ti_inferer *);
static void            ti_collect_functions_to_emit(ti_inferer *);
static void            ti_fixup_free_variables(ti_inferer *self);

static void            ti_collect_and_solve(ti_inferer *, int);
static void            ti_collect_constraints(ti_inferer *);
static void            ti_generate_tuple_functions(ti_inferer *);
static void            ti_generate_user_type_functions(ti_inferer *);
static void            ti_rename_variables(ti_inferer *);
static void            ti_run_solver(ti_inferer *);
tl_free_variable_sized ti_free_variables_in_continue(allocator *, ast_node const *, hashmap **);

void                   rename_one_variables(void *, ast_node *, hashmap **);
static tl_type        *make_args_type(allocator *, ast_node *[], u16);
static tl_type        *make_labelled_args_type(allocator *, ast_node *[], char const *[], u16);
static tl_type        *get_prim(ti_inferer *, tl_type_tag);
static void            next_variable_name(ti_inferer *, string_t *);
static void            rename_if_match(allocator *, string_t *, hashmap *, string_t *);

static size_t          apply_one_substitution(tl_type **, tl_type *, tl_type *);
static void            assign_type_variables(void *, ast_node *);
static void            generate_tuple_function(ti_inferer *, ast_node *, ast_node_array *, hashmap **);
static int             substitute_constraints(constraint *, constraint *, constraint const);
static u32             unify_one(ti_inferer *, constraint);

static tl_type        *make_arrow(allocator *, ast_node *[], u16, char const *[], tl_type *);
static ast_node       *make_tuple_constructor_function(ti_inferer *, u64, ast_node *);
static ast_node       *make_type_constructor_function(ti_inferer *, char const *, tl_type *);
static tl_type        *make_typevar(ti_inferer *);
static u32             make_typevar_val(void *);
static constraint      make_constraint(tl_type *, tl_type *);

static void            dbg_ast_nodes(ti_inferer *);
static void            dbg_constraint(constraint const *);
static void            log_function_records(ti_inferer *);
static void            log_specialization_records(ti_inferer *);
static void            log_specials(ti_inferer *self);
static void log(ti_inferer const *, char const *restrict, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

ti_inferer *ti_inferer_create(allocator *alloc, ast_node_array *nodes, type_registry *type_registry) {
    ti_inferer *self       = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena       = arena_create(alloc, TYPE_ARENA_SIZE);
    self->transient        = arena_create(alloc, TRANSIENT_ARENA_SIZE);

    self->nodes            = nodes;
    self->type_registry    = type_registry;

    self->constraints      = (constraint_array){.alloc = self->type_arena};
    self->substitutions    = (constraint_array){.alloc = self->type_arena};
    self->functions        = map_create(self->type_arena, sizeof(ti_function_record));
    self->requirements     = map_create(self->type_arena, sizeof(ti_function_record));
    self->specials         = (ast_node_array){.alloc = self->type_arena};
    self->errors           = (ti_error_array){.alloc = self->type_arena};
    self->out_program.v    = null;
    self->out_program.size = 0;

    self->next_var         = 1; // 0 is not valid
    self->next_type_var    = 1;
    self->next_specialized = 1;

    self->verbose          = 0;
    self->indent_level     = 0;

    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **pself) {
    ti_inferer *self = *pself;

    map_destroy(&self->requirements);
    map_destroy(&self->functions);
    arena_destroy(alloc, &self->transient);
    arena_destroy(alloc, &self->type_arena);
    alloc_free(alloc, *pself);
    *pself = null;
}

// -- operation --

void ti_inferer_set_verbose(ti_inferer *self, int val) {
    self->verbose = val;
}

static void ti_collect_and_solve(ti_inferer *self, int loop) {
    size_t loop_size  = 16;
    size_t loop_count = loop_size;
    while (--loop_count) {
        self->constraints.size   = 0; // reset constraints from prior phase
        self->substitutions.size = 0;

        ti_collect_constraints(self);

        // if (self->verbose) {
        //     ti_inferer_dbg_constraints(self);
        //     ti_inferer_dbg_substitutions(self);
        // }

        ti_run_solver(self);

        // if (self->verbose) {
        //     ti_inferer_dbg_substitutions(self);
        // }

        size_t count =
          ti_apply_substitutions_to_ast(self, (constraint_sized)sized_all(self->substitutions));

        if (!loop || !count) break;
    }
    if (!loop_count) {
        log(self, "loop exhausted");
        dbg_ast_nodes(self);
    }

    if (!loop_count) fatal("ti_collect_and_solve: loop exhausted.");
    log(self, "ti_collect_and_solve: loop count = %zu", loop_size - loop_count);
}

int ti_inferer_run(ti_inferer *self) {

    self->out_program.size = 0;
    self->out_program.v    = null;

    // Create type constructors.
    ti_generate_user_type_functions(self);

    // Give every variable a unique name, respecting lexical scope.
    ti_rename_variables(self);

    // Assign type variables to every ast node. Annotations are handled here.
    ti_assign_type_variables(self);

    // Create function records for every function: toplevel let forms,
    // and let-in assigned lambda functions. Analyses each callsite
    // for conformance with generic template, and assigns compatible
    // function type to the node.
    ti_check_callsites(self);

    if (self->errors.size) {
        ti_inferer_report_errors(self);
        return 1;
    }

    // Run constraint solver until it settles.
    ti_collect_and_solve(self, 1);

    // Create tuple constructors
    ti_generate_tuple_functions(self);

    // Collect function applications that require specialization of generic functions. Fills the
    // self->requirements hashmap.
    ti_collect_specialization_requirements(self);

    if (self->verbose) {
        dbg("\nrequirements:\n");
        log_specialization_records(self);
        dbg("\n");
    }

    // Create specialized functions. Using the self->requirements hashmap, synthesize a new function, if
    // possible, specialised to the arguments evident at the callsite. This also generates all named
    // lambda functions. Adds nodes to the self->specials array, which will eventually become the final
    // program to be emitted. Adds the specialised node to the self->requirements function record.
    ti_create_specials(self);

    if (self->verbose) {
        log_specials(self);
    }

    if (self->errors.size) {
        ti_inferer_report_errors(self);
        return 1;
    }
    if (self->constraints.size) {
        dbg("remaining constraints:\n");
        ti_inferer_dbg_constraints(self);
        return 1;
    }

    // Rewrite function application sites. Using the self->requirements hashmap, examine every symbol in
    // the program and replace it with the specialised name.
    ti_patch_special_applications(self);

    // Run the constraint solver again now that all specializations
    // are in place. Be sure to include specialised functions.
    ti_collect_and_solve(self, 1);

    // Repeat the patch step due to new type information becoming available in the previous
    // collect_and_solve step. This is needed because patching uses the callsite type as part of the hash.
    ti_patch_special_applications(self);

    // Since free variables aren't a formal part of the type system,
    // we have to apply this fixup at the end: ensure that all uses of
    // parameters inside functions have the correct free variable
    // type, where applicable.
    ti_fixup_free_variables(self);

    // Collect the rest of the functions to be emitted (the tuple
    // constructors, user types and main) into the specials array
    ti_collect_functions_to_emit(self);

    if (self->verbose) {
        log(self, "functions to be emitted:");
        forall(i, self->specials) {
            log(self, "%s", ast_node_to_string(self->transient, self->specials.v[i]));
        }

        log(self, "-- type inference completed --");
    }

    array_shrink(self->specials);
    self->out_program.size = self->specials.size;
    self->out_program.v    = self->specials.v;

    arena_reset(self->transient);
    return 0;
}

ast_node_sized ti_inferer_get_program(ti_inferer *self) {
    return self->out_program;
}

//

typedef struct {
    ti_inferer *self;
    u32         error_count;
} ti_report_errors_ctx;

static void find_error(void *ctx_, ast_node *node) {
    ti_report_errors_ctx *ctx  = ctx_;
    ti_inferer           *self = ctx->self;

    if (!node->file) return;

    if (!node->type) {
        fprintf(stderr, "%s:%u: no type assigned %s\n", node->file, node->line,
                ast_node_to_string_for_error(self->transient, node));
        return;
    }

    if (!tl_type_is_poly(node->type)) return;

    ctx->error_count++;
    fprintf(stderr, "%s:%u: cannot infer type of %s\n", node->file, node->line,
            ast_node_to_string_for_error(self->transient, node));
}

void ti_inferer_report_errors(ti_inferer *self) {

    // report callsite errors
    if (self->errors.size) {
        forall(i, self->errors) {
            ti_error       *err  = &self->errors.v[i];
            ast_node const *node = err->node;

            fprintf(stderr, "%s:%u: %s: %s\n", node->file, node->line, tl_error_tag_to_string(err->tag),
                    ast_node_to_string_for_error(self->transient, node));
        }
    }

    // report type errors

    // Find the deepest node with a polymorphic type and report it.
    // Exclude polymorphic functions.

    ti_report_errors_ctx ctx = {.self = self};
    forall(i, *self->nodes) {
        ast_node_dfs_safe_for_recur(self->transient, &ctx, self->nodes->v[i], find_error);
    }

    if (self->constraints.size) {
        dbg("error: unsatisfied constraints\n");
        ti_inferer_dbg_constraints(self);
    }

    dbg("\ninfo: program nodes follow --\n\n");
    dbg_ast_nodes(self);
    dbg("\n-- program nodes end\n\n");
    arena_reset(self->transient);
}

// -- check callsites --

static tl_type *make_args_type(allocator *alloc, ast_node *arguments[], u16 n) {

    tl_type_array types = {.alloc = alloc};
    array_reserve(types, n);
    for (u32 i = 0; i < n; ++i) array_push(types, &arguments[i]->type);

    tl_type *tuple = tl_type_create_tuple(alloc, (tl_type_sized)sized_all(types));

    return tuple;
}

static tl_type *make_labelled_args_type(allocator *alloc, ast_node *arguments[], char const *names[],
                                        u16 n) {

    tl_type_array types = {.alloc = alloc};
    array_reserve(types, n);
    for (u32 i = 0; i < n; ++i) array_push(types, &arguments[i]->type);

    tl_type *tuple = tl_type_create_labelled_tuple(alloc, (tl_type_sized)sized_all(types),
                                                   (c_string_csized){.v = names, .size = n});

    return tuple;
}

static tl_type *make_arrow(allocator *alloc, ast_node *args[], u16 n, char const *names[], tl_type *right) {
    tl_type *left = null;
    if (names) left = make_labelled_args_type(alloc, args, names, n);
    else left = make_args_type(alloc, args, n);

    return tl_type_create_arrow(alloc, left, right);
}

static tl_type *make_lambda_arrow(ti_inferer *self, ast_node *lambda_function) {
    assert(ast_lambda_function == lambda_function->tag);

    struct ast_lambda_function *v = ast_node_lf(lambda_function);
    tl_type *out = make_arrow(self->type_arena, v->parameters, v->n_parameters, null, v->body->type);

    // Free variables in a lambda function have no context - i.e., any variable which is not a formal
    // parameter to the lambda function is a free variable.
    out->arrow.free_variables = ti_free_variables_in(self->type_arena, lambda_function);
    return out;
}

static tl_type *make_let_arrow(allocator *alloc, ast_node *let) {
    assert(ast_let == let->tag);

    struct ast_let *v = ast_node_let(let);
    return make_arrow(alloc, v->parameters, v->n_parameters, null, v->body->type);
}

static tl_type *make_named_application_arrow(ti_inferer *self, ast_node *nfa, hashmap **lex) {
    assert(ast_named_function_application == nfa->tag);
    struct ast_named_application *v         = ast_node_named(nfa);
    ast_node                     *node_name = v->name;

    tl_type *out = make_arrow(self->type_arena, v->arguments, v->n_arguments, null, nfa->type);

    // find free variables in the node

    // We need a lexical scan in order to assign free variables to each arrow type.
    out->arrow.free_variables = ti_free_variables_in_continue(self->type_arena, nfa, lex);

    // add the set of free variables all of its arguments require, if any.
    tl_free_variable_array merged = {.alloc = self->type_arena};
    array_init_from_slice(&merged, &out->arrow.free_variables);

    // merge from function name, if any
    if (type_arrow == v->name->type->tag)
        tl_free_variable_array_merge(&merged, v->name->type->arrow.free_variables);

    for (u32 i = 0; i < v->n_arguments; ++i) {
        ast_node *arg   = v->arguments[i];
        tl_type  *arrow = null;

        if (type_arrow == arg->type->tag) arrow = arg->type;
        else if (ast_symbol == arg->tag) {
            // try to look up function record to get its type, because symbol nodes may not have correct
            // type information at all times. (TODO)
            char const         *arg_name = ast_node_name_string(arg);
            ti_function_record *rec      = ti_lookup_function(self, arg_name);

            if (rec && rec->type && type_arrow == rec->type->tag) {
                arrow = rec->type;
            }
        }

        if (arrow) {
            tl_free_variable_array_merge(&merged, arrow->arrow.free_variables);
        }
    }

    // ensure the node name itself does not appear in its own free variables
    for (u32 i = 0; i < merged.size;) {
        if (0 == string_t_cmp(&node_name->symbol.name, &merged.v[i].name)) array_erase(merged, i);
        else ++i;
    }

    array_shrink(merged);
    out->arrow.free_variables = (tl_free_variable_sized)sized_all(merged);

    return out;
}

void do_create_function_record(ti_inferer *self, ast_node *node, ast_node *name, tl_type *type) {
    // node may be null, if the body of this function is not known
    // e.g. because it's a variable

    assert(name && ast_symbol == name->tag);
    assert(type_arrow == type->tag);

    char const *name_str = string_t_str(&name->symbol.name);
    u16         name_len = strlen(name_str);

    if (map_contains(self->functions, name_str, name_len)) {
        // there was a previous declaration of this name -- could be
        // an annotation, so let's use its type instead of the
        // caller's argument, and its node if the caller didn't give
        // us one because it was a bare annotated symbol.

        ti_function_record *rec = map_get(self->functions, name_str, name_len);
        assert(rec);
        type = rec->type;

        if (!node) node = rec->node;
    }

    // assign the arrow type to the name node
    name->type             = type;

    ti_function_record rec = {.name = name_str, .type = type, .node = node};
    map_set(&self->functions, name_str, name_len, &rec);
    log(self, "create_function_record: '%s' (%s) %s, node = %p", name_str,
        string_t_str(&name->symbol.original), tl_type_to_string(self->transient, type), node);
}

void create_function_record_toplevel(void *ctx, ast_node *node) {
    ti_inferer *self = ctx;

    if (ast_let == node->tag) {
        struct ast_let *v = ast_node_let(node);
        assert(v->name && v->name->type);
        struct ast_symbol *name = ast_node_sym(v->name);
        tl_type           *type = name->annotation_type ? name->annotation_type : node->type;

        // if node type is not an arrow, which is likely if there is no annotation, we must make an
        // arrow type for its function record entry.
        if (type_arrow != type->tag) type = make_let_arrow(self->type_arena, node);

        do_create_function_record(self, node, v->name, type);
    }

    else if (ast_symbol == node->tag) {
        // toplevel symbols are likely annotated symbols with arrow types
        struct ast_symbol *name = ast_node_sym(node);
        if (name->annotation_type && type_arrow == name->annotation_type->tag) {
            do_create_function_record(self, node, node, name->annotation_type);
        }
    }
}

void create_function_record_dfs(void *ctx, ast_node *node) {
    ti_inferer *self = ctx;

    // We only need examine let-in expressions to see if a lambda
    // function is being defined.
    if (ast_let_in == node->tag) {
        // named lambda definition
        ast_node *name  = node->let_in.name;
        ast_node *value = node->let_in.value;
        if (ast_lambda_function == value->tag) {
            assert(value->type && type_arrow == value->type->tag);
            do_create_function_record(self, value, name, value->type);

            // This flag helps us find anonymous (unnamed) lambda
            // functions later.
            BIT_SET(value->lambda_function.flags, AST_LAMBDA_FLAG_NAMED);
        }
    }
}

void create_function_record_variable(void *ctx, ast_node *node) {
    // for named applications on variables
    ti_inferer *self = ctx;

    if (ast_named_function_application == node->tag) {
        ast_node   *name     = node->named_application.name;
        char const *name_str = ast_node_name_string(name);
        if (!ti_is_generated_variable_name(name_str)) return;

        log(self, "create_function_record_variable: %s", name_str);

        do_create_function_record(self, null, name, node->named_application.function_type);
    }
}

void assign_callsite_types(void *ctx, ast_node *node) {
    ti_inferer *self   = ctx;
    int         is_nfa = 0;

    if (!(ast_named_function_application == node->tag) &&
        !(ast_let_in == node->tag && ast_lambda_function == node->let_in.value->tag))
        return;

    if (ast_named_function_application == node->tag) is_nfa = 1;

    char const *name_str = ast_node_name_string(is_nfa ? node->named_application.name : node->let_in.name);
    ti_function_record *rec = map_get(self->functions, name_str, strlen(name_str));
    if (!rec) fatal("function record not found: '%s'", name_str);

    tl_type *arrow         = is_nfa ? node->named_application.function_type : node->let_in.value->type;

    tl_type *function_type = null;
    if (tl_type_is_compatible(rec->type, arrow, 0)) {
        if (rec->node) {
            if (ast_node_is_specialized(rec->node) &&
                tl_free_variable_array_contains(rec->type->arrow.free_variables,
                                                arrow->arrow.free_variables)) {

                // must take function_record type. And from here on,
                // we're no longer interested in the free_variables of
                // the type.
                function_type = rec->type;
                if (arrow->arrow.free_variables.size) {
                    log(self, "assign_callsite_types: '%s' requires %u free variables and satisfied",
                        name_str, arrow->arrow.free_variables.size);
                }
            } else {
                log(self, "assign_callsite_types: rejected record for '%s'", name_str);
            }
        }

        if (!function_type) {
            // Clone the generic type because it's compatible, then add the callsite free variables
            // requirement to it.
            function_type = tl_type_clone(self->type_arena, rec->type, make_typevar_val, self);
            function_type->arrow.free_variables = arrow->arrow.free_variables;

            log(self, "assign_callsite_types: cloned type for '%s'", name_str);

            if (arrow->arrow.free_variables.size)
                log(self, "assign_callsite_types: '%s' requires %u free variables", name_str,
                    arrow->arrow.free_variables.size);
        }

        if (is_nfa) node->named_application.function_type = function_type;
        else node->let_in.value->type = function_type;
    } else {
        ti_error err = {.tag = tl_err_not_compatible, .node = node};
        array_push(self->errors, &err);

        log(self, "callsite type at '%s' '%s' not compatible with '%s': ", name_str,
            tl_type_to_string(self->transient, arrow), tl_type_to_string(self->transient, rec->type));
    }
}

static int ti_check_callsites(ti_inferer *self) {

    int loop_count = 3; // 2 is not enough
    while (loop_count--) {

        // assign arrow types for all functions
        ti_assign_arrow_types(self);

        // create function records for toplevel
        forall(i, *self->nodes) create_function_record_toplevel(self, self->nodes->v[i]);

        // create function records for let-ins and lambdas
        forall(i, *self->nodes)
          ast_node_dfs_safe_for_recur(self->transient, self, self->nodes->v[i], create_function_record_dfs);

        // create function records for variables
        forall(i, *self->nodes) ast_node_dfs_safe_for_recur(self->transient, self, self->nodes->v[i],
                                                            create_function_record_variable);

        // Assign callsite types at named_function_application nodes. In some cases they will be the
        // same as the function record type, and in other cases they will be different.
        forall(i, *self->nodes)
          ast_node_dfs_safe_for_recur(self->transient, self, self->nodes->v[i], assign_callsite_types);
    }

    log_function_records(self);

    if (self->errors.size) return 1;

    arena_reset(self->transient);
    return 0;
}

// -- collect specialization requirements --

typedef struct {
    ti_inferer *self;
} collect_special_requirements_ctx;

static u64 hash_name_and_type(char const *name, tl_type *type) {
    u64 hash = tl_type_hash(type);
    // NOTE: if the type has changed since the function record was created, this hash may fail to find
    // the record when called from patch_one_special
    hash = hash64_combine(hash, (void *)name, strlen(name));
    return hash;
}

static void one_specialization_requirement(void *ctx_, ast_node *const node, hashmap **lex) {
    (void)lex;
    collect_special_requirements_ctx *ctx  = ctx_;
    ti_inferer                       *self = ctx->self;

    if (ast_named_function_application == node->tag) {

        tl_type    *type = node->named_application.function_type;
        char const *name = ast_node_name_string(node->named_application.name);

        // if type is generic, we skip
        if (tl_type_is_poly(type)) return;

        u64                hash = hash_name_and_type(name, type);
        ti_function_record rec  = {.name = name, .type = type, .node = null, .source = node};

        map_set(&self->requirements, &hash, sizeof hash, &rec);

        log(self, "one_specialization_requirement: %-24" PRIu64 " '%s' (%s) %s from source: %s", hash, name,
            string_t_str(&node->named_application.name->symbol.original),
            tl_type_to_string(self->transient, type), ast_node_to_string(self->transient, node));
    }

    else if (ast_let_in == node->tag && ast_lambda_function == node->let_in.value->tag) {

        // Because we are moving function definitions from the functions table to the specials table, we
        // need to include lambda functions here, to ensure an entry is made in the requirements table,
        // which triggers an entry being made in the specials table.
        // TODO: document this better.

        tl_type    *type = node->let_in.value->type;
        char const *name = ast_node_name_string(node->let_in.name);

        // if type is generic, we skip
        if (tl_type_is_poly(type)) return;

        u64                hash = hash_name_and_type(name, type);
        ti_function_record rec  = {.name = name, .type = type, .node = node->let_in.value, .source = node};

        map_set(&self->requirements, &hash, sizeof hash, &rec);

        log(self, "one_specialization_requirement: %-24" PRIu64 " '%s' (%s) %s from source: %s", hash, name,
            string_t_str(&node->let_in.name->symbol.original), tl_type_to_string(self->transient, type),
            ast_node_to_string(self->transient, node));
    }
}

static void ti_collect_specialization_requirements(ti_inferer *self) {
    // for every named application and named lambda function, create a record of the name and the
    // required type.

    // map: name+type hash -> function_record
    collect_special_requirements_ctx ctx = {.self = self};

    forall(i, *self->nodes) {
        ti_traverse_lexical(self->transient, &ctx, self->nodes->v[i], one_specialization_requirement);
    }

    arena_reset(self->transient);
}

// -- create specialized functions --

char *make_specialized_name(ti_inferer *self, char const *name) {
    // These names will be further mangled by the transpiler.

#define fmt "%s_%u"

    int len = snprintf(null, 0, fmt, name, self->next_specialized) + 1;
    if (len < 0) fatal("make_specialized_name: failed");

    char *out = alloc_malloc(self->type_arena, (u32)len);
    snprintf(out, (u32)len, fmt, name, self->next_specialized++);

    return out;
#undef fmt
}

static void process_new_special(ti_inferer *self, ast_node *node) {
    log(self, "process_new_special: %s", ast_node_to_string(self->transient, node));
    assert(ast_let == node->tag);
}

static ast_node *create_specialized_lambda(ti_inferer *self, char const *name, ast_node *lambda,
                                           tl_type *name_type) {
    ast_node       *out = ast_node_create(self->type_arena, ast_let);
    struct ast_let *v   = ast_node_let(out);
    v->n_parameters     = 0;
    v->parameters       = null;
    v->flags            = 0;
    v->name             = null;
    v->body             = null;
    out->type           = type_registry_must_find_name(self->type_registry, "nil");

    ast_node *clone     = ast_node_clone(self->type_arena, lambda);
    if (!out) fatal("clone failed");
    struct ast_lambda_function *vclone = ast_node_lf(clone);

    v->parameters                      = vclone->parameters;
    v->n_parameters                    = vclone->n_parameters;
    v->body                            = vclone->body;
    v->name                            = ast_node_create_sym(self->type_arena, name);
    v->name->type                      = name_type;
    ast_node_set_is_specialized(out);

    process_new_special(self, out);
    return out;
}

static ast_node *create_special(ti_inferer *self, char const *name, ast_node *src, tl_type *name_type) {

    if (ast_let == src->tag) {
        ast_node *out = ast_node_clone(self->type_arena, src);
        if (!out) fatal("clone failed");

        ast_node_set_is_specialized(out);
        out->let.name       = ast_node_create_sym(self->type_arena, name);
        out->let.name->type = name_type;

        log(self, "create_special: %s with type %s", name, tl_type_to_string(self->transient, name_type));

        process_new_special(self, out);
        return out;
    }

    else if (ast_lambda_function == src->tag) {

        ast_node *out = create_specialized_lambda(self, name, src, name_type);
        process_new_special(self, out);
        return out;

    }

    else if (ast_let_in == src->tag && ast_lambda_function == src->let_in.value->tag) {
        // create specialised function for the lambda function

        ast_node *out          = create_specialized_lambda(self, name, src->let_in.value, name_type);
        out->let_in.name->type = name_type;
        process_new_special(self, out);
        return out;
    }

    fatal("logic error");
}

static void ti_create_specials(ti_inferer *self) {

    // For each requirement, create a specialised function based on the generic templates in
    // self->functions.

    ast_node_array   nodes_to_add = {.alloc = self->type_arena};

    hashmap_iterator iter         = {0};
    while (map_iter(self->requirements, &iter)) {
        ti_function_record *callsite = iter.data;

        ti_function_record *function = ti_lookup_function(self, callsite->name);
        if (!function) fatal("could not find function '%s'", callsite->name);

        log(self, "create_specials: considering '%s'", callsite->name);

        //
        char *special_name = make_specialized_name(self, callsite->name);

        // There may be no node in the case of a function variable
        if (!function->node) {
            log(self, "create_specials: skip %s due to empty node", callsite->name);
            continue;
        }

        if (ast_symbol == function->node->tag) {
            // This is an annotated symbol, typically only for compiler intrinsics. We don't need to
            // specialise those.

            // FIXME: need to specialise things like c_malloc so they return the correct C type -- or
            // else, we need to convert *any to *void in the transpiler?
            ;
        } else if (ast_let == function->node->tag) {

            ast_node *created = create_special(self, special_name, function->node, callsite->type);
            callsite->node    = created;
            array_push(nodes_to_add, &created);

            // set its name symbol's type to help transpiler
            assert(ast_let == created->tag);
            created->let.name->type = callsite->type;

            // set each parameter type of the specialised function to be the same type as the source
            // arguments, so that free variable context information is carried over.
            // Some specialisations (for builtins) don't have source.

            ast_node **arguments = null;
            if (ast_named_function_application == callsite->source->tag) {
                arguments = callsite->source->named_application.arguments;
            } else if (ast_let_in == callsite->source->tag &&
                       ast_lambda_function == callsite->source->let_in.value->tag) {
                arguments = callsite->source->let_in.value->lambda_function.parameters;
            }
            if (arguments) {
                for (u32 i = 0; i < created->let.n_parameters; ++i) {
                    ast_node *param    = created->let.parameters[i];
                    ast_node *argument = arguments[i];
                    param->type        = argument->type;
                }
            }

            // FIXME: why add to self->functions from this point? We are actually building
            // self->specials. add to function records
            ti_function_record created_rec = {
              .name = special_name, .type = callsite->type, .node = created};

            log(self, "create_specials: adding let record for '%s' %s ==> %s", special_name,
                tl_type_to_string(self->transient, function->type),
                tl_type_to_string(self->transient, callsite->type));

            map_set(&self->functions, special_name, strlen(special_name), &created_rec);
        }

        else if (ast_lambda_function == function->node->tag) {
            // Named lambda (with a variable name). Can be treated
            // just like a named function.

            ast_node *created = create_special(self, special_name, function->node, callsite->type);
            callsite->node    = created;
            array_push(nodes_to_add, &created);

            // add to function records
            ti_function_record created_rec = {
              .name = special_name, .type = callsite->type, .node = created};

            log(self, "create_specials: adding lambda record for '%s' %s ==> %s", special_name,
                tl_type_to_string(self->transient, function->type),
                tl_type_to_string(self->transient, callsite->type));

            map_set(&self->functions, special_name, strlen(special_name), &created_rec);
        }

        else {
            fatal("unexpected function record node type: %s",
                  ast_node_to_string(self->transient, function->node));
        }
    }

    arena_reset(self->transient);
    self->specials = nodes_to_add;
}

// -- patch special applications --

typedef struct {
    ti_inferer *self;
} patch_special_ctx;

static void patch_one_symbol(patch_special_ctx *ctx, ast_node *node) {
    // also look at every symbol, which could be referencing a function by name. In this case, we don't
    // have access to the enclosing ast node, so we have to overwrite the symbol's data.

    ti_inferer         *self = ctx->self;

    char const         *name = string_t_str(&node->symbol.name);

    tl_type            *type = node->type;
    u64                 hash = hash_name_and_type(name, type);
    ti_function_record *rec  = map_get(self->requirements, &hash, sizeof hash);

    if (!rec) return;

    string_t name_string = string_t_init(self->type_arena, rec->name);
    node->symbol.name    = name_string;
    node->type           = rec->type;

    // patch all arrow types attached to symbols to ensure their free_variables symbols have been
    // patched
    tl_type *arrow = tl_type_get_arrow(node->type);

    if (arrow) {
        tl_free_variable_sized free_variables = arrow->arrow.free_variables;

        forall(i, free_variables) {
            char const         *name = string_t_str(&free_variables.v[i].name);
            tl_type            *type = free_variables.v[i].type;
            u64                 hash = hash_name_and_type(name, type);
            ti_function_record *rec  = map_get(self->requirements, &hash, sizeof hash);
            if (!rec) continue;

            free_variables.v[i].name = string_t_init(self->type_arena, rec->name);
            free_variables.v[i].type = rec->type;
        }
    }
}

static void patch_one_special(void *ctx_, ast_node *node) {

    patch_special_ctx *ctx  = ctx_;
    ti_inferer        *self = ctx->self;

    if (ast_let_in == node->tag) {
        if (ast_lambda_function != node->let_in.value->tag) return;

        char const *name = ast_node_name_string(node->let_in.name);
        tl_type    *type = node->let_in.value->type;

        if (tl_type_is_poly(type)) {
            return;
        }

        u64                 hash = hash_name_and_type(name, type);
        ti_function_record *rec  = map_get(self->requirements, &hash, sizeof hash);

        // If our type is already monomorphic, we might not find a matching name and type in the
        // requirements map. That's because we've already been specialised, so no further patching is
        // needed.
        if (!rec && !tl_type_is_poly(node->let_in.name->type)) return;

        if (!rec) {
            log_specialization_records(self);
            fatal("could not find requirements record for lambda '%s' (%s) %s", name,
                  string_t_str(&node->let_in.name->symbol.original),
                  tl_type_to_string(self->transient, type));
        }
        if (!rec->node) {
            fatal("found requirements record for lambda but it has no specialisation '%s' (%s) %s", name,
                  string_t_str(&node->let_in.name->symbol.original),
                  tl_type_to_string(self->transient, type));
        }

        // take the name of the specialised lambda function
        node->let_in.name       = rec->node->let.name;
        node->let_in.name->type = rec->type;

        assert(type_arrow == node->let_in.name->type->tag);
    }

    else if (ast_named_function_application == node->tag) {

        char const *name = ast_node_name_string(node->named_application.name);

        tl_type    *type = node->named_application.function_type;

        if (tl_type_is_poly(type)) {
            return;
        }

        u64                 hash = hash_name_and_type(name, type);
        ti_function_record *rec  = map_get(self->requirements, &hash, sizeof hash);

        // If our type is already monomorphic, we might not find a matching name and type in the
        // requirements map. That's because we've already been specialised, so no further patching is
        // needed.
        if (!rec && !tl_type_is_poly(node->named_application.name->type)) return;

        // ignore cases where there is no specialised node for this name
        // and type combination, because those are intrinsics or c_ etc.
        if (!rec) {
            log(self, "could not find requirements record for %" PRIu64 " '%s' of type %s", hash, name,
                tl_type_to_string(self->transient, type));
            return;
        }
        if (!rec->node) return;

        if (ast_let == rec->node->tag) {
            // just copy the specialized name to the application site.
            node->named_application.name = rec->node->let.name;

            // mark node as having been specialised, so its new name
            // can be constrained to the implied type of the callsite.
            BIT_SET(node->named_application.flags, AST_NAMED_APP_SPECIALIZED);

        } else if (ast_lambda_function == rec->node->tag) {
            BIT_SET(node->named_application.flags, AST_NAMED_APP_SPECIALIZED);
        }

        // patch the arguments

        for (u32 i = 0; i < node->named_application.n_arguments; ++i) {
            ast_node *arg = node->named_application.arguments[i];
            if (ast_symbol == arg->tag && type_arrow == arg->type->tag) {

                char const         *arg_name = ast_node_name_string(arg);
                u64                 hash     = hash_name_and_type(arg_name, arg->type);
                ti_function_record *rec      = map_get(self->requirements, &hash, sizeof hash);
                if (!rec) {
                    fatal("could not find specialization record for lambda '%s' of type %s", arg_name,
                          tl_type_to_string(self->transient, arg->type));
                }

                assert(ast_let == rec->node->tag); // everything is a let now

                // patch the new name in place
                node->named_application.arguments[i] = rec->node->let.name;
            }
        }

    }

    else if (ast_symbol == node->tag) {
        patch_one_symbol(ctx, node);
    }
}

static void ti_patch_special_applications(ti_inferer *self) {

    // patch all named_applications, let_ins and symbol references to
    // functions. Need to look at entire program, not just starting at
    // main, because functions can be referenced by name in arguments.
    patch_special_ctx ctx = {.self = self};

    forall(i, *self->nodes) {
        ast_node_dfs_safe_for_recur(self->transient, &ctx, self->nodes->v[i], patch_one_special);
    }

    // and the specials
    forall(i, self->specials) {
        ast_node_dfs_safe_for_recur(self->transient, &ctx, self->specials.v[i], patch_one_special);
    }

    arena_reset(self->transient);
}

// -- fixup generic applications --

typedef struct {
    ti_inferer *self;
} fixup_free_variables_ctx;

static void apply_type(void *ctx, ast_node *node) {
    if (ast_symbol != node->tag) return;
    hashmap    *parameter_types = ctx;
    char const *name            = ast_node_name_string(node);
    tl_type   **found           = map_get(parameter_types, name, strlen(name));
    if (found) node->type = *found;
}

static void one_fixup_free_variables(void *ctx_, ast_node *node, hashmap **lexical) {
    fixup_free_variables_ctx *ctx  = ctx_;
    ti_inferer               *self = ctx->self;
    (void)lexical;
    if (ast_let != node->tag) return;

    log(self, "fixup: checking %s", ast_node_name_string(node->let.name));

    hashmap *parameter_types = map_create(self->transient, sizeof(tl_type *));
    for (u32 i = 0; i < node->let.n_parameters; ++i) {
        ast_node   *param = node->let.parameters[i];
        char const *name  = ast_node_name_string(param);
        map_set(&parameter_types, name, strlen(name), &param->type);
    }

    ast_node_dfs_safe_for_recur(self->transient, parameter_types, node, apply_type);

    map_destroy(&parameter_types);
}

static void ti_fixup_free_variables(ti_inferer *self) {
    fixup_free_variables_ctx ctx  = {.self = self};

    ti_function_record      *main = ti_lookup_function(self, "main");
    if (!main) fatal("no main function found");

    ti_traverse_lexical(self->transient, &ctx, main->node, one_fixup_free_variables);
    forall(i, self->specials) {
        ti_traverse_lexical(self->transient, &ctx, self->specials.v[i], one_fixup_free_variables);
    }

    arena_reset(self->transient);
}

// -- collect functions to emit --

typedef struct {
    ti_inferer *self;
    hashmap    *seen; // char const* => int
} collect_syms_ctx;

static void collect_syms(void *ctx_, ast_node *node) {
    collect_syms_ctx *ctx  = ctx_;
    ti_inferer       *self = ctx->self;

    if (ast_symbol != node->tag) return;

    char const *name = ast_node_name_string(node);

    if (0 == strncmp("_v", name, 2)) return;
    if (hset_contains(ctx->seen, name, strlen(name))) return;

    ti_function_record *rec = ti_lookup_function(self, name);
    if (!rec || !rec->node) return;
    if (tl_type_is_poly(rec->type)) return;

    // don't emit symbol nodes
    if (ast_let != rec->node->tag) return;

    log(self, "collect_syms: %s", ast_node_to_string(self->transient, rec->node));
    array_push(self->specials, &rec->node);
    hset_insert(&ctx->seen, name, strlen(name));
}

static void collect_anon_lambdas(void *ctx_, ast_node *node) {
    collect_syms_ctx *ctx  = ctx_;
    ti_inferer       *self = ctx->self;

    // FIXME is this doing anything?? It seems to ensure that anonymous lambdas are emitted as
    // lambda-function nodes to the program. The transpiler can look for top-level anonymous lambdas and
    // generate functions for them.

    if (ast_lambda_function != node->tag) return;
    if (BIT_TEST(node->lambda_function.flags, AST_LAMBDA_FLAG_NAMED)) return;

    log(self, "collect_anon_lambdas: %s", ast_node_to_string(self->transient, node));
    // An anonymous lambda function
    array_push(self->specials, &node);
}

static void ti_collect_functions_to_emit(ti_inferer *self) {

    // check all non-variable symbol references, and if they are
    // function templates which are not generic, include them.

    collect_syms_ctx ctx = {.self = self, .seen = hset_create(self->transient)};

    forall(i, self->specials) {
        ast_node *node = self->specials.v[i];
        if (ast_let != node->tag) continue;
        char const *name = ast_node_name_string(node->let.name);
        hset_insert(&ctx.seen, name, strlen(name));
    }

    ti_function_record *main = ti_lookup_function(self, "main");
    if (!main) fatal("no main function found");
    ast_node_dfs_safe_for_recur(self->transient, &ctx, main->node, collect_syms);

    ast_node_dfs_safe_for_recur(self->transient, &ctx, main->node, collect_anon_lambdas);

    forall(i, *self->nodes) {
        ast_node *node = self->nodes->v[i];

        // Search for generated functions, because these are created
        // by the inferer but may not be directly referenced in the
        // ast, so collect_syms won't find them. For example, tuple
        // constructor functions.
        if (ast_let == node->tag) {
            char const *name = ast_node_name_string(node->let.name);
            if (0 == strncmp("tl_gen_", name, 5)) {
                log(self, "collect_function_to_emit generated: %s",
                    ast_node_to_string(self->transient, node));
                array_push(self->specials, &node);
            }
        }

        // And same for user type definitions
        if (ast_user_type_definition == node->tag) {
            log(self, "collect_function_to_emit user_type_def: %s",
                ast_node_to_string(self->transient, node));
            array_push(self->specials, &node);
        }
    }

    hset_destroy(&ctx.seen);
    arena_reset(self->transient);
}

// -- rename variables --

int ti_is_generated_variable_name(char const *str) {
    return 0 == strncmp("_v", str, 2);
}

static void next_variable_name(ti_inferer *self, string_t *out) {
    char buf[64];
    snprintf(buf, sizeof buf, "_v%u_", self->next_var++);
    *out = string_t_init(self->type_arena, buf);
}

static void rename_if_match(allocator *alloc, string_t *string, hashmap *map, string_t *copy_to) {
    string_t const *found = map_get(map, string_t_str(string), (u16)string_t_size(string));

    if (found) {
        string_t_copy(alloc, copy_to, string); // preserve original name for errors
        string_t_copy(alloc, string, found);
    }
}

typedef void (*ti_traverse_lexical_fun)(void *ctx, ast_node *, hashmap **lexical_map);

typedef struct {
    void    *user_ctx;
    hashmap *lexical_map;
    hashmap *visited;
} traverse_lexical_ctx;

void do_traverse_lexical(void *ctx_, ast_node *node, ti_traverse_lexical_fun fun) {
    traverse_lexical_ctx *ctx = ctx_;

    if (!node) return;

    if (ctx->visited) {
        if (hset_contains(ctx->visited, &node, sizeof(ast_node *))) return;
        hset_insert(&ctx->visited, &node, sizeof(ast_node *));
    }

    lexical_info li = {.name = string_t_init_empty()};

    switch (node->tag) {

    case ast_symbol:
        // symbol nodes are affected by the lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        break;

    case ast_address_of:
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        do_traverse_lexical(ctx, node->address_of.target, fun);
        break;

    case ast_arrow:
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        do_traverse_lexical(ctx, node->arrow.left, fun);
        do_traverse_lexical(ctx, node->arrow.right, fun);
        break;

    case ast_dereference:
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        do_traverse_lexical(ctx, node->dereference.target, fun);
        break;

    case ast_dereference_assign:
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        do_traverse_lexical(ctx, node->dereference_assign.target, fun);
        do_traverse_lexical(ctx, node->dereference_assign.value, fun);
        break;

    case ast_assignment: {
        // a utility node, does nothing on its own. It is part of
        // let_match_in, or labelled_tuple.
        struct ast_assignment *v = ast_node_assignment(node);
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        do_traverse_lexical(ctx, v->name, fun);
        do_traverse_lexical(ctx, v->value, fun);
    } break;

    case ast_labelled_tuple:
    case ast_tuple:          {
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        struct ast_array *v = ast_node_arr(node);
        for (size_t i = 0; i < v->n; ++i) do_traverse_lexical(ctx, v->nodes[i], fun);
    } break;

    case ast_let_in: {
        // This node type creates a lexical context. So we traverse is
        // a specific order: first the value, because it operates in
        // the pre-existing lexical context. Next the let_in node
        // itself, allowing fun() to update map. Then the name and
        // body, which exist in the created context.
        struct ast_let_in *v    = ast_node_let_in(node);

        hashmap           *save = map_copy(ctx->lexical_map);

        do_traverse_lexical(ctx, v->value, fun);

        {
            // add an entry in the lexical map for each variable being
            // created in the lexical scope. Clients like
            // rename_variables will reset the value to something
            // particular, but other clients may just need to
            // determine existence of the symbol.
            ast_node   *name     = node->let_in.name;
            char const *name_str = ast_node_name_string(name);
            li.type              = node->let_in.name->type;
            map_set(&ctx->lexical_map, name_str, strlen(name_str), &li);
        }

        fun(ctx->user_ctx, node, &ctx->lexical_map);

        do_traverse_lexical(ctx, v->name, fun);
        do_traverse_lexical(ctx, v->body, fun);
        fun(ctx->user_ctx, node, &ctx->lexical_map);

        map_destroy(&ctx->lexical_map);
        ctx->lexical_map = save;

    } break;

    case ast_let_match_in: {
        // similar to let_in, only with multiple bindings
        struct ast_let_match_in   *v    = ast_node_let_match_in(node);
        struct ast_labelled_tuple *lt   = ast_node_lt(v->lt);

        hashmap                   *save = map_copy(ctx->lexical_map);

        do_traverse_lexical(ctx, v->value, fun);

        {
            // see note in ast_let above
            for (u32 i = 0; i < lt->n_assignments; ++i) {

                ast_node const *name = lt->assignments[i]->assignment.name;
                li.type              = lt->assignments[i]->assignment.name->type;
                assert(ast_symbol == name->tag);

                char const *name_str = ast_node_name_string(name);
                map_set(&ctx->lexical_map, name_str, strlen(name_str), &li);
            }
        }

        fun(ctx->user_ctx, node, &ctx->lexical_map);

        for (u32 i = 0; i < lt->n_assignments; ++i) {
            do_traverse_lexical(ctx, lt->assignments[i], fun);
        }

        do_traverse_lexical(ctx, v->body, fun);

        map_destroy(&ctx->lexical_map);
        ctx->lexical_map = save;

    } break;

    case ast_let: {
        // creates lexical context
        struct ast_let   *v    = ast_node_let(node);
        struct ast_array *arr  = ast_node_arr(node);

        hashmap          *save = map_copy(ctx->lexical_map);

        {
            // see note in ast_let above

            for (size_t i = 0; i < arr->n; ++i) {
                ast_node const *name = arr->nodes[i];
                li.type              = arr->nodes[i]->type;
                // parameter may be a symbol or nil
                if (ast_nil == name->tag) break; // nil can only be sole param
                assert(ast_symbol == name->tag);

                char const *name_str = ast_node_name_string(name);
                map_set(&ctx->lexical_map, name_str, strlen(name_str), &li);
            }
        }

        fun(ctx->user_ctx, node, &ctx->lexical_map);

        for (size_t i = 0; i < arr->n; ++i) {
            do_traverse_lexical(ctx, arr->nodes[i], fun);
        }

        do_traverse_lexical(ctx, v->body, fun);

        map_destroy(&ctx->lexical_map);
        ctx->lexical_map = save;

    } break;

    case ast_if_then_else: {
        // not affected by lexical context
        struct ast_if_then_else *v = ast_node_ifthen(node);
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        do_traverse_lexical(ctx, v->condition, fun);
        do_traverse_lexical(ctx, v->yes, fun);
        do_traverse_lexical(ctx, v->no, fun);
    } break;

    case ast_lambda_function: {
        // creates lexical context
        struct ast_lambda_function *v    = ast_node_lf(node);
        struct ast_array           *arr  = ast_node_arr(node);

        hashmap                    *save = map_copy(ctx->lexical_map);

        {
            // see note in ast_let above
            for (u32 i = 0; i < arr->n; ++i) {

                ast_node const *name = arr->nodes[i];
                li.type              = arr->nodes[i]->type;
                assert(ast_symbol == name->tag);

                char const *name_str = ast_node_name_string(name);
                map_set(&ctx->lexical_map, name_str, strlen(name_str), &li);
            }
        }
        fun(ctx->user_ctx, node, &ctx->lexical_map);

        for (size_t i = 0; i < arr->n; ++i) {
            do_traverse_lexical(ctx, arr->nodes[i], fun);
        }
        do_traverse_lexical(ctx, v->body, fun);

        map_destroy(&ctx->lexical_map);
        ctx->lexical_map = save;

    } break;

    case ast_lambda_function_application: {
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) do_traverse_lexical(ctx, arr->nodes[i], fun);
        do_traverse_lexical(ctx, node->lambda_application.lambda, fun);
    } break;

    case ast_named_function_application: {
        // not affected by lexical context
        struct ast_array *arr = ast_node_arr(node);
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        for (size_t i = 0; i < arr->n; ++i) do_traverse_lexical(ctx, arr->nodes[i], fun);
        do_traverse_lexical(ctx, node->named_application.name, fun);
    } break;

    case ast_begin_end: {
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) do_traverse_lexical(ctx, arr->nodes[i], fun);

    } break;

    case ast_user_type_get: {
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        struct ast_user_type_get *v = ast_node_utg(node);
        do_traverse_lexical(ctx, v->struct_name, fun);
    } break;

    case ast_user_type_set: {
        // not affected by lexical context
        fun(ctx->user_ctx, node, &ctx->lexical_map);
        struct ast_user_type_set *v = ast_node_uts(node);
        do_traverse_lexical(ctx, v->struct_name, fun);
        do_traverse_lexical(ctx, v->value, fun);
    } break;

    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_user_type:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_user_type_definition: fun(ctx->user_ctx, node, &ctx->lexical_map); break;
    }
}

void ti_traverse_lexical(allocator *alloc, void *user_ctx, ast_node *node, ti_traverse_lexical_fun fun) {
    traverse_lexical_ctx ctx = {.user_ctx    = user_ctx,
                                .lexical_map = map_create(alloc, sizeof(lexical_info)),
                                .visited     = hset_create(alloc)};

    do_traverse_lexical(&ctx, node, fun);

    hset_destroy(&ctx.visited);
    map_destroy(&ctx.lexical_map);
}

void ti_traverse_lexical_continue(allocator *alloc, void *user_ctx, ast_node *node,
                                  ti_traverse_lexical_fun fun, hashmap **lexical_map) {
    traverse_lexical_ctx ctx = {
      .user_ctx = user_ctx, .lexical_map = *lexical_map, .visited = hset_create(alloc)};

    do_traverse_lexical(&ctx, node, fun);

    hset_destroy(&ctx.visited);

    *lexical_map = ctx.lexical_map;
}

void rename_one_variables(void *ctx, ast_node *node, hashmap **lexical_map) {
    if (!node) return;
    ti_inferer *self = ctx;

    // The purpose of this operation is to rename all variables in the program in order to respect
    // lexical scoping rules and variable shadowing rules. Doing this at the start of type analysis lets
    // us assume every occurence of a particular variable name must have the same type.
    //
    // When we specialise a function, we need to rename the variables in the specialised function again,
    // because the function's parameters will have different types, and therefore must not share the
    // same name.
    //
    // Non variable symbols such as struct type field names do not need to participate in this
    // transformation, because the constraint solver knows how to respect constraints relating to user
    // type fields.

    switch (node->tag) {

    case ast_symbol: {
        struct ast_symbol *v = ast_node_sym(node);
        return rename_if_match(self->type_arena, &v->name, *lexical_map, &v->original);
    }

    case ast_let_in: {

        // make a new variable for this let-in subexpression and recurse, but save prior value in case
        // this is a shadowing binding.

        // first apply rename to the value portion of the expression, since it is not allowed to refer
        // to the symbol being defined. But it may refer to an outer let-in binding of the same name.
        struct ast_let_in *v = ast_node_let_in(node);

        string_t           var_name;
        next_variable_name(self, &var_name);

        ast_node const *name     = v->name;
        char const     *name_str = ast_node_name_string(name);
        assert(ast_symbol == name->tag);

        lexical_info *li = map_get(*lexical_map, name_str, strlen(name_str));
        if (li) li->name = var_name;

    } break;

    case ast_let_match_in: {
        // similar to let_in, only with multiple bindings
        struct ast_let_match_in   *v  = ast_node_let_match_in(node);
        struct ast_labelled_tuple *lt = ast_node_lt(v->lt);

        for (u32 i = 0; i < lt->n_assignments; ++i) {
            string_t var_name;
            next_variable_name(self, &var_name);

            ast_node const *name     = lt->assignments[i]->assignment.name;
            char const     *name_str = ast_node_name_string(name);
            assert(ast_symbol == name->tag);

            lexical_info *li = map_get(*lexical_map, name_str, strlen(name_str));
            if (li) li->name = var_name;
        }

    } break;

    case ast_let: {
        // make new variables for all function parameters. save existing map in case any of them shadow.

        // Also apply renaming of variables to any type annotations. The let node defines a function -
        // if the function name is annotated, it may be defining user-defined type variables. Those
        // variables need to be available for use in annotations in the body of the function.

        // FIXME: we should rename the annotations too because they have lexical scope in further
        // annotations

        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) {
            ast_node const *name     = arr->nodes[i];
            char const     *name_str = ast_node_name_string(name);
            // parameter may be a symbol or nil
            if (ast_symbol != name->tag) break; // nil can only be sole param

            string_t var_name;
            next_variable_name(self, &var_name);

            lexical_info *li = map_get(*lexical_map, name_str, strlen(name_str));
            if (li) li->name = var_name;

            // rename the actual parameter symbol
            rename_one_variables(ctx, arr->nodes[i], lexical_map);
        }

    } break;

    case ast_lambda_function: {
        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) {
            ast_node const *name     = arr->nodes[i];
            char const     *name_str = ast_node_name_string(name);
            // parameter may be a symbol or nil
            if (ast_symbol != name->tag) break; // nil can only be sole param

            string_t var_name;
            next_variable_name(self, &var_name);

            lexical_info *li = map_get(*lexical_map, name_str, strlen(name_str));
            if (li) li->name = var_name;

            // rename the actual parameter symbol
            rename_one_variables(ctx, arr->nodes[i], lexical_map);
        }
    } break;

    case ast_if_then_else:
    case ast_address_of:
    case ast_arrow:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_assignment:
    case ast_labelled_tuple:
    case ast_tuple:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_begin_end:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_user_type:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_user_type_definition:        break;
    }
}

static void ti_rename_variables(ti_inferer *self) {
    forall(i, *self->nodes) {
        ti_traverse_lexical(self->type_arena, self, self->nodes->v[i], rename_one_variables);
    }
}

// -- apply substitutions --

nodiscard static size_t apply_one_substitution(tl_type **ptype, tl_type *from, tl_type *to) {
    size_t count = 0;

    assert(ptype && *ptype);
    assert(from);
    assert(to);

    if (!(type_type_var == from->tag || type_type_var == to->tag)) {
        // a prior substitution has already taken place with these
        // types as they are no longer type variables - reference
        // identity
        return 0;
    }

    tl_type *type    = *ptype;

    tl_type *match   = type_type_var == from->tag ? from : to;
    tl_type *replace = type_type_var == from->tag ? to : from;

    if (tl_type_equal(*ptype, match)) {
        *ptype = replace;
        return ++count;
    }

    switch (type->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_user:
    case type_any:
    case type_type_var:
    case type_ellipsis:       break;

    case type_pointer:        count += apply_one_substitution(&type->pointer.target, from, to); break;

    case type_tuple:
    case type_labelled_tuple: {
        struct tlt_array *v = tl_type_arr(type);
        for (size_t i = 0; i < v->elements.size; ++i)
            count += apply_one_substitution(&v->elements.v[i], match, replace);

    } break;

    case type_arrow: {
        count += apply_one_substitution(&type->arrow.left, match, replace);
        count += apply_one_substitution(&type->arrow.right, match, replace);
        forall(i, type->arrow.free_variables) count +=
          apply_one_substitution(&type->arrow.free_variables.v[i].type, match, replace);

    } break;
    }

    return count;
}

typedef struct {
    ti_inferer       *ti;
    constraint_sized *substitutions;
    size_t            count;
} apply_substitutions_ctx;

void dfs_apply_substitutions(void *ctx_, ast_node *node) {
    apply_substitutions_ctx *ctx  = ctx_;
    constraint_sized        *subs = ctx->substitutions;

    tl_type                **buf[UINT8_MAX + 1];
    u32                      buf_size = 0;

    // find additional types in ast variants
    switch (node->tag) {

    case ast_symbol: {
        struct ast_symbol *v = ast_node_sym(node);
        if (v->annotation_type) {
            buf[0]   = &v->annotation_type;
            buf_size = 1;
        }
    } break;

    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);
        for (u8 i = 0; i < v->n_fields; ++i) buf[i] = &v->field_types[i];
        buf_size = v->n_fields;
    } break;

    case ast_user_type_get: {
        struct ast_user_type_get *v = ast_node_utg(node);
        buf[0]                      = &v->struct_name->type;
        buf[1]                      = &v->field_name->type;
        buf_size                    = 2;
    } break;

    case ast_user_type_set: {
        struct ast_user_type_set *v = ast_node_uts(node);
        buf[0]                      = &v->struct_name->type;
        buf[1]                      = &v->field_name->type;
        buf[2]                      = &v->value->type;
        buf_size                    = 3;
    } break;

    case ast_named_function_application: {
        buf[0]   = &node->named_application.function_type;
        buf_size = 1;
    } break;

    case ast_address_of:
    case ast_arrow:
    case ast_assignment:
    case ast_begin_end:
    case ast_user_type:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_labelled_tuple:
    case ast_tuple:
    case ast_let:
    case ast_let_in:
    case ast_let_match_in:
    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application: break;
    }

    forall(i, *subs) {
        ctx->count += apply_one_substitution(&node->type, subs->v[i].left, subs->v[i].right);

        for (u32 j = 0; j < buf_size; ++j)
            ctx->count += apply_one_substitution(buf[j], subs->v[i].left, subs->v[i].right);
    }
}

static size_t ti_apply_substitutions_to_ast(ti_inferer *self, constraint_sized substitutions) {
    apply_substitutions_ctx ctx = {.ti = self, .substitutions = &substitutions, .count = 0};

    forall(i, *self->nodes) {
        ast_node_dfs_safe_for_recur(self->transient, &ctx, self->nodes->v[i], dfs_apply_substitutions);
    }
    forall(i, self->specials) {
        ast_node_dfs_safe_for_recur(self->transient, &ctx, self->specials.v[i], dfs_apply_substitutions);
    }

    arena_reset(self->transient);
    return ctx.count;
}

// -- solver --

static void dbg_constraint(constraint const *c) {
    char buf_left[256], buf_right[256];
    tl_type_snprint(buf_left, sizeof buf_left, c->left);
    tl_type_snprint(buf_right, sizeof buf_right, c->right);
    dbg("constraint %s = %s, line %i\n", buf_left, buf_right, c->line);
}

static void dbg_ast_nodes(ti_inferer *self) {
    for (size_t i = 0; i < self->nodes->size; ++i) {
        char *str = ast_node_to_string(self->transient, self->nodes->v[i]);
        dbg("%p: %s\n", self->nodes->v[i], str);
        alloc_free(self->transient, str);
    }
}

static int substitute_constraints(constraint *begin, constraint *end, constraint const sub) {
    // When a substitution e.g 'tv1 becomes tv2' has been added to the
    // sequence of substitutions to be applied, it should also be
    // immediately applied to the rest of the constraints, so that
    // 'tv1' no longer appears in any constraint. This ensures
    // transitive constraints are satisfied. For example, tv1 = tv2,
    // tv1 = int. Without the step in this function, the constraint
    // tv1 = int would be lost.

    size_t count = 0;

    while (begin != end) {
        count += apply_one_substitution(&begin->left, sub.left, sub.right);
        count += apply_one_substitution(&begin->right, sub.left, sub.right);

        ++begin;
    }

    return count != 0;
}

static int is_any_tuple(tl_type *self) {
    return type_tuple == self->tag || type_labelled_tuple == self->tag;
}

static u32 unify_one(ti_inferer *self, constraint c) {
    if (c.left == c.right || tl_type_equal(c.left, c.right)) return 0;

    else if (type_type_var == c.left->tag || type_type_var == c.right->tag) {
        tl_type   *orig      = type_type_var == c.left->tag ? c.left : c.right;
        tl_type   *other     = type_type_var == c.left->tag ? c.right : c.left;

        constraint candidate = {orig, other, c.line};

        // check conditions to rule out the candidate: original must
        // not appear anywhere in the type replacing it.
        if (tl_type_contains(other, orig)) return 0;

        // push the candidate substitution
        assert(type_type_var == candidate.left->tag);
        array_push(self->substitutions, &candidate);

        return 1;
    }

    // tuple constraints of equal size: unify matching elements
    else if (is_any_tuple(c.left) && is_any_tuple(c.right) &&
             c.left->array.elements.size == c.right->array.elements.size) {

        u32 count = 0;
        for (size_t i = 0; i < c.left->array.elements.size; ++i) {
            count +=
              unify_one(self, make_constraint(c.left->array.elements.v[i], c.right->array.elements.v[i]));
        }

        return count;

    }

    // arrow types: unify matching arms and free variables' types, if they are the same symbol
    else if (type_arrow == c.left->tag && type_arrow == c.right->tag) {
        u32 count = 0;
        count += unify_one(self, make_constraint(c.left->arrow.left, c.right->arrow.left));
        count += unify_one(self, make_constraint(c.left->arrow.right, c.right->arrow.right));

        // NOTE: unification does not consider free variable type information, because it's duplicative

        return count;
    }

    // pointer types
    else if (type_pointer == c.left->tag && type_pointer == c.right->tag) {
        return unify_one(self, make_constraint(c.left->pointer.target, c.right->pointer.target));
    }

    return 0;
}

static void ti_run_solver(ti_inferer *self) {
    size_t loop_count = 100;
    while (--loop_count) {

        int did_substitute = 0;

        for (u32 i = 0; i < self->constraints.size;) { // no end-of-loop increment

            constraint *item = &self->constraints.v[i];

            // erase constraints that are self-satisfied
            if (tl_type_satisfies(item->left, item->right)) {
                array_erase(self->constraints, i); // because of in-loop erase
                continue;                          // continues to next element
            }

            else {
                if (unify_one(self, *item)) {
                    // iterate through remainder of constraints and substitute
                    if (substitute_constraints(&item[1], &self->constraints.v[self->constraints.size],
                                               *item))
                        did_substitute = 1;
                }
            }

            ++i;
        }

        // apply each substitution in sequence to constraints
        forall(i, self->substitutions) {
            if (substitute_constraints(self->constraints.v, &self->constraints.v[self->constraints.size],
                                       self->substitutions.v[i]))
                did_substitute = 1;
        }

        if (!did_substitute) break;
    }

    if (!loop_count) fatal("ti_run_solver: loop exhausted");
}

// -- assign_type_variables --

static tl_type *make_type_annotation(ti_inferer *self, ast_node *ann, hashmap **map) {
    if (ast_nil == ann->tag) {
        tl_type **found = type_registry_find_name(self->type_registry, "nil");
        if (found) return *found;
        fatal("nil type not found");
    }

    if (ast_ellipsis == ann->tag) {
        tl_type **found = type_registry_find_name(self->type_registry, "ellipsis");
        if (found) return *found;
        fatal("ellipsis type not found");
    }

    if (ast_symbol == ann->tag) {
        // either a prim or user type, or a generic/typevar
        char const *ann_str = string_t_str(&ann->symbol.name);
        size_t      len     = strlen(ann_str);
        tl_type   **found   = type_registry_find_name(self->type_registry, ann_str);
        if (found) {
            // If it's an any type, assign it a new typevar
            if (type_any == (*found)->tag) return make_typevar(self);
            return *found;
        }

        // previously seen in the annotation? then assign same type
        found = map_get(*map, ann_str, len);
        if (found) return *found;

        // unknown symbol, consider it as a typevar
        tl_type *out = make_typevar(self);
        map_set(map, ann_str, len, &out);
        return out;
    }

    if (ast_tuple == ann->tag) {
        struct ast_tuple *v        = ast_node_tuple(ann);
        tl_type_array     elements = {.alloc = self->type_arena};
        array_reserve(elements, v->n_elements);

        for (u32 i = 0; i < v->n_elements; ++i) {
            tl_type *res = make_type_annotation(self, v->elements[i], map);
            array_push(elements, &res);
        }

        return tl_type_create_tuple(self->type_arena, (tl_type_sized)sized_all(elements));
    }

    if (ast_arrow == ann->tag) {
        tl_type *left  = make_type_annotation(self, ann->arrow.left, map);
        tl_type *right = make_type_annotation(self, ann->arrow.right, map);

        // promote simple arrows like a -> b to tuple form: (a, ) -> b
        if (!is_any_tuple(left)) {
            if (type_nil == left->tag) {
                left = tl_type_create_tuple(self->type_arena, (tl_type_sized){.size = 0});
            } else {
                tl_type_sized elements = {.v    = alloc_malloc(self->type_arena, sizeof elements.v[0]),
                                          .size = 1};
                elements.v[0]          = left;
                left                   = tl_type_create_tuple(self->type_arena, elements);
            }
        }

        return tl_type_create_arrow(self->type_arena, left, right);
    }

    if (ast_address_of == ann->tag) {
        tl_type *target     = make_type_annotation(self, ann->address_of.target, map);
        tl_type *ptr        = tl_type_create(self->type_arena, type_pointer);
        ptr->pointer.target = target;
        return ptr;
    }

    fatal("unknown annotation type: '%s'", ast_tag_to_string(ann->tag));
}

static void handle_symbol_annotation(ti_inferer *self, ast_node *node) {

    assert(ast_symbol == node->tag);

    ast_node *ann = node->symbol.annotation;
    if (ann) {

        // this map ensures that user-defined type variables (e.g. a, b, c
        // etc) are assigned the same typevars, rather than unique tvars.
        // This is necessary in particular for intrinsic functions which
        // have no bodies that the type solver can analyze to discover
        // constraints.
        hashmap *map                 = map_create_n(self->transient, sizeof(tl_type *), 8);

        node->symbol.annotation_type = make_type_annotation(self, ann, &map);
        map_destroy(&map);
    }

    node->type = make_typevar(self);
}

void assign_type_variables(void *ctx, ast_node *node) {
    ti_inferer *self = ctx;

    switch (node->tag) {
    case ast_let: {
        node->type = make_typevar(self);
    } break;

    case ast_labelled_tuple:
        node->type = type_registry_ast_node_labelled_tuple(self->type_registry, node);
        break;

    case ast_tuple:
        //
        node->type = type_registry_ast_node_tuple(self->type_registry, node);
        break;

    case ast_address_of:
        assert(node->address_of.target && node->address_of.target->type);
        node->type                 = tl_type_create(self->type_arena, type_pointer);
        node->type->pointer.target = node->address_of.target->type;
        assert(node->type->pointer.target);
        break;

    case ast_dereference: {
        assert(node->dereference.target && node->dereference.target->type);
        struct ast_dereference *v       = ast_node_deref(node);
        v->target->type                 = tl_type_create(self->type_arena, type_pointer);
        v->target->type->pointer.target = make_typevar(self);
        node->type                      = make_typevar(self);
    } break;

    case ast_dereference_assign: {
        assert(node->dereference_assign.target && node->dereference_assign.target->type);
        struct ast_dereference_assign *v = ast_node_deref_assign(node);
        v->target->type                  = tl_type_create(self->type_arena, type_pointer);
        v->target->type->pointer.target  = make_typevar(self);
        node->type                       = make_typevar(self);
    } break;

    case ast_symbol:                      handle_symbol_annotation(self, node); break;

    case ast_arrow:
    case ast_assignment:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_lambda_function:
    case ast_let_in:
    case ast_let_match_in:
    case ast_if_then_else:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_begin_end:
    case ast_user_type:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_user_type_definition:        {
        tl_type *tv = make_typevar(self);
        node->type  = tv;
    }

    break;
    }
}

void ti_assign_type_variables(ti_inferer *self) {
    for (size_t i = 0; i < self->nodes->size; ++i) {
        ast_node_dfs_safe_for_recur(self->transient, self, self->nodes->v[i], assign_type_variables);
    }

    arena_reset(self->transient);
}

// -- assign arrow types --

static void assign_one_arrow(void *ctx, ast_node *node, hashmap **lex) {

    // Assigns arrow types to lambda functions and named_function_application, including free variables

    ti_inferer *self = ctx;

    if (ast_lambda_function == node->tag) {
        node->type = make_lambda_arrow(self, node);
    } else if (ast_named_function_application == node->tag) {
        node->named_application.function_type = make_named_application_arrow(self, node, lex);
    }
}

void ti_assign_arrow_types(ti_inferer *self) {
    // Because this is a lexical traverse, the computation of free variables required by an outer named
    // application will be inaccurate until all its formal arguments (recursively) have been analysed.
    forall(i, *self->nodes) {
        ast_node *node = self->nodes->v[i];
        if (ast_let == node->tag)
            ti_traverse_lexical(self->transient, self, (ast_node *)node, assign_one_arrow);
    }
}

// -- collect_constraints --

static tl_type *get_prim(ti_inferer *self, tl_type_tag tag) {
    tl_type **type = type_registry_find_name(self->type_registry, tl_type_tag_to_string(tag));
    if (!type) fatal("get_prim: failed to find '%s'", tl_type_tag_to_string(tag));

    return *type;
}

void collect_constraints(void *ctx_, ast_node *node, hashmap **lex) {
    collect_constraints_ctx *ctx  = ctx_;
    ti_inferer              *self = ctx->ti;
    constraint               c    = {0};

#define push(L, R)                                                                                         \
    do {                                                                                                   \
        assert((L) && (R));                                                                                \
        c = (constraint){(L), (R), __LINE__};                                                              \
        if ((L) != (R)) array_push(self->constraints, &c);                                                 \
    } while (0)

    switch (node->tag) {
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:      push(node->type, get_prim(self, type_nil)); break;
    case ast_bool:     push(node->type, get_prim(self, type_bool)); break;

    case ast_arrow: // only used for annotation
        break;

    case ast_user_type_definition:
        //
        push(node->type, get_prim(self, type_nil));
        break;

    case ast_symbol: {

        char const *name_str = ast_node_name_string(node);
        u16         name_len = (u16)string_t_size(&node->symbol.name);

        // every occurence of a symbol must be the same type,
        // unless it's the name of a generic function. In that
        // case, it cannot be constrained until it is replaced
        // with a specialised function name.
        lexical_info *li = map_get(*lex, name_str, name_len);
        if (li) push(node->type, li->type);
        else {
            lexical_info li = {.name = string_t_init(self->type_arena, name_str)};

            // FIXME this shouldn't be here but it's necessary. Maybe it should be in a separate pass.

            ti_function_record *rec = ti_lookup_function(self, name_str);
            if (rec) {
                if (tl_type_is_poly(rec->type)) {
                    li.type = node->type;
                    map_set(lex, name_str, name_len, &li);
                } else {
                    push(node->type, rec->type);
                    li.type = rec->type;
                    map_set(lex, name_str, name_len, &li);
                }
            } else {
                // FIXME: if no function record, why are we setting the type of a free variable?

                // li.type = node->type;
                // map_set(lex, name_str, name_len, &li);
            }
        }

        // ensure symbol type matches its annotated type, if any
        tl_type *annotation = ast_node_annotation(node);
        if (annotation) {
            push(node->type, annotation);
        }

    } break;

    case ast_address_of: {

        assert(type_pointer == node->type->tag);
        // node type set by assign_type_variables
        push(node->type->pointer.target, node->address_of.target->type);

    } break;

    case ast_dereference: {

        assert(type_pointer == node->dereference.target->type->tag);
        push(node->type, node->dereference.target->type->pointer.target);

    } break;

    case ast_dereference_assign: {

        assert(type_pointer == node->dereference_assign.target->type->tag);
        push(node->type, node->dereference_assign.value->type);

    } break;

    case ast_assignment:
        //
        push(node->assignment.name->type, node->assignment.value->type);
        push(node->type, node->assignment.value->type);
        break;

    case ast_i64:
    case ast_u64:
        push(node->type, get_prim(self, type_int)); // TODO unsigned
        break;

    case ast_f64:       push(node->type, get_prim(self, type_float)); break;
    case ast_string:    push(node->type, get_prim(self, type_string)); break;

    case ast_begin_end: {
        struct ast_begin_end const *v = ast_node_begin_end(node);
        if (v->n_expressions) push(node->type, v->expressions[v->n_expressions - 1]->type);
    } break;

    case ast_user_type: {

        tl_type **type =
          type_registry_find_name(self->type_registry, ast_node_name_string(node->user_type.name));
        if (!type)
            fatal("collect_constraints: failed to find type '%s'",
                  ast_node_name_string(node->user_type.name));

        push(node->type, *type);

        // each field must be constrained to its correct type
        struct tlt_user           *user_type = tl_type_user(*type);
        struct tlt_labelled_tuple *lt        = tl_type_lt(user_type->labelled_tuple);
        assert(node->user_type.n_fields == lt->fields.size);

        for (u16 i = 0; i < node->user_type.n_fields; ++i)
            push(node->user_type.fields[i]->type, lt->fields.v[i]);

    } break;

    case ast_user_type_get: {
        // node type is the type of the field being accessed
        struct ast_user_type_get *v           = ast_node_utg(node);
        tl_type                  *struct_name = v->struct_name->type;

        tl_type                  *name_type   = null;
        if (type_pointer == struct_name->tag) name_type = struct_name->pointer.target;
        else name_type = struct_name;

        if (type_user == name_type->tag) {
            tl_type *field_type =
              tl_type_find_user_field_type(name_type, ast_node_name_original(v->field_name));
            push(node->type, field_type);
        } else if (type_labelled_tuple == name_type->tag) {
            struct tlt_labelled_tuple *lt = tl_type_lt(name_type);

            // node type is the field type, find the matching name in the lt
            char const *field_name = ast_node_name_string(v->field_name);
            forall(i, lt->names) {
                if (0 == strcmp(lt->names.v[i], field_name)) push(node->type, lt->fields.v[i]);
            }

        }

        else {
            // wait until a later repetition for user types
        }

    } break;

    case ast_user_type_set: {
        // node type is the type of the value being assigned
        struct ast_user_type_set *v = ast_node_uts(node);
        push(node->type, v->value->type);
    } break;

    case ast_labelled_tuple: {

        struct ast_labelled_tuple *v = ast_node_lt(node);
        for (u32 i = 0; i < v->n_assignments; ++i) {
            struct ast_assignment *ass = ast_node_assignment(v->assignments[i]);
            push(ass->name->type, ass->value->type);
        }

        tl_type *lt = type_registry_ast_node_labelled_tuple(self->type_registry, node);
        push(node->type, lt);
    } break;

    case ast_tuple: {
        tl_type *els = make_args_type(self->type_arena, node->array.nodes, node->array.n);

        push(node->type, els);
    } break;

    case ast_function_declaration:
    case ast_lambda_declaration:
        /* function_declaration and lambda_declaration only appear during compilation */
        break;

    case ast_let_in:
        // variable name same type as value
        push(node->let_in.name->type, node->let_in.value->type);

        // result must be same type as body
        push(node->type, node->let_in.body->type);
        break;

    case ast_let_match_in: {
        struct ast_let_match_in   *v  = ast_node_let_match_in(node);
        struct ast_labelled_tuple *lt = ast_node_lt(v->lt);

        for (u32 i = 0; i < lt->n_assignments; ++i) {
            if (type_user == v->value->type->tag) {
                tl_type *field_type = tl_type_find_user_field_type(
                  v->value->type, ast_node_name_original(lt->assignments[i]->assignment.value));

                push(lt->assignments[i]->assignment.name->type, field_type);

            } else if (type_labelled_tuple == v->value->type->tag) {
                tl_type *field_type = tl_type_find_labelled_field_type(
                  v->value->type, ast_node_name_string(lt->assignments[i]->assignment.value));

                push(lt->assignments[i]->assignment.name->type, field_type);

            } else {
                // wait until a later repetition
            }
        }

        // result must be same type as body
        push(node->type, v->body->type);
    } break;

    case ast_let: {
        // If the let name has an arrow type, we use it to apply constraints to the parameters and body
        // type.
        tl_type *arrow = node->let.name->type;
        if (type_arrow == arrow->tag) {
            tl_type *params = make_args_type(self->type_arena, node->array.nodes, node->array.n);
            push(arrow->arrow.left, params);
            push(arrow->arrow.right, node->let.body->type);
        }

        // If the function is 'main', we constrain the body type to be
        // the required int return type for main().
        if (0 == strcmp("main", ast_node_name_string(node->let.name))) {
            push(type_registry_must_find_name(self->type_registry, "int"), node->let.body->type);
        }

        // result is nil
        push(node->type, get_prim(self, type_nil));

    } break;

    case ast_if_then_else:
        // yes and no arms same type
        push(node->if_then_else.yes->type, node->if_then_else.no->type);

        // condition must be bool
        push(node->if_then_else.condition->type, type_registry_must_find_name(self->type_registry, "bool"));

        // result is same type as arms
        push(node->type, node->if_then_else.yes->type);
        break;

    case ast_lambda_function: {
        // argument tuple must be same type as parameter tuple
        struct tlt_arrow *v   = tl_type_arrow(node->type);

        tl_type          *tup = make_args_type(self->type_arena, node->array.nodes, node->array.n);
        push(v->left, tup);

        // body type must be same as right hand of arrow
        push(v->right, node->lambda_function.body->type);

        break;

    } break;

    case ast_lambda_function_application: {
        // lambda must be a ast_lambda_functoin
        ast_node const *lambda = node->lambda_application.lambda;
        assert(ast_lambda_function == lambda->tag);

        // arguments must match parameters
        tl_type *args   = make_args_type(self->type_arena, node->array.nodes, node->array.n);
        tl_type *params = make_args_type(self->type_arena, lambda->array.nodes, lambda->array.n);

        push(args, params);

        // result must be same as right hand of arrow
        struct tlt_arrow *v = tl_type_arrow(lambda->type);
        push(node->type, v->right);
    } break;

    case ast_named_function_application: {
        struct ast_named_application *v = ast_node_named(node);

        // Our function type has been assigned based on the named
        // function template. We need to constrain our arguments'
        // types to match it.
        assert(v->function_type && type_arrow == v->function_type->tag);

        tl_type *args_type = make_args_type(self->type_arena, v->arguments, v->n_arguments);
        push(v->function_type->arrow.left, args_type);
        push(v->function_type->arrow.right, node->type);

        // After specialisation, our symbol's type must match the arrow type and our node type must
        // match the result type.
        if (BIT_TEST(v->flags, AST_NAMED_APP_SPECIALIZED)) {
            push(v->name->type, v->function_type);
            push(node->type, v->function_type->arrow.right);
        } else {
            // Before specialization, if the name type is not generic, we
            // should also constrain. This propagates constraints in the
            // other direction, for example because of annotations.
            if (!tl_type_is_poly(v->name->type)) {

                // Set the function type to match the name type, since it's not generic. Do not
                // overwrite free_variables portion of type. TODO make this safer.
                assert(type_arrow == v->function_type->tag && type_arrow == v->name->type->tag);
                v->function_type->arrow.left  = v->name->type->arrow.left;
                v->function_type->arrow.right = v->name->type->arrow.right;
                push(node->type, v->function_type->arrow.right);

                // Set the specialized flag since no further
                // specialization of this node is required.
                BIT_SET(v->flags, AST_NAMED_APP_SPECIALIZED);
            }
        }
    } break;
    }

#undef push
}

void ti_collect_constraints(ti_inferer *self) {
    collect_constraints_ctx ctx = {.ti = self};

    // FIXME: make these traversals lexical, and only enforce same-named symbols being same type if they
    // are in the same lexical scope.

    for (size_t i = 0; i < self->nodes->size; ++i)
        ti_traverse_lexical(self->transient, &ctx, self->nodes->v[i], collect_constraints);

    forall(i, self->specials)
      ti_traverse_lexical(self->transient, &ctx, self->specials.v[i], collect_constraints);

    arena_reset(self->transient);
}

void ti_inferer_dbg_constraints(ti_inferer const *self) {
    log(self, "Constraints: %u", self->constraints.size);
    forall(i, self->constraints) dbg_constraint(&self->constraints.v[i]);
}

void ti_inferer_dbg_substitutions(ti_inferer const *self) {
    log(self, "Substitutions: %u", self->substitutions.size);
    forall(i, self->substitutions) dbg_constraint(&self->substitutions.v[i]);
}

// -- specialize function applications --

char *make_tuple_name(allocator *alloc, tl_type *type) {
    assert(type_tuple == type->tag || type_labelled_tuple == type->tag);
    struct tlt_array *v = tl_type_arr(type);

    char              buf[64];
    char              out[256];
    u16               buf_len = 0, out_len = 0;

    forall(i, v->elements) {
        buf_len = 0;
        buf_len += tl_type_snprint(buf + buf_len, (int)sizeof buf - buf_len, v->elements.v[i]);
        if (out_len >= sizeof out) fatal("buffer overflow");
        memcpy(out + out_len, buf, buf_len);
        out_len += buf_len;
        if (i < v->elements.size - 1) out[out_len++] = '_';
    }
    out[out_len] = '\0';

    return alloc_strdup(alloc, out);
}

//

static ast_node *make_type_constructor_function(ti_inferer *self, char const *name, tl_type *user_type) {
    // create a let_node with parameters matching the user_type fields, and a body with a single node, a
    // user_type literal

    struct tlt_user *v = tl_type_user(user_type);

    // TODO this is a transpiler concern
    //
    // constructor name: _gen_make_{type}_
    char *generated_name = null;
    {
#define fmt "tl_gen_make_%s_"
        int len = snprintf(null, 0, fmt, name) + 1;
        if (len < 0) fatal("make_type_constructor_function: generate name failed.");
        generated_name = alloc_malloc(self->type_arena, (u32)len);
        snprintf(generated_name, (u32)len, fmt, name);
#undef fmt
    }

    ast_node *out         = ast_node_create(self->type_arena, ast_let);
    out->let.parameters   = null;
    out->let.n_parameters = 0;
    out->let.flags        = 0;
    out->let.body         = null;
    out->let.name         = ast_node_create_sym(self->type_arena, name);
    ast_node_set_is_specialized(out);

    // make params array from user_type's labelled_tuple
    struct tlt_labelled_tuple *lt = tl_type_lt(v->labelled_tuple);
    assert(lt->fields.size == lt->names.size);

    out->let.n_parameters = (u8)lt->fields.size;
    out->let.parameters   = alloc_malloc(self->type_arena, lt->fields.size * sizeof out->let.parameters[0]);
    for (u16 i = 0; i < out->let.n_parameters; ++i)
        out->let.parameters[i] = ast_node_create_sym(self->type_arena, lt->names.v[i]);

    // the body is a single node with the type literal
    out->let.body                     = ast_node_create(self->type_arena, ast_user_type);
    out->let.body->user_type.fields   = null;
    out->let.body->user_type.n_fields = 0;
    out->let.body->user_type.name     = null;

    // with each parameter mapped directly to a field. We can share
    // references to the same symbol nodes here because their types
    // will be identical.
    ast_node *body           = out->let.body;
    body->user_type.n_fields = out->let.n_parameters;
    body->user_type.fields   = out->let.parameters;
    body->user_type.name     = ast_node_create_sym(self->type_arena, name);

    return out;
}

static void ti_generate_user_type_functions(ti_inferer *self) {
    // generate required functions for user defined types: constructors

    ast_node_array added = {.alloc = self->type_arena};

    forall(i, *self->nodes) {
        ast_node const *node = self->nodes->v[i];
        if (ast_user_type_definition != node->tag) continue;

        struct ast_user_type_def const *v = ast_node_utd((ast_node *)node);

        // type must be registered
        char const *type_name = ast_node_name_string(v->name);
        tl_type   **ty        = type_registry_find_name(self->type_registry, type_name);
        if (!ty) fatal("generate_user_type_functions: could not find type '%s'", type_name);

        // make constructor

        ast_node *constructor = make_type_constructor_function(self, type_name, *ty);
        array_push(added, &constructor);
    }

    // add nodes to program
    forall(i, added) {
        array_push(*self->nodes, &added.v[i]);
    }
}

//

static ast_node *make_tuple_constructor_function(ti_inferer *self, u64 hash, ast_node *node) {
    // create a let_node with parameters matching the user_type fields, and a body with a single node, a
    // user_type literal

    // TODO share this with transpiler
    // constructor name: _gen_make_{type}_
    char *generated_name = null;
    {
#define fmt "tl_gen_make_tup_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        generated_name = alloc_malloc(self->type_arena, (u32)len);
        snprintf(generated_name, (u32)len, fmt, hash);
#undef fmt
    }

    allocator *a          = self->type_arena;
    ast_node  *out        = ast_node_create(a, ast_let);
    out->let.parameters   = null;
    out->let.n_parameters = 0;
    out->let.flags        = 0;
    out->let.body         = null;
    out->type             = *type_registry_find_name(self->type_registry, "nil");
    out->let.name         = ast_node_create_sym(a, generated_name);
    ast_node_set_is_specialized(out);
    ast_node_set_is_tuple_constructor(out);

    if (ast_labelled_tuple == node->tag) {
        // make params array from labelled_tuple
        struct ast_labelled_tuple *lt = ast_node_lt(node);

        out->let.n_parameters         = (u8)lt->n_assignments;
        out->let.parameters           = ast_node_assignment_names(self->type_arena, node);

        // the body is a single node with the tuple literal
        out->let.body                               = ast_node_create(a, ast_labelled_tuple);
        out->let.body->labelled_tuple.assignments   = null;
        out->let.body->labelled_tuple.n_assignments = 0;
        out->let.body->labelled_tuple.flags         = 0;

        out->let.body->type                         = node->type;

        struct ast_labelled_tuple *v                = ast_node_lt(out->let.body);
        v->n_assignments                            = out->let.n_parameters;
        v->assignments = alloc_malloc(a, v->n_assignments * sizeof v->assignments[0]);
        BIT_SET(v->flags, AST_TUPLE_FLAG_INIT);
        for (u16 i = 0; i < v->n_assignments; ++i) {
            v->assignments[i]                  = ast_node_create(a, ast_assignment);
            v->assignments[i]->assignment.name = ast_node_clone(a, lt->assignments[i]->assignment.name);

            // init value from parameter
            v->assignments[i]->assignment.value = out->let.parameters[i];
            v->assignments[i]->type             = lt->assignments[i]->type;
        }

        c_string_array names = {.alloc = self->type_arena};
        for (u16 i = 0; i < v->n_assignments; ++i) {
            char const *name = ast_node_name_string(v->assignments[i]->assignment.name);
            array_push(names, &name);
        }

        // make an arrow type for the generated function
        tl_type *arrow =
          make_arrow(a, out->let.parameters, out->let.n_parameters, (char const **)names.v, node->type);

        // register the function record
        do_create_function_record(self, out, out->let.name, arrow);

    } else {
        // need to construct names for the anonymous tuple elements
        struct ast_tuple *tup = ast_node_tuple(node);
        out->let.n_parameters = (u8)tup->n_elements;
        out->let.parameters   = alloc_malloc(a, out->let.n_parameters * sizeof out->let.parameters[0]);

        for (u16 i = 0; i < out->let.n_parameters; ++i) {
            char buf[32];
            snprintf(buf, sizeof buf - 1, "x%u", i);
            out->let.parameters[i] = ast_node_create_sym(a, buf);
        }

        // the body is a single node with the tuple literal
        out->let.body                   = ast_node_create(a, ast_tuple);
        out->let.body->tuple.elements   = null;
        out->let.body->tuple.n_elements = 0;
        out->let.body->tuple.flags      = 0;

        out->let.body->type             = node->type;

        struct ast_tuple *v             = ast_node_tuple(out->let.body);
        v->n_elements                   = out->let.n_parameters;
        v->elements                     = alloc_malloc(a, v->n_elements * sizeof v->elements[0]);
        BIT_SET(v->flags, AST_TUPLE_FLAG_INIT); // tell transpiler to emit initialisation code

        for (u16 i = 0; i < v->n_elements; ++i) {
            v->elements[i]       = out->let.parameters[i];
            v->elements[i]->type = tup->elements[i]->type;
        }

        // make an arrow type for the generated function
        tl_type *arrow = make_arrow(a, out->let.parameters, out->let.n_parameters, null, node->type);

        // register the function record
        do_create_function_record(self, out, out->let.name, arrow);
    }

    return out;
}

static void generate_tuple_function(ti_inferer *self, ast_node *node, ast_node_array *added,
                                    hashmap **seen) {
    if (ast_tuple != node->tag && ast_labelled_tuple != node->tag) return;

    // we can't run this until type inference has been run far enough for us to infer all types about
    // these tuples.

    if (ast_tuple == node->tag) {
        struct ast_tuple *v = ast_node_tuple(node);
        for (u32 i = 0; i < v->n_elements; ++i)
            if (tl_type_is_poly(v->elements[i]->type))
                fatal("generate_tuple_function: unexpected polymorphic type");

    } else {
        struct ast_labelled_tuple *v = ast_node_lt(node);
        for (u32 i = 0; i < v->n_assignments; ++i)
            if (tl_type_is_poly(v->assignments[i]->type))
                fatal("generate_tuple_function: unexpected polymorphic type");
    }

    u64 hash = tl_type_hash(node->type);
    if (hset_contains(*seen, &hash, sizeof hash)) return;

    ast_node *constructor = make_tuple_constructor_function(self, hash, node);

    hset_insert(seen, &hash, sizeof hash);
    array_push(*added, &constructor);
}

static void generate_tuple_function_glue(void *ctx_, ast_node *node) {
    generate_tuple_function_ctx *ctx  = ctx_;
    ti_inferer                  *self = ctx->self;
    return generate_tuple_function(self, node, ctx->added, &ctx->seen);
}

static void ti_generate_tuple_functions(ti_inferer *self) {
    // generate constructor functions for every tuple type in the program.

    ast_node_array              added = {.alloc = self->type_arena};
    generate_tuple_function_ctx ctx   = {
        .self  = self,
        .added = &added,
        .seen  = hset_create(self->transient),
    };

    forall(i, *self->nodes)
      ast_node_dfs_safe_for_recur(self->transient, &ctx, self->nodes->v[i], generate_tuple_function_glue);

    // add nodes to program
    forall(i, added) {
        array_push(*self->nodes, &added.v[i]);
    }

    hset_destroy(&ctx.seen);
    arena_reset(self->transient);
}

//

void find_free_variables(void *ctx, ast_node *node, hashmap **lex) {

    ast_node_array *array = ctx;

    // If symbol is not in the lexical map, it is a free variable. But
    // only if it's an actual generated variable name, so this
    // excludes function names.

    switch (node->tag) {

    case ast_symbol: {
        char const *name = ast_node_name_string(node);

        if (!ti_is_generated_variable_name(name)) return;
        if (!map_contains(*lex, name, strlen(name))) array_push(*array, &node);

    } break;

    case ast_address_of:  find_free_variables(ctx, node->address_of.target, lex); break;
    case ast_dereference: find_free_variables(ctx, node->dereference.target, lex); break;
    case ast_dereference_assign:
        find_free_variables(ctx, node->dereference_assign.target, lex);
        find_free_variables(ctx, node->dereference_assign.value, lex);
        break;

    case ast_user_type_get:
        //
        find_free_variables(ctx, node->user_type_get.struct_name, lex);
        break;

    case ast_user_type_set:
        find_free_variables(ctx, node->user_type_set.struct_name, lex);
        find_free_variables(ctx, node->user_type_set.value, lex);
        break;

    case ast_nil:
    case ast_arrow:
    case ast_assignment:
    case ast_bool:
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
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_lambda_function:
    case ast_lambda_function_application:
    case ast_let:
    case ast_named_function_application:
    case ast_tuple:
    case ast_user_type:                   break;
    }
}

static tl_free_variable_sized ast_node_array_to_free_variables(allocator *alloc, ast_node_sized array) {
    tl_free_variable_array fva = {.alloc = alloc};
    array_reserve(fva, array.size);
    forall(i, array) {
        assert(ast_symbol == array.v[i]->tag);
        tl_free_variable fv = {.name = array.v[i]->symbol.name, .type = array.v[i]->type};
        array_insert_sorted(fva, &fv, tl_free_variable_cmpv);
    }
    array_shrink(fva);
    return (tl_free_variable_sized)sized_all(fva);
}

tl_free_variable_sized ti_free_variables_in(allocator *alloc, ast_node const *node) {

    // return array of free variable symbols found in node and its
    // children. free variables are symbols not defined by a visible
    // lexical scope parent. Algorithm: do a lexical traverse, keeping
    // track of lexical variables, and build an array of free
    // variables.

    // It is an error to start this scan from a named_application node.
    // TODO remove this once all bugs have been squashed.
    if (ast_named_function_application == node->tag) fatal("logic error");

    ast_node_array array = {.alloc = alloc};

    ti_traverse_lexical(alloc, &array, (ast_node *)node, find_free_variables);

    return ast_node_array_to_free_variables(alloc, (ast_node_sized)sized_all(array));
}

tl_free_variable_sized ti_free_variables_in_continue(allocator *alloc, ast_node const *node,
                                                     hashmap **lexical_map) {

    // Use this variant when the analysis of free variables needs to include existing context, for
    // example from the enclosing let node.

    // return array of free variable symbols found in node and its
    // children. free variables are symbols not defined by a visible
    // lexical scope parent. Algorithm: do a lexical traverse, keeping
    // track of lexical variables, and build an array of free
    // variables.

    ast_node_array array = {.alloc = alloc};

    ti_traverse_lexical_continue(alloc, &array, (ast_node *)node, find_free_variables, lexical_map);

    return ast_node_array_to_free_variables(alloc, (ast_node_sized)sized_all(array));
}

//

void log(ti_inferer const *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "ti: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
static void log_function_records(ti_inferer *self) {
    if (!self->verbose) return;
    hashmap_iterator iter = {0};
    while (map_iter(self->functions, &iter)) {
        ti_function_record *rec = iter.data;
        char                buf[128];
        if (iter.key_size < sizeof buf) {
            memcpy(buf, iter.key_ptr, iter.key_size);
            buf[iter.key_size] = '\0';
            log(self, "function record: %s -> %s: %s node: %p", buf, rec->name,
                tl_type_to_string(self->transient, rec->type), rec->node);
        } else {
            log(self, "function record: %s: %s", rec->name, tl_type_to_string(self->transient, rec->type));
        }
    }
}
#pragma clang diagnostic pop

static void log_specialization_records(ti_inferer *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->requirements, &iter)) {
        ti_function_record *rec = iter.data;
        assert(iter.key_size == sizeof(u64));
        log(self, "specialization requirement: %" PRIu64 " -> %s %s source: %s", *(u64 *)iter.key_ptr,
            rec->name, tl_type_to_string(self->transient, rec->type),
            ast_node_to_string(self->transient, rec->source));
    }
}

static void log_specials(ti_inferer *self) {
    dbg("\nspecials: \n");
    forall(i, self->specials) {
        char *str = ast_node_to_string(self->transient, self->specials.v[i]);
        dbg("%p: %s\n", self->specials.v[i], str);
        alloc_free(self->transient, str);
    }
    dbg("\n");
}

constraint make_constraint(tl_type *l, tl_type *r) {
    return (constraint){l, r, -1};
}

u32 make_typevar_val(void *ctx) {
    ti_inferer *self = ctx;
    // also used with tl_clone_type
    return self->next_type_var++;
}

tl_type *make_typevar(ti_inferer *self) {
    return tl_type_create_type_var(self->type_arena, make_typevar_val(self));
}

ti_function_record *ti_lookup_function(ti_inferer *self, char const *name) {
    return map_get(self->functions, name, strlen(name));
}

//
