#include "type_inference.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hashmap.h"
#include "string_t.h"
#include "type.h"
#include "type_registry.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define TYPE_ARENA_SIZE    16 * 1024
#define STRINGS_ARENA_SIZE 4 * 1024
#define CONSTRAINTS_SIZE   1024

typedef struct constraint {
    tl_type *left;
    tl_type *right;
} constraint;

typedef struct {
    array_header;
    constraint *v;
} constraint_array;

typedef struct {
    array_sized;
    constraint *v;
} constraint_sized;

struct ti_inferer {
    allocator       *type_arena;
    allocator       *transient;

    ast_node_array  *nodes;
    type_registry   *type_registry;

    constraint_array constraints;
    constraint_array substitutions;

    u32              next_var;         // for rename_variables
    u32              next_type_var;    // for assign_type_variables
    u32              next_specialized; // for specialized function names
    int              indent_level;     // for log output

    // flags which do not affect operation
    int verbose;

    // flags which affect operation
    int constrain_function_applications;
    // To implement function specialisation, the first pass of
    // collect_constraints does not constrain function applications.

    int unify_monotypes;
    // can be set to false to find type variables that may have
    // contradictory constraints. This will aid in reporting to the
    // user which program forms are ill-typed.
};

// -- ti_inferer --

typedef struct {
    ti_inferer *ti;
    allocator  *alloc;
    hashmap    *map;
} rename_variables_ctx;

typedef struct {
    ti_inferer *ti;
    hashmap    *symbols;
} collect_constraints_ctx;

typedef struct {
    ti_inferer    *ti;
    ast_node_array specials;
} specialize_functions_ctx;

typedef struct {
    ti_inferer     *self;
    ast_node_array *added;
    hashmap        *map;
} generate_tuple_function_ctx;

static size_t     ti_apply_substitutions_to_ast(ti_inferer *, constraint_sized, ast_node_sized);
static void       ti_assign_type_variables(ti_inferer *);
static void       ti_collect_and_solve(ti_inferer *, int);
static void       ti_collect_constraints(ti_inferer *);
static void       ti_generate_tuple_functions(ti_inferer *);
static void       ti_generate_user_type_functions(ti_inferer *);
static void       ti_rename_variables(ti_inferer *);
static void       ti_run_solver(ti_inferer *);
static void       ti_specialize_functions(ti_inferer *, ast_node_array *);

static tl_type   *make_args_type(allocator *, ast_node *[], u16);
static tl_type   *make_labelled_args_type(allocator *, ast_node *[], char const *[], u16);
static ast_node  *find_let_node(char const *, tl_type_sized, ast_node_sized, int);
static tl_type   *get_prim(ti_inferer *, tl_type_tag);
static void       next_variable_name(rename_variables_ctx *, string_t *);
static void       reassign_typevars(void *, ast_node *);
static void       rename_array_elements(rename_variables_ctx *, ast_node **, u16);
static void       rename_if_match(allocator *, string_t *, hashmap *, string_t *);
static void       rename_variables(rename_variables_ctx *, ast_node *);
static void       specialize_node(void *, ast_node *);

static size_t     apply_one_substitution(tl_type **, tl_type *, tl_type *);
static void       assign_type_variables(void *, ast_node *);
static void       collect_constraints(void *, ast_node *);
static void       generate_tuple_function(ti_inferer *, ast_node *, ast_node_array *, hashmap **);
static int        substitute_constraints(constraint *, constraint *, constraint const);
static u32        unify_one(ti_inferer *, constraint);

static tl_type   *make_arrow(allocator *, ast_node *[], u16, char const *[], tl_type *);
static ast_node  *make_specialized(specialize_functions_ctx *, ast_node *, tl_type *);
static char      *make_specialized_name(ti_inferer *, char const *);
static ast_node  *make_tuple_constructor_function(ti_inferer *, u64, ast_node *);
static ast_node  *make_type_constructor_function(ti_inferer *, char const *, tl_type *);
static tl_type   *make_typevar(ti_inferer *);
static constraint make_constraint(tl_type *, tl_type *);

static int        is_special_name(char const *);
static int        is_special_name_s(string_t const *);
static int        is_type_compatible(tl_type const *, tl_type const *, int);

