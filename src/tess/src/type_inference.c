#include "type_inference.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hashmap.h"
#include "mos_string.h"
#include "type.h"
#include "type_registry.h"

#include <assert.h>
#include <stdarg.h>
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
    allocator       *strings;

    ast_node_array  *nodes;
    type_registry   *type_registry;

    constraint_array constraints;
    constraint_array substitutions;

    u32              next_var;         // for rename_variables
    u32              next_type_var;    // for assign_type_variables
    u32              next_specialized; // for specialized function names
    hashmap         *symbols;          // for collect_constraints
    int              indent_level;     // for log output

    // flags which do not affect operation
    bool verbose;

    // flags which affect operation
    bool constrain_function_applications;
    // To implement function specialisation, the first pass of
    // collect_constraints does not constrain function applications.

    bool unify_monotypes;
    // can be set to false to find type variables that may have
    // contradictory constraints. This will aid in reporting to the
    // user which program forms are ill-typed.
};

// -- ti_inferer --

static void       ti_generate_user_type_functions(ti_inferer *);
static void       ti_rename_variables(ti_inferer *);
static void       ti_assign_type_variables(ti_inferer *);
static void       ti_collect_constraints(ti_inferer *);

static void       ti_apply_substitutions_to_ast(ti_inferer *, constraint_sized, ast_node_sized);
static void       ti_specialize_functions(ti_inferer *, ast_node_array *out_nodes);
void              ti_run_solver(ti_inferer *);

static bool       is_special_name(char const *);
static bool       is_special_name_s(string_t const *);
static constraint make_constraint(tl_type *, tl_type *);
static tl_type   *make_typevar(ti_inferer *);
static void       dbg_constraint(constraint const *);
static void       dbg_ast_nodes(ti_inferer *);
static void       log(ti_inferer *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

ti_inferer *ti_inferer_create(allocator *alloc, ast_node_array *nodes, type_registry *type_registry) {
    ti_inferer *self       = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena       = arena_create(alloc, TYPE_ARENA_SIZE);
    self->strings          = arena_create(alloc, STRINGS_ARENA_SIZE);

    self->nodes            = nodes;
    self->type_registry    = type_registry;

    self->constraints      = (constraint_array){.alloc = self->type_arena};
    self->substitutions    = (constraint_array){.alloc = self->type_arena};

    self->symbols          = map_create(self->type_arena, sizeof(tl_type *));

    self->unify_monotypes  = true;
    self->next_type_var    = 1; // 0 is not valid
    self->next_var         = 1;
    self->next_specialized = 1;

    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **pself) {
    ti_inferer *self = *pself;

    map_destroy(&self->symbols);
    arena_destroy(alloc, &self->strings);
    arena_destroy(alloc, &self->type_arena);
    alloc_free(alloc, *pself);
    *pself = null;
}

// -- operation --

void ti_inferer_set_verbose(ti_inferer *self, bool val) {
    self->verbose = val;
}

int ti_inferer_run(ti_inferer *self) {

    ti_generate_user_type_functions(self);
    ti_rename_variables(self);
    ti_assign_type_variables(self);

    if (self->verbose) {
        dbg("\n\nti_inferer_run: input nodes:\n");
        dbg_ast_nodes(self);
    }

    // 1. collect constraints, but don't constrain function applications at this stage
    self->constrain_function_applications = false;
    ti_collect_constraints(self);

    // 2
    ti_run_solver(self);

    // 3
    ti_apply_substitutions_to_ast(self, (constraint_sized)sized_all(self->substitutions),
                                  (ast_node_sized)sized_all(*self->nodes));

    // 4: specialize

    if (self->verbose) {
        dbg("\n\nti_inferer_run: before specialisation:\n");
        dbg_ast_nodes(self);
    }

    ast_node_array specialized = {.alloc = self->type_arena};
    ti_specialize_functions(self, &specialized);

    // 5: add specialised nodes to program
    array_copy(*self->nodes, specialized.v, specialized.size);

    if (self->verbose) {
        dbg("ti_inferer_run: after specialization:\n");
        dbg_ast_nodes(self);
    }

    // 6. collect and solve constraints again, this time constraining function applications
    self->constrain_function_applications = true;
    self->constraints.size                = 0; // reset constraints from first phase
    ti_collect_constraints(self);

    ti_run_solver(self);
    ti_apply_substitutions_to_ast(self, (constraint_sized)sized_all(self->substitutions),
                                  (ast_node_sized)sized_all(*self->nodes));

    if (self->verbose) {
        dbg("\nti_inferer_run: final constraints:\n");
        ti_inferer_dbg_constraints(self);
        dbg_ast_nodes(self);
    }

    // any remaining constraints indicate an ill-typed program
    if (self->constraints.size) return 1;
    return 0;
}

void ti_inferer_report_errors(ti_inferer *self) {
    dbg("error: unsatisfied constraints\n");
    ti_inferer_dbg_constraints(self);

    dbg("\ninfo: program nodes follow --\n\n");
    dbg_ast_nodes(self);
    dbg("\n-- program nodes end\n\n");
}

// -- rename variables --

typedef struct {
    ti_inferer *ti;
    allocator  *alloc;
    hashmap    *map;
} rename_variables_ctx;

static void next_variable_name(rename_variables_ctx *self, string_t *out) {
    char buf[64];
    snprintf(buf, sizeof buf, "_v%u_", self->ti->next_var++);
    *out = mos_string_init(self->ti->strings, buf);
}

static void rename_if_match(allocator *alloc, string_t *string, hashmap *map, string_t *copy_to) {

    string_t const *found = map_get(map, mos_string_str(string), (u16)mos_string_size(string));

    if (found) {
        mos_string_copy(alloc, copy_to, string); // preserve original name for errors
        mos_string_copy(alloc, string, found);
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

        map_set(&self->map, ast_node_name_string(name), (u16)mos_string_size(&name->symbol.name),
                &var_name);

        // rename the actual parameter symbol
        rename_variables(self, elements[i]);
    }
}

static void rename_variables(rename_variables_ctx *self, ast_node *node) {
    if (!node) return;

    switch (node->tag) {
    case ast_symbol:
        return rename_if_match(self->alloc, &node->symbol.name, self->map, &node->symbol.original);

    case ast_infix:
        rename_variables(self, node->infix.left);
        rename_variables(self, node->infix.right);
        break;

    case ast_tuple:
        for (size_t i = 0; i < node->array.n; ++i) rename_variables(self, node->array.nodes[i]);
        break;

    case ast_let_in: {
        // make a new variable for this let-in subexpression and recurse,
        // but save prior value in case this is a shadowing binding.

        // first apply rename to the value portion of the expression,
        // since it is not allowed to refer to the symbol being defined.
        // But it may refer to an outer let-in binding of the same name.
        rename_variables(self, node->let_in.value);

        string_t var_name;
        next_variable_name(self, &var_name);

        hashmap *save = map_copy(self->map);
        assert(save);

        ast_node const *name = node->let_in.name;
        assert(ast_symbol == name->tag);

        map_set(&self->map, ast_node_name_string(name), (u16)mos_string_size(&name->symbol.name),
                &var_name);

        rename_variables(self, node->let_in.name);
        rename_variables(self, node->let_in.body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_let: {
        // make new variables for all function parameters. save existing
        // map in case any of them shadow.

        hashmap *save = map_copy(self->map);
        assert(save);

        rename_array_elements(self, node->array.nodes, node->array.n);
        rename_variables(self, node->let.body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_if_then_else:
        rename_variables(self, node->if_then_else.condition);
        rename_variables(self, node->if_then_else.yes);
        rename_variables(self, node->if_then_else.no);
        break;

    case ast_lambda_function: {
        // make new variable for function parameters, saving map in case of
        // shadowing.

        hashmap *save = map_copy(self->map);
        if (!save) fatal("rename_variables: map copy failed.");

        rename_array_elements(self, node->array.nodes, node->array.n);
        rename_variables(self, node->lambda_function.body);

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_lambda_function_application:
    case ast_named_function_application:  {
        for (size_t i = 0; i < node->array.n; ++i) rename_variables(self, node->array.nodes[i]);

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

static bool apply_one_substitution(tl_type **ptype, tl_type *from, tl_type *to) {
    bool did_substitute = false;

    assert(ptype && *ptype);
    assert(from);
    assert(to);

    tl_type *type = *ptype;

    if (tl_type_equal(*ptype, from)) {
        *ptype = to;
        return true;
    }

    switch (type->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_user:
    case type_type_var:
    case type_any:      break;

    case type_tuple:    {
        for (size_t i = 0; i < type->elements.size; ++i)
            if (apply_one_substitution(&type->elements.v[i], from, to)) did_substitute = true;

    } break;

    case type_labelled_tuple: {
        for (size_t i = 0; i < type->fields.size; ++i)
            if (apply_one_substitution(&type->fields.v[i], from, to)) did_substitute = true;

    } break;

    case type_arrow: {
        if (apply_one_substitution(&type->left, from, to)) did_substitute = true;
        if (apply_one_substitution(&type->right, from, to)) did_substitute = true;
    } break;
    }

    return did_substitute;
}

void dfs_apply_substitutions(void *ctx, ast_node *node) {
    constraint_sized *subs = ctx;

    for (u32 i = 0; i < subs->size; ++i) {
        switch (node->tag) {
        case ast_let:
            apply_one_substitution(&node->type, subs->v[i].left, subs->v[i].right);
            apply_one_substitution(&node->let.arrow, subs->v[i].left, subs->v[i].right);
            break;

        case ast_user_type_definition:
            apply_one_substitution(&node->type, subs->v[i].left, subs->v[i].right);
            for (u32 j = 0; j < node->user_type_def.n_fields; ++j)
                apply_one_substitution(&node->user_type_def.field_types[j], subs->v[i].left,
                                       subs->v[i].right);
            break;

        case ast_user_type:
        case ast_eof:
        case ast_nil:
        case ast_bool:
        case ast_symbol:
        case ast_i64:
        case ast_u64:
        case ast_f64:
        case ast_string:
        case ast_infix:
        case ast_tuple:
        case ast_let_in:
        case ast_if_then_else:
        case ast_lambda_function:
        case ast_function_declaration:
        case ast_lambda_declaration:
        case ast_lambda_function_application:
        case ast_named_function_application:
            apply_one_substitution(&node->type, subs->v[i].left, subs->v[i].right);
            break;
        }
    }
}

static void ti_apply_substitutions_to_ast(ti_inferer *self, constraint_sized substitutions,
                                          ast_node_sized nodes) {
    constraint_sized ctx = substitutions;
    for (size_t i = 0; i < nodes.size; ++i) {
        log(self, "apply sub to %s", ast_node_to_string(self->strings, nodes.v[i]));
        ast_node_dfs(&ctx, nodes.v[i], dfs_apply_substitutions);
    }
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
        char *str = ast_node_to_string_for_error(self->strings, self->nodes->v[i]);
        dbg("%p: %s\n", self->nodes->v[i], str);
        alloc_free(self->strings, str);
    }
}

static bool substitute_constraints(constraint *begin, constraint *end, constraint const sub) {

    // When a substitution e.g 'tv1 becomes tv2' has been added to the
    // sequence of substitutions to be applied, it should also be
    // immediately applied to the rest of the constraints, so that
    // 'tv1' no longer appears in any constraint. This ensures
    // transitive constraints are satisfied. For example, tv1 = tv2,
    // tv1 = int. Without the step in this function, the constraint
    // tv1 = int would be lost.

    int did_substitute = 0;

    while (begin != end) {
        did_substitute += apply_one_substitution(&begin->left, sub.left, sub.right);
        did_substitute += apply_one_substitution(&begin->right, sub.left, sub.right);

        ++begin;
    }

    return did_substitute != 0;
}

static bool unify_one(ti_inferer *self, constraint c) {

    if (c.left == c.right || tl_type_equal(c.left, c.right)) return false;

    else if (type_type_var == c.left->tag || type_type_var == c.right->tag) {
        tl_type   *orig      = type_type_var == c.left->tag ? c.left : c.right;
        tl_type   *other     = type_type_var == c.left->tag ? c.right : c.left;

        constraint candidate = {orig, other};

        // check conditions to rule out the candidate: original must
        // not appear anywhere in the type replacing it.
        switch (other->tag) {
        case type_nil:
        case type_bool:
        case type_int:
        case type_float:
        case type_string:
        case type_user:
            if (!self->unify_monotypes) return false;
            break;

        case type_type_var:
        case type_any:      break;

        case type_tuple:
            for (size_t i = 0; i < other->elements.size; ++i)
                if (other->elements.v[i] == orig) return false;

            break;

        case type_labelled_tuple:
            for (size_t i = 0; i < other->fields.size; ++i)
                if (other->fields.v[i] == orig) return false;

            break;

        case type_arrow:
            if (other->left == orig || other->right == orig) return false;
            break;
        }

        // push the candidate substitution
        array_push(self->substitutions, &candidate);

    }

    // tuple constraints of equal size: unify matching elements
    else if (type_tuple == c.left->tag && type_tuple == c.right->tag &&
             c.left->elements.size == c.right->elements.size) {

        for (size_t i = 0; i < c.left->elements.size; ++i)
            unify_one(self, make_constraint(c.left->elements.v[i], c.right->elements.v[i]));

    }

    // arrow types: unify matching arms
    else if (type_arrow == c.left->tag && type_arrow == c.right->tag) {
        unify_one(self, make_constraint(c.left->left, c.right->left));
        unify_one(self, make_constraint(c.left->right, c.right->right));
    }

    return true;
}

void ti_run_solver(ti_inferer *self) {
    int loop_count = 100;
    while (loop_count--) {

        bool did_substitute = false;

        for (u32 i = 0; i < self->constraints.size;) {

            constraint *item = &self->constraints.v[i];

            // delete a = a constraints, and a = any constraints
            if (item->left == item->right || tl_type_equal(item->left, item->right) ||
                item->left->tag == type_any || item->right->tag == type_any) {

                array_erase(self->constraints, i);

                // i does not increment
                continue;
            }

            else {

                if (unify_one(self, *item)) {
                    // iterate through remainder of constraints and substitute
                    if (substitute_constraints(&item[1], &self->constraints.v[self->constraints.size],
                                               *item))
                        did_substitute = true;
                }
            }

            ++i;
        }

        // apply each substitution in sequence to constraints
        for (u32 i = 0; i < self->substitutions.size; ++i) {

            if (substitute_constraints(self->constraints.v, &self->constraints.v[self->constraints.size],
                                       self->substitutions.v[i]))
                did_substitute = true;
        }

        if (!did_substitute) break;
    }

    if (loop_count == -1) fatal("ti_run_solver: loop exhausted");
}

// -- assign_type_variables --

void do_assign_type_variables(ti_inferer *self, ast_node *node) {
    if (ast_lambda_function == node->tag || ast_let == node->tag) {
        tl_type *left  = make_typevar(self);
        tl_type *right = make_typevar(self);
        tl_type *arrow = tl_type_create_arrow(self->type_arena, left, right);

        if (ast_lambda_function == node->tag) {
            node->type = arrow;
        } else {
            node->let.arrow = arrow;
            node->type      = make_typevar(self);
        }

    } else {
        tl_type *tv = make_typevar(self);
        node->type  = tv;
    }
}

void assign_type_variables(void *ctx, ast_node *node) {
    return do_assign_type_variables(ctx, node);
}

void ti_assign_type_variables(ti_inferer *self) {

    for (size_t i = 0; i < self->nodes->size; ++i) {
        ast_node_dfs(self, self->nodes->v[i], assign_type_variables);
    }
}

// -- collect_constraints --

static tl_type *arguments_to_tuple_type(allocator *alloc, ast_node const *arguments[], u16 n) {

    tl_type_array types = {.alloc = alloc};
    array_reserve(types, n);
    for (u32 i = 0; i < n; ++i) array_push(types, &arguments[i]->type);

    tl_type *tuple = tl_type_create_tuple(alloc, (tl_type_sized)sized_all(types));

    return tuple;
}

static bool is_type_compatible(tl_type const *a, tl_type const *b, bool strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    if (tl_type_satisfies(a, b)) return true;
    if (strict) return false;

    // if not strict, we are additionally satisfied when using type variables

    switch (a->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: return b->tag == type_type_var;

    case type_tuple:
        if (type_type_var == b->tag) return true;
        else if (type_tuple != b->tag && type_labelled_tuple != b->tag) return false;

        if (a->elements.size != b->elements.size) return false;

        for (u32 i = 0; i < a->elements.size; ++i)
            if (!is_type_compatible(a->elements.v[i], b->elements.v[i], strict)) return false;

        return true;

    case type_labelled_tuple:
        if (type_type_var == b->tag) return true;

        if (a->elements.size != b->elements.size) return false;

        // regardless of typevars, names must match
        for (u32 i = 0; i < a->elements.size; ++i) {
            if (0 != strcmp(a->names.v[i], b->names.v[i])) return false;
            if (!is_type_compatible(a->elements.v[i], b->elements.v[i], strict)) return false;
        }

        return true;

    case type_arrow:
        return b->tag == type_arrow && is_type_compatible(a->left, b->left, strict) &&
               is_type_compatible(a->right, b->right, strict);

    case type_user:
        // user types are exclusively identified by reference
        return a == b;

    case type_type_var:
        // type variables match anything if not strict

    case type_any:      return true;
    }
}

static ast_node *find_let_node(char const *name, tl_type_sized elements, ast_node_sized nodes,
                               bool strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    // TODO profile linear search versus hashmap

    if (!name) fatal("find_let_node: null search string");

    for (u32 i = 0; i < nodes.size; ++i) {
        ast_node *candidate = nodes.v[i];
        if (ast_let != candidate->tag) continue;

        if (0 != strcmp(name, mos_string_str(&candidate->let.name))) continue;

        assert(candidate->let.arrow && type_arrow == candidate->let.arrow->tag);

        assert(type_tuple == candidate->let.arrow->left->tag);

        tl_type *params = candidate->let.arrow->left;
        if (elements.size != params->elements.size) continue;

        for (u32 j = 0; j < elements.size; ++j) {
            tl_type *el    = elements.v[j];
            tl_type *param = params->elements.v[j];
            if (!is_type_compatible(el, param, strict)) goto skip;
        }

        return candidate;

    skip:;
    }

    return null;
}

static tl_type *get_prim(ti_inferer *self, tl_type_tag tag) {

    type_entry *e = type_registry_find(self->type_registry, type_tag_to_string(tag));
    if (!e) fatal("get_prim: failed to find '%s'", type_tag_to_string(tag));

    return e->type;
}

void collect_constraints(void *ctx_, ast_node *node) {
    ti_inferer *self = ctx_;
    constraint  c    = {0};

#define push(L, R)                                                                                         \
    do {                                                                                                   \
        c = (constraint){(L), (R)};                                                                        \
        array_push(self->constraints, &c);                                                                 \
    } while (0)

    switch (node->tag) {
    case ast_eof:
    case ast_nil:                  push(node->type, get_prim(self, type_nil)); break;
    case ast_bool:                 push(node->type, get_prim(self, type_bool)); break;

    case ast_user_type_definition: break;

    case ast_symbol:               {
        char const *name_str = ast_node_name_string(node);
        u16         name_len = (u16)mos_string_size(&node->symbol.name);

        // ensure every symbol usage matches its definition
        tl_type **found = map_get(self->symbols, name_str, name_len);

        if (found) {
            push(node->type, *found);
        } else {
            map_set(&self->symbols, name_str, name_len, &node->type);
        }

    } break;

    case ast_i64:
    case ast_u64:
        push(node->type, get_prim(self, type_int)); // TODO unsigned
        break;

    case ast_f64:       push(node->type, get_prim(self, type_float)); break;
    case ast_string:    push(node->type, get_prim(self, type_string)); break;

    case ast_user_type: {

        type_entry *e = type_registry_find(self->type_registry, ast_node_name_string(node->user_type.name));
        if (!e)
            fatal("collect_constraints: failed to find type '%s'",
                  ast_node_name_string(node->user_type.name));

        push(node->type, e->type);

        // each field must be constrained to its correct type
        assert(type_user == e->type->tag);
        tl_type *lt = e->type->labelled_tuple;
        assert(node->user_type.n_fields == lt->fields.size);

        for (u16 i = 0; i < node->user_type.n_fields; ++i)
            push(node->user_type.fields[i]->type, lt->fields.v[i]);

    } break;

    case ast_tuple: {
        tl_type *els =
          arguments_to_tuple_type(self->type_arena, (ast_node const **)node->array.nodes, node->array.n);

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

        // result must be same type as value
        push(node->type, node->let_in.value->type);
        break;

    case ast_let: {
        assert(node->let.arrow->tag == type_arrow);

        // left side of arrow is same as parameter tuple type
        tl_type *params =
          arguments_to_tuple_type(self->type_arena, (ast_node const **)node->array.nodes, node->array.n);

        push(node->let.arrow->left, params);

        // right side of arrow is same as function body type
        push(node->let.arrow->right, node->let.body->type);

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
        assert(type_arrow == node->type->tag);

        tl_type *tup =
          arguments_to_tuple_type(self->type_arena, (ast_node const **)node->array.nodes, node->array.n);
        push(node->type->left, tup);

        // body type must be same as right hand of arrow
        push(node->type->right, node->lambda_function.body->type);

        // result must be same as right hand of arrow
        push(node->type, node->type->right);
        break;

    } break;

    case ast_lambda_function_application: {
        // lambda must be a ast_lambda_functoin
        ast_node const *lambda = node->lambda_application.lambda;
        assert(ast_lambda_function == lambda->tag);

        // arguments must match parameters
        tl_type *args =
          arguments_to_tuple_type(self->type_arena, (ast_node const **)node->array.nodes, node->array.n);
        tl_type *params = arguments_to_tuple_type(self->type_arena, (ast_node const **)lambda->array.nodes,
                                                  lambda->array.n);

        push(args, params);

        // result must be same as right hand of arrow
        assert(type_arrow == lambda->type->tag);
        push(node->type, lambda->type->right);
    } break;

    case ast_named_function_application:
        if (!self->constrain_function_applications) return;
        else {
            // do not constraint c_ or std_ applications
            if (is_special_name_s(&node->named_application.name)) return;

            // this pass happens after functions have been specialised
            ast_node *fun = node->named_application.specialized;
            assert(fun && fun->tag == ast_let);
            assert(fun->let.arrow && fun->let.arrow->tag == type_arrow);

            tl_type *args_type = arguments_to_tuple_type(
              self->type_arena, (ast_node const **)node->named_application.arguments,
              node->named_application.n_arguments);

            push(fun->let.arrow->left, args_type);
            push(fun->let.arrow->right, node->type);
        }

        break;
    }

#undef push
}

void ti_collect_constraints(ti_inferer *self) {
    for (size_t i = 0; i < self->nodes->size; ++i)
        ast_node_dfs(self, self->nodes->v[i], collect_constraints);
}

void ti_inferer_dbg_constraints(ti_inferer const *self) {
    for (u32 i = 0; i < self->constraints.size; ++i) dbg_constraint(&self->constraints.v[i]);
}

void ti_inferer_dbg_substitutions(ti_inferer const *self) {
    dbg("substitutions count = %u\n", self->substitutions.size);
    for (u32 i = 0; i < self->substitutions.size; ++i) dbg_constraint(&self->substitutions.v[i]);
}

// -- specialize function applications --

struct specialize_functions_ctx {
    ti_inferer    *ti;
    ast_node_array specials;
};

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

static ast_node *make_specialized(struct specialize_functions_ctx *ctx, ast_node *src, tl_type *args) {

    allocator *alloc = ctx->ti->type_arena;

    assert(src->let.arrow);

    ast_node *special = ast_node_clone(alloc, src);
    if (null == special) fatal("specialize_node: clone failed.");

    special->let.specialized_name = mos_string_init(
      ctx->ti->type_arena, make_specialized_name(ctx->ti, mos_string_str(&special->let.name)));

    char *str = tl_type_to_string(alloc, args);
    log(ctx->ti, "specialized '%s' for type %s", mos_string_str(&special->let.name), str);
    alloc_free(alloc, str);

    // cloned arrow type needs to be reset
    do_assign_type_variables(ctx->ti, special);

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
    struct specialize_functions_ctx *ctx   = ctx_;
    allocator                       *alloc = ctx->ti->type_arena;

    if (node->tag != ast_named_function_application) return;
    if (node->named_application.specialized) return;

    char const *name = mos_string_str(&node->named_application.name);
    if (is_special_name(name)) return;

    // does a specialised function already exist?

    tl_type  *args_ty = arguments_to_tuple_type(alloc, (ast_node const **)node->named_application.arguments,
                                                node->named_application.n_arguments);

    ast_node *let     = null;
    let = find_let_node(name, args_ty->elements, (ast_node_sized)sized_all(*ctx->ti->nodes), true);

    if (let) {
        node->named_application.specialized = let;
        return;
    }

    let = find_let_node(name, args_ty->elements, (ast_node_sized)sized_all(*ctx->ti->nodes), false);

    // TODO compiler error
    if (null == let) return;

    // specialize it and inject into callsite
    node->named_application.specialized = make_specialized(ctx, let, args_ty);

    // record the special to be added to the program
    array_push(ctx->specials, &node->named_application.specialized);
}

static void ti_specialize_functions(ti_inferer *self, ast_node_array *out_nodes) {
    struct specialize_functions_ctx ctx;
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

    assert(type_user == user_type->tag);

    ast_node *out             = ast_node_create(self->type_arena, ast_let);
    out->let.name             = mos_string_init(self->type_arena, name);
    out->let.specialized_name = out->let.name; // indicates this is not a generic function

    // struct {
    //     struct ast_node **fields;
    //     u16               n_fields;
    //     struct ast_node  *name;
    // } user_type; // a type literal, generated by compiler as body of constructor

    // make params array from user_type's labelled_tuple
    tl_type *lt = user_type->labelled_tuple;
    assert(type_labelled_tuple == lt->tag);
    assert(lt->fields.size == lt->names.size);
    out->let.n_parameters = (u16)lt->fields.size;
    out->let.parameters   = alloc_malloc(self->type_arena, lt->fields.size);
    for (u16 i = 0; i < out->let.n_parameters; ++i)
        out->let.parameters[i] = ast_node_create_sym(self->type_arena, lt->names.v[i]);

    // we can use lt->fields as the elements of parameters' tuple type
    tl_type *left = tl_type_create_tuple(self->type_arena, lt->fields);

    // make the arrow type
    out->let.arrow = tl_type_create_arrow(self->type_arena, left, user_type);

    // the body is a single node with the type literal
    out->let.body = ast_node_create(self->type_arena, ast_user_type);

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
    // generate required functions for user defined types: constructors, getters and setters.

    ast_node_array added = {.alloc = self->type_arena};

    for (u32 i = 0; i < self->nodes->size; ++i) {
        ast_node const *node = self->nodes->v[i];
        if (ast_user_type_definition != node->tag) continue;

        // type must be registered
        char const       *type_name = ast_node_name_string(node->user_type_def.name);
        type_entry const *te        = type_registry_find(self->type_registry, type_name);
        if (!te) fatal("generate_user_type_functions: could not find type '%s'", type_name);
        tl_type *ty = te->type;

        // c_string_cslice field_names = {0};
        // field_names =
        //   ast_nodes_get_names(self->strings, (ast_node_slice){.v   = node->user_type_def.field_names,
        //                                                       .end = node->user_type_def.n_fields});

        ast_node *constructor = make_type_constructor_function(self, type_name, ty);
        array_push(added, &constructor);
    }

    // add nodes to program
    for (u32 i = 0; i < added.size; ++i) {
        log(self, "generate_user_type_functions: adding %s", ast_node_to_string(self->strings, added.v[i]));
        array_push(*self->nodes, &added.v[i]);
    }
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

static bool is_special_name(char const *str) {
    return (0 == strncmp("_gen_", str, 5) || 0 == strncmp("c_", str, 2) || 0 == strncmp("std_", str, 4));
}

static bool is_special_name_s(string_t const *string) {
    char const *str = mos_string_str(string);
    return (0 == strncmp("_gen_", str, 5) || 0 == strncmp("c_", str, 2) || 0 == strncmp("std_", str, 4));
}