static void       dbg_ast_nodes(ti_inferer *);
static void       dbg_constraint(constraint const *);
static void       log(ti_inferer *, char const *restrict, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

ti_inferer *ti_inferer_create(allocator *alloc, ast_node_array *nodes, type_registry *type_registry) {
    ti_inferer *self                      = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena                      = arena_create(alloc, TYPE_ARENA_SIZE);
    self->transient                       = arena_create(alloc, STRINGS_ARENA_SIZE);

    self->nodes                           = nodes;
    self->type_registry                   = type_registry;

    self->constraints                     = (constraint_array){.alloc = self->type_arena};
    self->substitutions                   = (constraint_array){.alloc = self->type_arena};

    self->next_var                        = 1; // 0 is not valid
    self->next_type_var                   = 1;
    self->next_specialized                = 1;
    self->indent_level                    = 0;

    self->verbose                         = 0;
    self->constrain_function_applications = 0;
    self->unify_monotypes                 = 1;

    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **pself) {
    ti_inferer *self = *pself;

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

        ti_run_solver(self);
        size_t count = ti_apply_substitutions_to_ast(self, (constraint_sized)sized_all(self->substitutions),
                                                     (ast_node_sized)sized_all(*self->nodes));
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
    ti_generate_user_type_functions(self);
    ti_rename_variables(self);
    ti_assign_type_variables(self);

    if (self->verbose) {
        dbg("\n\nti_inferer_run: input nodes:\n");
        dbg_ast_nodes(self);
    }

    // collect constraints, but don't constrain function applications at this stage
    self->constrain_function_applications = 0;
    ti_collect_and_solve(self, 0);

    // specialize generic functions

    if (self->verbose) {
        dbg("\n\nti_inferer_run: before specialisation:\n");
        dbg_ast_nodes(self);
    }

    ast_node_array specialized = {.alloc = self->type_arena};
    ti_specialize_functions(self, &specialized);

    // add specialised nodes to program
    array_copy(*self->nodes, specialized.v, specialized.size);

    if (self->verbose) {
        dbg("ti_inferer_run: after specialization:\n");
        ti_inferer_dbg_constraints(self);
        ti_inferer_dbg_substitutions(self);
        dbg_ast_nodes(self);
    }

    // collect and solve constraints again, this time constraining
    // function applications, and repeating until there are no further
    // substitutions to be made. This is required because
    // user-type-get nodes must defer their constraints until their
    // struct types become known in a prior iteration.
    self->constrain_function_applications = 1;
    ti_collect_and_solve(self, 1);

    if (self->verbose) {
        dbg("\nti_inferer_run: final constraints:\n");
        ti_inferer_dbg_constraints(self);
        ti_inferer_dbg_substitutions(self);
        dbg_ast_nodes(self);
    }

    // any remaining constraints indicate an ill-typed program
    if (self->constraints.size) return 1;

    // create tuple constructors now that we have complete type information
    ti_generate_tuple_functions(self);

    // FIXME: one more round of specialisation: need this for
    // functions with function pointer arguments.
    specialized.size = 0;
    ti_specialize_functions(self, &specialized);
    array_copy(*self->nodes, specialized.v, specialized.size);

    return 0;
}

typedef struct {
    ti_inferer *self;
    u32         error_count;
} ti_report_errors_ctx;

static void find_error(void *ctx_, ast_node *node) {
    ti_report_errors_ctx *ctx  = ctx_;
    ti_inferer           *self = ctx->self;

    if (!tl_type_is_poly(node->type)) return;
    if (!node->file) return;

    ctx->error_count++;
    fprintf(stderr, "%s:%u: cannot infer type of %s\n", node->file, node->line,
            ast_node_to_string_for_error(self->transient, node));

    arena_reset(self->transient);
}

void ti_inferer_report_errors(ti_inferer *self) {

    // Find the deepest node with a polymorphic type and report it.
    // Exclude polymorphic functions.

    ti_report_errors_ctx ctx = {.self = self};
    for (u32 i = 0; i < self->nodes->size; ++i) {
        ast_node_dfs(&ctx, self->nodes->v[i], find_error);
    }

    dbg("error: unsatisfied constraints\n");
    ti_inferer_dbg_constraints(self);

    dbg("\ninfo: program nodes follow --\n\n");
    dbg_ast_nodes(self);
    dbg("\n-- program nodes end\n\n");
}

// -- rename variables --

static void next_variable_name(rename_variables_ctx *self, string_t *out) {
    char buf[64];
    snprintf(buf, sizeof buf, "_v%u_", self->ti->next_var++);
    *out = string_t_init(self->ti->type_arena, buf);
}

static void rename_if_match(allocator *alloc, string_t *string, hashmap *map, string_t *copy_to) {
    string_t const *found = map_get(map, string_t_str(string), (u16)string_t_size(string));

    if (found) {
        string_t_copy(alloc, copy_to, string); // preserve original name for errors
        string_t_copy(alloc, string, found);
    }
}

static void rename_variables(rename_variables_ctx *, ast_node *);

static void rename_array_elements(rename_variables_ctx *self, ast_node **elements, u16 n) {
    for (size_t i = 0; i < n; ++i) {
        ast_node const *name = elements[i];
        // parameter may be a symbol or nil
        if (ast_symbol != name->tag) break; // nil can only be sole param

        string_t var_name;
        next_variable_name(self, &var_name);

        map_set(&self->map, ast_node_name_string(name), (u16)string_t_size(&name->symbol.name), &var_name);

        // rename the actual parameter symbol
        rename_variables(self, elements[i]);
    }
}

static void rename_variables(rename_variables_ctx *self, ast_node *node) {
    if (!node) return;

    // The purpose of this operation is to rename all variables in the
    // program in order to respect lexical scoping rules and variable
    // shadowing rules. Doing this at the start of type analysis lets
    // us assume every occurence of a particular variable name must
    // have the same type.
    //
    // Non variable symbols such as struct type field names do not
    // need to participate in this transformation, because the
    // constraint solver knows how to respect constraints relating to
    // user type fields.

    switch (node->tag) {

    case ast_symbol: {
        struct ast_symbol *v = ast_node_sym(node);
        return rename_if_match(self->alloc, &v->name, self->map, &v->original);
    }

    case ast_address_of:
        //
        rename_variables(self, node->address_of.target);
        break;

    case ast_arrow:
        //
        rename_variables(self, node->arrow.left);
        rename_variables(self, node->arrow.right);
        break;

    case ast_dereference:
        //
        rename_variables(self, node->dereference.target);
        break;

    case ast_assignment: {
        struct ast_assignment *v = ast_node_assignment(node);
        rename_variables(self, v->name);
        rename_variables(self, v->value);
    } break;

    case ast_infix: {
        struct ast_infix *v = ast_node_infix(node);
        rename_variables(self, v->left);
        rename_variables(self, v->right);
    } break;

    case ast_labelled_tuple:
    case ast_tuple:          {
        // for labelled tuples, the names do not need to be renamed
        struct ast_array *v = ast_node_arr(node);
        for (size_t i = 0; i < v->n; ++i) rename_variables(self, v->nodes[i]);
    } break;

    case ast_let_in: {
        // make a new variable for this let-in subexpression and recurse,
        // but save prior value in case this is a shadowing binding.

        // first apply rename to the value portion of the expression,
        // since it is not allowed to refer to the symbol being defined.
        // But it may refer to an outer let-in binding of the same name.
        struct ast_let_in *v = ast_node_let_in(node);

        rename_variables(self, v->value);

        string_t var_name;
        next_variable_name(self, &var_name);

        hashmap *save = map_copy(self->map);
        assert(save);

        ast_node const *name = v->name;
        assert(ast_symbol == name->tag);

        map_set(&self->map, ast_node_name_string(name), (u16)string_t_size(&name->symbol.name), &var_name);

        rename_variables(self, v->name);
        rename_variables(self, v->body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_let_match_in: {
        // similar to let_in, only with multiple bindings
        struct ast_let_match_in   *v  = ast_node_let_match_in(node);
        struct ast_labelled_tuple *lt = ast_node_lt(v->lt);

        rename_variables(self, v->value); // cannot refer to bindings, so can rename before updating map

        hashmap *save = map_copy(self->map);
        assert(save);

        for (u32 i = 0; i < lt->n_assignments; ++i) {
            string_t var_name;
            next_variable_name(self, &var_name);

            ast_node const *name = lt->assignments[i]->assignment.name;
            assert(ast_symbol == name->tag);

            map_set(&self->map, ast_node_name_string(name), (u16)string_t_size(&name->symbol.name),
                    &var_name);

            rename_variables(self, lt->assignments[i]);
        }

        rename_variables(self, v->body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_let: {
        // make new variables for all function parameters. save
        // existing map in case any of them shadow.

        // Also apply renaming of variables to any type annotations.
        // The let node defines a function - if the function name is
        // annotated, it may be defining user-defined type variables.
        // Those variables need to be available for use in annotations
        // in the body of the function.
        struct ast_let   *v    = ast_node_let(node);
        struct ast_array *arr  = ast_node_arr(node);

        hashmap          *save = map_copy(self->map);
        assert(save);

        // annotations, if any:
        // FIXME
        assert(ast_symbol == v->name->tag);

        rename_array_elements(self, arr->nodes, arr->n);
        rename_variables(self, v->body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_if_then_else: {
        struct ast_if_then_else *v = ast_node_ifthen(node);
        rename_variables(self, v->condition);
        rename_variables(self, v->yes);
        rename_variables(self, v->no);
    } break;

    case ast_lambda_function: {
        // make new variable for function parameters, saving map in case of
        // shadowing.
        struct ast_lambda_function *v    = ast_node_lf(node);
        struct ast_array           *arr  = ast_node_arr(node);

        hashmap                    *save = map_copy(self->map);
        if (!save) fatal("rename_variables: map copy failed.");

        rename_array_elements(self, arr->nodes, arr->n);
        rename_variables(self, v->body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_lambda_function_application: {
        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) rename_variables(self, arr->nodes[i]);
        rename_variables(self, node->lambda_application.lambda);
    } break;

    case ast_named_function_application: {
        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) rename_variables(self, arr->nodes[i]);
        rename_variables(self, node->named_application.name);
        rename_variables(self, node->named_application.specialized);
    } break;

    case ast_begin_end: {
        struct ast_array *arr = ast_node_arr(node);
        for (size_t i = 0; i < arr->n; ++i) rename_variables(self, arr->nodes[i]);

    } break;

    case ast_user_type_get: {
        struct ast_user_type_get *v = ast_node_utg(node);
        rename_variables(self, v->struct_name);
    } break;

    case ast_user_type_set: {
        struct ast_user_type_set *v = ast_node_uts(node);
        rename_variables(self, v->struct_name);
        rename_variables(self, v->value);
    } break;

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
    case ast_user_type_definition: break;
    }
}

static void ti_rename_variables(ti_inferer *self) {
    rename_variables_ctx ctx;
    ctx.ti    = self;
    ctx.alloc = self->type_arena;
    ctx.map   = map_create(self->type_arena, sizeof(string_t));

    for (u32 i = 0; i < self->nodes->size; ++i) {
        rename_variables(&ctx, self->nodes->v[i]);
    }

    map_destroy(&ctx.map);
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
    case type_type_var:
    case type_any:            break;

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

    case ast_let: {
        struct ast_let *v = ast_node_let(node);
        buf[0]            = &v->arrow;
        buf_size          = 1;
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

    case ast_address_of:
    case ast_arrow:
    case ast_assignment:
    case ast_begin_end:
    case ast_user_type:
    case ast_dereference:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_labelled_tuple:
    case ast_tuple:
    case ast_let_in:
    case ast_let_match_in:
    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_named_function_application:  break;
    }

    for (u32 i = 0; i < subs->size; ++i) {
        ctx->count += apply_one_substitution(&node->type, subs->v[i].left, subs->v[i].right);

        for (u32 j = 0; j < buf_size; ++j)
            ctx->count += apply_one_substitution(buf[j], subs->v[i].left, subs->v[i].right);
    }
}

static size_t ti_apply_substitutions_to_ast(ti_inferer *self, constraint_sized substitutions,
                                            ast_node_sized nodes) {
    apply_substitutions_ctx ctx = {.ti = self, .substitutions = &substitutions, .count = 0};

    for (size_t i = 0; i < nodes.size; ++i) {
        ast_node_dfs(&ctx, nodes.v[i], dfs_apply_substitutions);
    }

    return ctx.count;
}

// -- solver --

static void dbg_constraint(constraint const *c) {
    int  len_left  = tl_type_snprint(null, 0, c->left) + 1;
    int  len_right = tl_type_snprint(null, 0, c->right) + 1;
    char buf_left[len_left], buf_right[len_right];
    tl_type_snprint(buf_left, len_left, c->left);
    tl_type_snprint(buf_right, len_right, c->right);
    dbg("constraint %s = %s\n", buf_left, buf_right);
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

        constraint candidate = {orig, other};

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

    // arrow types: unify matching arms
    else if (type_arrow == c.left->tag && type_arrow == c.right->tag) {
        u32 count = 0;
        count += unify_one(self, make_constraint(c.left->arrow.left, c.right->arrow.left));
        count += unify_one(self, make_constraint(c.left->arrow.right, c.right->arrow.right));
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

        for (u32 i = 0; i < self->constraints.size;) {

            constraint *item = &self->constraints.v[i];

            // delete a = a constraints, and a = any constraints
            if (item->left == item->right || tl_type_equal(item->left, item->right) ||
                item->left->tag == type_any || item->right->tag == type_any) {

                array_erase(self->constraints, i);

                // i does not increment
                continue;
            }

            // FIXME
            // consider tuples and labelled tuples equivalent if their
            // types satisfy without regard to names
            // else if (is_any_tuple(item->left) && is_any_tuple(item->right) &&
            //          tl_type_satisfies(item->left, item->right)) {

            //     array_erase(self->constraints, i);
            //     continue;
            // }

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
        for (u32 i = 0; i < self->substitutions.size; ++i) {

            if (substitute_constraints(self->constraints.v, &self->constraints.v[self->constraints.size],
                                       self->substitutions.v[i]))
                did_substitute = 1;
        }

        if (!did_substitute) break;
    }

    if (!loop_count) fatal("ti_run_solver: loop exhausted");
}

// -- assign_type_variables --

static tl_type *make_type_annotation(ti_inferer *self, ast_node *ann) {
    if (ast_symbol == ann->tag) {
        // either a prim or user type, or a generic/typevar
        tl_type **found = type_registry_find_name(self->type_registry, string_t_str(&ann->symbol.name));
        if (found) return *found;

        // unknown symbol, consider it as a typevar
        return make_typevar(self);
    }

    if (ast_tuple == ann->tag) {
        struct ast_tuple *v        = ast_node_tuple(ann);
        tl_type_array     elements = {.alloc = self->type_arena};
        array_reserve(elements, v->n_elements);

        for (u32 i = 0; i < v->n_elements; ++i) {
            tl_type *res = make_type_annotation(self, v->elements[i]);
            array_push(elements, &res);
        }

        return tl_type_create_tuple(self->type_arena, (tl_type_sized)sized_all(elements));
    }

    if (ast_arrow == ann->tag) {
        tl_type *left  = make_type_annotation(self, ann->arrow.left);
        tl_type *right = make_type_annotation(self, ann->arrow.right);
        return tl_type_create_arrow(self->type_arena, left, right);
    }

    if (ast_address_of == ann->tag) {
        tl_type *target     = make_type_annotation(self, ann->address_of.target);
        tl_type *ptr        = tl_type_create(self->type_arena, type_pointer);
        ptr->pointer.target = target;
        return ptr;
    }

    fatal("unknown annotation type: '%s'", ast_tag_to_string(ann->tag));
}

static void handle_symbol_annotation(ti_inferer *self, ast_node *node) {

    assert(ast_symbol == node->tag);

    ast_node *ann = node->symbol.annotation;
    if (ann) node->symbol.annotation_type = make_type_annotation(self, ann);
    node->type = make_typevar(self);
}

void assign_type_variables(void *ctx, ast_node *node) {
    ti_inferer *self = ctx;

    switch (node->tag) {
    case ast_let: {
        tl_type *left  = make_typevar(self);
        tl_type *right = make_typevar(self);
        tl_type *arrow = tl_type_create_arrow(self->type_arena, left, right);

        assert(node->let.name->type);
        node->let.arrow = arrow;
        node->type      = make_typevar(self);
    } break;

    case ast_lambda_function: {
        tl_type *left  = make_typevar(self);
        tl_type *right = make_typevar(self);
        tl_type *arrow = tl_type_create_arrow(self->type_arena, left, right);
        node->type     = arrow;
    } break;

    case ast_labelled_tuple:
        node->type = type_registry_ast_node_labelled_tuple(self->type_registry, node);
        break;

    case ast_tuple:
        //
        node->type = type_registry_ast_node_tuple(self->type_registry, node);
        break;

    case ast_address_of:
        node->type                 = tl_type_create(self->type_arena, type_pointer);
        node->type->pointer.target = node->address_of.target->type;
        assert(node->type->pointer.target);
        break;

    case ast_dereference: {
        struct ast_dereference *v       = ast_node_deref(node);
        v->target->type                 = tl_type_create(self->type_arena, type_pointer);
        v->target->type->pointer.target = make_typevar(self);
        node->type                      = make_typevar(self);

    } break;

    case ast_symbol:                      handle_symbol_annotation(self, node); break;

    case ast_arrow:
    case ast_assignment:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
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
        ast_node_dfs(self, self->nodes->v[i], assign_type_variables);
    }
}

// -- collect_constraints --

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

static int is_type_compatible(tl_type const *a, tl_type const *b, int strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    if (tl_type_satisfies(a, b)) return 1;
    if (strict) return 0;

    // if not strict, we are additionally satisfied when using type variables

    switch (a->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_pointer: // FIXME not sure if this is correct for pointer
        return b->tag == type_type_var;

    case type_tuple: {
        if (type_type_var == b->tag) return 1;
        else if (type_tuple != b->tag && type_labelled_tuple != b->tag) return 0;

        struct tlt_array const *va = tl_type_arr((tl_type *)a), *vb = tl_type_arr((tl_type *)b);
        if (va->elements.size != vb->elements.size) return 0;

        for (u32 i = 0; i < va->elements.size; ++i)
            if (!is_type_compatible(va->elements.v[i], vb->elements.v[i], strict)) return 0;

        return 1;
    }

    case type_labelled_tuple: {
        if (type_type_var == b->tag) return 1;

        struct tlt_array const *varr = tl_type_arr((tl_type *)a), *vbarr = tl_type_arr((tl_type *)b);
        struct tlt_labelled_tuple const *va = tl_type_lt((tl_type *)a), *vb = tl_type_lt((tl_type *)b);
        if (varr->elements.size != vbarr->elements.size) return 0;

        // regardless of typevars, names must match
        for (u32 i = 0; i < varr->elements.size; ++i) {
            if (0 != strcmp(va->names.v[i], vb->names.v[i])) return 0;
            if (!is_type_compatible(varr->elements.v[i], vbarr->elements.v[i], strict)) return 0;
        }

        return 1;
    }

    case type_arrow:
        return b->tag == type_arrow && is_type_compatible(a->arrow.left, b->arrow.left, strict) &&
               is_type_compatible(a->arrow.right, b->arrow.right, strict);

    case type_user:
        // user types are exclusively identified by reference
        return a == b;

    case type_type_var:
        // type variables match anything if not strict

    case type_any:      return 1;
    }
}

static ast_node *find_let_node(char const *name, tl_type_sized elements, ast_node_sized nodes, int strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    // TODO profile linear search versus hashmap

    if (!name) fatal("find_let_node: null search string");

    for (u32 i = 0; i < nodes.size; ++i) {
        ast_node *candidate = nodes.v[i];
        if (ast_let != candidate->tag) continue;

        if (0 != string_t_cmp_c(&candidate->let.name->symbol.name, name)) continue;

        assert(candidate->let.arrow && type_arrow == candidate->let.arrow->tag);

        assert(type_tuple == candidate->let.arrow->arrow.left->tag);

        struct tlt_array *params = tl_type_arr(candidate->let.arrow->arrow.left);
        if (elements.size != params->elements.size) continue;

        for (u32 j = 0; j < elements.size; ++j) {
            tl_type *el    = elements.v[j];
            tl_type *param = params->elements.v[j];
            // If the callsite is looking for a typevar in this slot, skip it
            if (type_type_var == el->tag) continue;
            if (!is_type_compatible(el, param, strict)) goto skip;
        }

        return candidate;

    skip:;
    }

    return null;
}

static tl_type *get_prim(ti_inferer *self, tl_type_tag tag) {
    tl_type **type = type_registry_find_name(self->type_registry, tl_type_tag_to_string(tag));
    if (!type) fatal("get_prim: failed to find '%s'", tl_type_tag_to_string(tag));

    return *type;
}

void collect_constraints(void *ctx_, ast_node *node) {
    collect_constraints_ctx *ctx  = ctx_;
    ti_inferer              *self = ctx->ti;
    constraint               c    = {0};

#define push(L, R)                                                                                         \
    do {                                                                                                   \
        assert((L) && (R));                                                                                \
        c = (constraint){(L), (R)};                                                                        \
        array_push(self->constraints, &c);                                                                 \
    } while (0)

    switch (node->tag) {
    case ast_eof:
    case ast_nil:                  push(node->type, get_prim(self, type_nil)); break;
    case ast_bool:                 push(node->type, get_prim(self, type_bool)); break;

    case ast_arrow:                // only used for annotation
    case ast_user_type_definition: break;

    case ast_symbol:               {
        char const *name_str = ast_node_name_string(node);
        u16         name_len = (u16)string_t_size(&node->symbol.name);

        // ensure every symbol usage matches its definition
        tl_type **found = map_get(ctx->symbols, name_str, name_len);
        if (found) push(node->type, *found);
        else map_set(&ctx->symbols, name_str, name_len, &node->type);

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

        if (type_user == struct_name->tag) {
            tl_type *field_type =
              tl_type_find_user_field_type(struct_name, ast_node_name_string(v->field_name));
            push(node->type, field_type);
        } else if (type_labelled_tuple == struct_name->tag) {
            struct tlt_labelled_tuple *lt = tl_type_lt(struct_name);

            // node type is the field type, find the matching name in the lt
            char const *field_name = ast_node_name_string(v->field_name);
            for (u32 i = 0; i < lt->names.size; ++i) {
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

    case ast_infix:
        // operands must be same type
        push(node->infix.left->type, node->infix.right->type);

        // result must be same type as both
        push(node->type, node->infix.right->type);
        push(node->type, node->infix.left->type);

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
                  v->value->type, ast_node_name_string(lt->assignments[i]->assignment.value));

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
        assert(node->let.arrow->tag == type_arrow);

        // left side of arrow is same as parameter tuple type
        tl_type *params = make_args_type(self->type_arena, node->array.nodes, node->array.n);
        push(node->let.arrow->arrow.left, params);

        // right side of arrow is same as function body type
        push(node->let.arrow->arrow.right, node->let.body->type);

        // function name symbol's type must match arrow
        push(node->let.arrow, node->let.name->type);

        // if name symbol has an annotation type, it must also constrain the arrow
        if (node->let.name->symbol.annotation_type)
            push(node->let.name->symbol.annotation_type, node->let.arrow);

        // result is nil
        push(node->type, get_prim(self, type_nil));

    } break;

    case ast_if_then_else:
        // yes and no arms same type
        push(node->if_then_else.yes->type, node->if_then_else.no->type);

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

        // result must be same as right hand of arrow
        push(node->type, v->right);
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

    case ast_named_function_application:
        if (!self->constrain_function_applications) return;
        else {
            struct ast_named_application *v = ast_node_named(node);
            // do not constraint c_ or std_ applications
            assert(ast_symbol == v->name->tag);
            if (is_special_name_s(&v->name->symbol.name)) return;

            // this pass happens after functions have been specialised
            ast_node *fun = v->specialized;
            if (fun) {
                // TODO syntax check phase to check for successful specialisation
                // FIXME: this should be a type inferencing error and reported to the user
                if (!fun) fatal("collect_constraints: function application is not specialised.");
                assert(fun && fun->tag == ast_let);
                assert(fun->let.arrow && fun->let.arrow->tag == type_arrow);

                tl_type *args_type = make_args_type(self->type_arena, v->arguments, v->n_arguments);

                push(fun->let.arrow->arrow.left, args_type);
                push(fun->let.arrow->arrow.right, node->type);
            } else {
                // FIXME: should we even continue if we haven't been
                // able to specialise this callsite?

                // constrain arguments against the function symbol's arrow type
                tl_type *args_type = make_args_type(self->type_arena, v->arguments, v->n_arguments);

                assert(ast_symbol == v->name->tag);
                if (type_arrow == v->name->type->tag) {
                    tl_type *left  = v->name->type->arrow.left;
                    tl_type *right = v->name->type->arrow.right;
                    push(left, args_type);
                    push(node->type, right);
                }
            }
        }

        break;
    }

#undef push
}

void ti_collect_constraints(ti_inferer *self) {
    collect_constraints_ctx ctx = {.ti = self, .symbols = map_create(self->type_arena, sizeof(tl_type *))};

    for (size_t i = 0; i < self->nodes->size; ++i)
        ast_node_dfs(&ctx, self->nodes->v[i], collect_constraints);

    map_destroy(&ctx.symbols);
}

void ti_inferer_dbg_constraints(ti_inferer const *self) {
    for (u32 i = 0; i < self->constraints.size; ++i) dbg_constraint(&self->constraints.v[i]);
}

void ti_inferer_dbg_substitutions(ti_inferer const *self) {
    dbg("substitutions count = %u\n", self->substitutions.size);
    for (u32 i = 0; i < self->substitutions.size; ++i) dbg_constraint(&self->substitutions.v[i]);
}

// -- specialize function applications --

char *make_specialized_name(ti_inferer *self, char const *name) {
#define fmt "_%s_%u_"
    int len = snprintf(null, 0, fmt, name, self->next_specialized) + 1;
    if (len < 0) fatal("make_specialized_name: failed");

    char *out = alloc_malloc(self->type_arena, (u32)len);
    snprintf(out, (u32)len, fmt, name, self->next_specialized++);
    return out;
#undef fmt
}

static void reassign_typevars(void *ctx, ast_node *node) {
    ti_inferer *ti = ctx;

    if (type_type_var != node->type->tag) return;
    node->type = make_typevar(ti);
}

static ast_node *make_specialized(specialize_functions_ctx *ctx, ast_node *src, tl_type *args) {
    allocator *alloc = ctx->ti->type_arena;

    assert(src->let.arrow);

    ast_node *special = ast_node_clone(alloc, src);
    if (null == special) fatal("specialize_node: clone failed.");

    ast_node_set_is_specialized(special);
    special->let.specialized_name = string_t_init(
      ctx->ti->type_arena, make_specialized_name(ctx->ti, string_t_str(&special->let.name->symbol.name)));

    char *str = tl_type_to_string(alloc, args);
    log(ctx->ti, "specialized '%s' for type %s", string_t_str(&special->let.name->symbol.name), str);
    alloc_free(alloc, str);

    // cloned arrow type needs to be reset
    assign_type_variables(ctx->ti, special);

    // assign new typevars to params and to body nodes that are not monotyped
    ast_node_dfs(ctx->ti, special, reassign_typevars);

    // rename variables in the specialized function, since at this
    // point every variable name must have 1-1 map to its type
    // rename variables in specialised copies
    {
        rename_variables_ctx rename;
        rename.ti    = ctx->ti;
        rename.alloc = ctx->ti->type_arena;
        rename.map   = map_create(ctx->ti->type_arena, sizeof(string_t));
        rename_variables(&rename, special);
        map_destroy(&rename.map);
    }

    return special;
}

static void specialize_node(void *ctx_, ast_node *node) {
    specialize_functions_ctx *ctx   = ctx_;
    ti_inferer               *self  = ctx->ti;

    allocator                *alloc = self->type_arena;

    if (node->tag != ast_named_function_application) return;
    if (node->named_application.specialized) return;

    char const *name = ast_node_name_string(node->named_application.name);
    if (is_special_name(name)) return;

    // does a specialised function already exist?

    tl_type *args_ty =
      make_args_type(alloc, node->named_application.arguments, node->named_application.n_arguments);
    struct tlt_tuple *vargs_ty = tl_type_tup(args_ty);

    ast_node         *let      = null;
    let = find_let_node(name, vargs_ty->elements, (ast_node_sized)sized_all(*self->nodes), 1);

    if (let) {
        // ensure its specialised status is set, because it could be a user-annotated function
        ast_node_set_is_specialized(let);
        if (string_t_empty(&let->let.specialized_name)) {
            let->let.specialized_name = string_t_init(
              self->type_arena, make_specialized_name(self, string_t_str(&let->let.name->symbol.name)));
        }
        log(self, "found specialized function '%s'", string_t_str(&let->let.specialized_name));
        node->named_application.specialized = let;
        return;
    }

    let = find_let_node(name, vargs_ty->elements, (ast_node_sized)sized_all(*self->nodes), 0);

    // TODO compiler error
    if (null == let) return;

    // specialize it and inject into callsite
    node->named_application.specialized = make_specialized(ctx, let, args_ty);

    // record the special to be added to the program
    array_push(ctx->specials, &node->named_application.specialized);
}

static void ti_specialize_functions(ti_inferer *self, ast_node_array *out_nodes) {
    specialize_functions_ctx ctx;
    ctx.ti       = self;

    ctx.specials = (ast_node_array){.alloc = self->type_arena};
    array_reserve(ctx.specials, 128);

    for (size_t i = 0; i < self->nodes->size; ++i) ast_node_dfs(&ctx, self->nodes->v[i], specialize_node);

    if (ctx.specials.size) array_shrink(ctx.specials);
    if (!ctx.specials.v) fatal("ti_specialize_functions: realloc failed.");
    *out_nodes = ctx.specials;
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
#define fmt "_gen_make_%s_"
        int len = snprintf(null, 0, fmt, name) + 1;
        if (len < 0) fatal("make_type_constructor_function: generate name failed.");
        generated_name = alloc_malloc(self->type_arena, (u32)len);
        snprintf(generated_name, (u32)len, fmt, name);
#undef fmt
    }

    ast_node *out             = ast_node_create(self->type_arena, ast_let);
    out->let.parameters       = null;
    out->let.n_parameters     = 0;
    out->let.flags            = 0;
    out->let.body             = null;
    out->let.arrow            = null;
    out->let.name             = ast_node_create_sym(self->type_arena, name);
    out->let.specialized_name = string_t_init(self->type_arena, generated_name);
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

    for (u32 i = 0; i < self->nodes->size; ++i) {
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
    for (u32 i = 0; i < added.size; ++i) {
        log(self, "generate_user_type_functions: adding %s",
            ast_node_to_string(self->transient, added.v[i]));
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
#define fmt "_gen_make_tup_%" PRIu64 "_"
        int len = snprintf(null, 0, fmt, hash) + 1;
        if (len < 0) fatal("generate name failed.");
        generated_name = alloc_malloc(self->type_arena, (u32)len);
        snprintf(generated_name, (u32)len, fmt, hash);
#undef fmt
    }

    allocator *a              = self->type_arena;
    ast_node  *out            = ast_node_create(a, ast_let);
    out->let.parameters       = null;
    out->let.n_parameters     = 0;
    out->let.flags            = 0;
    out->let.body             = null;
    out->let.arrow            = null;
    out->type                 = *type_registry_find_name(self->type_registry, "nil");
    out->let.name             = ast_node_create_sym(a, generated_name);
    out->let.specialized_name = string_t_init(a, generated_name);
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
        SET_BIT(v->flags, AST_TUPLE_FLAG_INIT);
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
        out->let.arrow =
          make_arrow(a, out->let.parameters, out->let.n_parameters, (char const **)names.v, node->type);

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
        SET_BIT(v->flags, AST_TUPLE_FLAG_INIT); // tell transpiler to emit initialisation code

        for (u16 i = 0; i < v->n_elements; ++i) {
            v->elements[i]       = out->let.parameters[i];
            v->elements[i]->type = tup->elements[i]->type;
        }

        // make an arrow type for the generated function
        out->let.arrow = make_arrow(a, out->let.parameters, out->let.n_parameters, null, node->type);
    }

    return out;
}

static void generate_tuple_function(ti_inferer *self, ast_node *node, ast_node_array *added,
                                    hashmap **map) {
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
    if (map_get(*map, &hash, sizeof hash)) return;

    ast_node *constructor = make_tuple_constructor_function(self, hash, node);

    map_set_v(map, &hash, sizeof hash, (void *)1);
    array_push(*added, &constructor);
}

static void generate_tuple_function_glue(void *ctx_, ast_node *node) {
    generate_tuple_function_ctx *ctx  = ctx_;
    ti_inferer                  *self = ctx->self;
    return generate_tuple_function(self, node, ctx->added, &ctx->map);
}

static void ti_generate_tuple_functions(ti_inferer *self) {
    // generate constructor functions for every tuple type in the program.

    ast_node_array              added = {.alloc = self->type_arena};
    generate_tuple_function_ctx ctx   = {
        .self  = self,
        .added = &added,
        .map   = map_create(self->transient, sizeof(int)),
    };

    for (u32 i = 0; i < self->nodes->size; ++i)
        ast_node_dfs(&ctx, self->nodes->v[i], generate_tuple_function_glue);

    // add nodes to program
    for (u32 i = 0; i < added.size; ++i) {
        log(self, "generate_tuple_functions: adding %s", ast_node_to_string(self->transient, added.v[i]));
        array_push(*self->nodes, &added.v[i]);
    }

    map_destroy(&ctx.map);
}

//

void log(ti_inferer *self, char const *restrict fmt, ...) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
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

#pragma clang diagnostic push
}

constraint make_constraint(tl_type *l, tl_type *r) {
    return (constraint){l, r};
}

tl_type *make_typevar(ti_inferer *self) {
    return tl_type_create_type_var(self->type_arena, self->next_type_var++);
}

static int is_special_name(char const *str) {
    return (0 == strncmp("_gen_", str, 5) || 0 == strncmp("c_", str, 2) || 0 == strncmp("std_", str, 4));
}

static int is_special_name_s(string_t const *string) {
    char const *str = string_t_str(string);
    return is_special_name(str);
}
