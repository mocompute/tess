#include "type_inference.h"

#include "alloc.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hashmap.h"
#include "mos_string.h"
#include "tess_type.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#define TYPE_ARENA_SIZE    16 * 1024
#define STRINGS_ARENA_SIZE 4 * 1024
#define CONSTRAINTS_SIZE   1024

struct ti_inferer {
    allocator         *type_arena;
    allocator         *strings;
    allocator         *nodes_alloc;

    struct ast_node ***in_out_nodes;
    u32               *in_out_n_nodes;

    struct ast_node  **nodes;
    u32                n_nodes;

    struct constraint *constraints;
    u32                n_constraints;
    u32                cap_constraints;

    struct constraint *substitutions;
    u32                n_substitutions;
    u32                cap_substitutions;

    // for rename_variables
    u32 next_var;

    // for assign_type_variables
    u32 next_type_var;

    // for specialized function names
    u32 next_specialized;

    // for collect_constraints
    hashmap *symbols;

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

    // for log output
    int indent_level;
};

struct constraint {
    struct tess_type *left;
    struct tess_type *right;
};

// -- ti_inferer --

static void ti_rename_variables(ti_inferer *);
static void ti_assign_type_variables(ti_inferer *);
static void ti_collect_constraints(ti_inferer *);

static void ti_apply_substitutions_to_ast(struct constraint *, u32, ast_node *[], u32);
static void ti_specialize_functions(ti_inferer *, struct ast_node ***out_nodes, u32 *out_n);
void        ti_run_solver(ti_inferer *);

static void dbg_constraint(struct constraint const *);
static void log(ti_inferer *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

ti_inferer *ti_inferer_create(allocator *alloc, struct ast_node ***nodes, u32 *n, allocator *nodes_alloc) {
    ti_inferer *self        = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena        = alloc_arena_create(alloc, TYPE_ARENA_SIZE);
    self->strings           = alloc_arena_create(alloc, STRINGS_ARENA_SIZE);
    self->nodes_alloc       = nodes_alloc;
    self->in_out_nodes      = nodes;
    self->in_out_n_nodes    = n;
    self->nodes             = *nodes;
    self->n_nodes           = *n;

    self->cap_constraints   = CONSTRAINTS_SIZE;
    self->cap_substitutions = CONSTRAINTS_SIZE;
    self->constraints = alloc_malloc(self->type_arena, self->cap_constraints * sizeof self->constraints[0]);
    self->substitutions =
      alloc_malloc(self->type_arena, self->cap_substitutions * sizeof self->substitutions[0]);

    self->symbols          = map_create(self->type_arena, sizeof(struct tess_type *));

    self->unify_monotypes  = true;
    self->next_type_var    = 1; // 0 is not valid
    self->next_var         = 1; // 0 is not valid
    self->next_specialized = 1; // 0 is not valid

    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **self) {
    map_destroy(&(*self)->symbols);
    alloc_arena_destroy(alloc, &(*self)->strings);
    alloc_arena_destroy(alloc, &(*self)->type_arena);
    alloc_free(alloc, *self);
    *self = null;
}

void ti_inferer_set_verbose(ti_inferer *self, bool val) {
    self->verbose = val;
}

int ti_inferer_run(ti_inferer *self) {

    ti_rename_variables(self);

    ti_assign_type_variables(self);

    if (self->verbose) {
        dbg("\n\nti_inferer_run: input nodes:\n");
        {
            for (size_t i = 0; i < self->n_nodes; ++i) {
                char *str = ast_node_to_string_for_error(self->strings, self->nodes[i]);
                dbg("%p: %s\n", self->nodes[i], str);
                alloc_free(self->strings, str);
            }
        }
    }

    // 1. collect constraints, but don't constrain function applications at this stage
    self->constrain_function_applications = false;
    ti_collect_constraints(self);

    // 2
    ti_run_solver(self);

    // 3
    ti_apply_substitutions_to_ast(self->substitutions, self->n_substitutions, self->nodes, self->n_nodes);

    // 4: specialize

    if (self->verbose) {
        dbg("\n\nti_inferer_run: before specialisation:\n");
        {
            for (size_t i = 0; i < self->n_nodes; ++i) {
                char *str = ast_node_to_string_for_error(self->strings, self->nodes[i]);
                dbg("%p: %s\n", self->nodes[i], str);
                alloc_free(self->strings, str);
            }
        }
    }

    struct ast_node **specialized   = 0;
    u32               n_specialized = 0;
    ti_specialize_functions(self, &specialized, &n_specialized);

    // 5: add specialised nodes to program
    {
        u32 old = self->n_nodes;
        self->n_nodes += n_specialized;
        self->nodes = alloc_realloc(self->nodes_alloc, self->nodes, self->n_nodes * sizeof self->nodes[0]);
        if (!self->nodes) fatal("ti_inferer_run: realloc failed after specialization.");

        memcpy(&self->nodes[old], specialized, n_specialized * sizeof specialized[0]);

        alloc_free(self->type_arena, specialized);
        specialized = null;
    }

    if (self->verbose) {
        dbg("ti_inferer_run: after specialization:\n");
        {
            for (size_t i = 0; i < self->n_nodes; ++i) {
                char *str = ast_node_to_string(self->strings, self->nodes[i]);
                dbg("%p: %s\n", self->nodes[i], str);
                alloc_free(self->strings, str);
            }
        }
    }

    // 6. collect and solve constraints again, this time constraining function applications
    self->constrain_function_applications = true;
    ti_collect_constraints(self);

    ti_run_solver(self);
    ti_apply_substitutions_to_ast(self->substitutions, self->n_substitutions, self->nodes, self->n_nodes);

    if (self->verbose) {
        dbg("ti_inferer_run: final constraints:\n");
        ti_inferer_dbg_constraints(self);
        {
            for (size_t i = 0; i < self->n_nodes; ++i) {
                char *str = ast_node_to_string_for_error(self->strings, self->nodes[i]);
                dbg("%s\n", str);
                alloc_free(self->strings, str);
            }
        }
    }

    // update caller's nodes
    *self->in_out_nodes   = self->nodes;
    *self->in_out_n_nodes = self->n_nodes;

    // any remaining constraints indicate an ill-typed program
    if (self->n_constraints) return 1;
    return 0;
}

void ti_inferer_report_errors(ti_inferer *self) {
    dbg("error: unsatisfied constraints\n");
    ti_inferer_dbg_constraints(self);

    dbg("\ninfo: program nodes follow --\n\n");
    {
        for (size_t i = 0; i < self->n_nodes; ++i) {
            char *str = ast_node_to_string_for_error(self->strings, self->nodes[i]);
            dbg("%s\n", str);
            alloc_free(self->strings, str);
        }
    }
    dbg("\n-- program nodes end\n\n");
}

// -- rename variables --

struct rename_variables_ctx {
    ti_inferer *ti;
    allocator  *alloc;
    hashmap    *map;
};

static void next_variable_name(struct rename_variables_ctx *self, string_t *out) {
    char buf[64];
    snprintf(buf, sizeof buf, "__v%u", self->ti->next_var++);
    *out = mos_string_init(self->ti->strings, buf);
}

static void rename_if_match(allocator *alloc, string_t *string, hashmap *map, string_t *copy_to) {

    string_t const *found = map_get(map, mos_string_str(string), (u16)mos_string_size(string));

    if (found) {
        mos_string_copy(alloc, copy_to, string); // preserve original name for errors
        mos_string_copy(alloc, string, found);
    }
}

static void rename_variables(struct rename_variables_ctx *, ast_node *);

static void rename_array_elements(struct rename_variables_ctx *self, ast_node **elements, u16 n) {

    for (size_t i = 0; i < n; ++i) {
        ast_node const *name = elements[i];
        // parameter may be a symbol or nil
        if (ast_symbol != name->tag) break; // nil can only be sole param

        string_t var_name;
        next_variable_name(self, &var_name);

        map_set(&self->map, mos_string_str(&name->symbol.name), (u16)mos_string_size(&name->symbol.name),
                &var_name);

        // rename the actual parameter symbol
        rename_variables(self, elements[i]);
    }
}

static void rename_variables(struct rename_variables_ctx *self, ast_node *node) {
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

        map_set(&self->map, mos_string_str(&name->symbol.name), (u16)mos_string_size(&name->symbol.name),
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
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_user_defined_type:    break;
    }
}

static void ti_rename_variables(ti_inferer *self) {
    struct rename_variables_ctx ctx;
    ctx.ti    = self;
    ctx.alloc = self->type_arena;
    ctx.map   = map_create(self->type_arena, sizeof(string_t));

    for (u32 i = 0; i < self->n_nodes; ++i) {
        rename_variables(&ctx, self->nodes[i]);
    }

    map_destroy(&ctx.map);
}

// -- apply substitutions --

static bool apply_one_substitution(struct tess_type **type, struct tess_type *from, struct tess_type *to) {
    bool did_substitute = false;

    assert(type && *type);
    assert(from);
    assert(to);

    if (*type == from) {
        *type = to;
        return true;
    }

    switch ((*type)->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_user:
    case type_type_var:
    case type_any:      break;

    case type_tuple:    {

        for (size_t i = 0; i < (*type)->n_elements; ++i) {
            if ((*type)->elements[i] == from) {
                (*type)->elements[i] = to;
                did_substitute       = true;
            }
        }

    } break;

    case type_arrow: {
        if ((*type)->left == from) {
            (*type)->left  = to;
            did_substitute = true;
        }
        if ((*type)->right == from) {
            (*type)->right = to;
            did_substitute = true;
        }
    } break;
    }
    return did_substitute;
}

struct constraint_buffer {
    struct constraint *buffer;
    u32                size;
};

void dfs_apply_substitutions(void *ctx, ast_node *node) {
    struct constraint_buffer *subs = ctx;

    for (u32 i = 0; i < subs->size; ++i) {
        switch (node->tag) {
        case ast_let:
            apply_one_substitution(&node->type, subs->buffer[i].left, subs->buffer[i].right);
            apply_one_substitution(&node->let.arrow, subs->buffer[i].left, subs->buffer[i].right);
            break;

        case ast_user_defined_type:
            apply_one_substitution(&node->type, subs->buffer[i].left, subs->buffer[i].right);
            for (u32 j = 0; j < node->user_type.n_fields; ++j)
                apply_one_substitution(&node->user_type.field_types[j], subs->buffer[i].left,
                                       subs->buffer[i].right);
            break;

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
            apply_one_substitution(&node->type, subs->buffer[i].left, subs->buffer[i].right);
            break;
        }
    }
}

static void ti_apply_substitutions_to_ast(struct constraint *substitutions, u32 n_substitutions,
                                          ast_node *nodes[], u32 const count) {
    struct constraint_buffer ctx = {substitutions, n_substitutions};
    for (size_t i = 0; i < count; ++i) ast_pool_dfs(&ctx, nodes[i], dfs_apply_substitutions);
}

// -- solver --

static void dbg_constraint(struct constraint const *c) {

    int  len_left  = tess_type_snprint(null, 0, c->left) + 1;
    int  len_right = tess_type_snprint(null, 0, c->right) + 1;
    char buf_left[len_left], buf_right[len_right];
    tess_type_snprint(buf_left, len_left, c->left);
    tess_type_snprint(buf_right, len_right, c->right);
    dbg("constraint %s = %s\n", buf_left, buf_right);
}

static bool substitute_constraints(struct constraint *begin, struct constraint *end,
                                   struct constraint const sub) {

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

static bool unify_one(ti_inferer *self, struct constraint c) {

    if (c.left == c.right || tess_type_equal(c.left, c.right)) return false;

    else if (type_type_var == c.left->tag || type_type_var == c.right->tag) {
        struct tess_type *orig      = type_type_var == c.left->tag ? c.left : c.right;
        struct tess_type *other     = type_type_var == c.left->tag ? c.right : c.left;

        struct constraint candidate = {orig, other};

        // check conditions to rule out the candidate
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
            for (size_t i = 0; i < other->n_elements; ++i)
                if (other->elements[i] == orig) return false;

            break;
        case type_arrow:
            if (other->left == orig || other->right == orig) return false;
            break;
        }

        // If a constraint exists on the original type towards a
        // primitive type, reject this candidate.
        // FIXME: not sure about the correct logic on this question

        // for (u32 i = 0; i < self->n_constraints; i++) {
        //     if (self->constraints[i].left == orig && tess_type_is_prim(self->constraints[i].right))
        //         return false;
        // }

        // push the candidate substitution

        alloc_push_back(self->type_arena, &self->substitutions, &self->n_substitutions,
                        &self->cap_substitutions, &candidate);

    }

    // tuple constraints of equal size: unify matching elements
    else if (type_tuple == c.left->tag && type_tuple == c.right->tag &&
             c.left->n_elements == c.right->n_elements) {

        for (size_t i = 0; i < c.left->n_elements; ++i)
            unify_one(self, (struct constraint){c.left->elements[i], c.right->elements[i]});

    }

    // arrow types: unify matching arms
    else if (type_arrow == c.left->tag && type_arrow == c.right->tag) {
        unify_one(self, (struct constraint){c.left->left, c.right->left});
        unify_one(self, (struct constraint){c.left->right, c.right->right});
    }

    return true;
}

void ti_run_solver(ti_inferer *self) {
    int loop_count = 100;
    while (loop_count--) {

        bool did_substitute = false;

        for (u32 i = 0; i < self->n_constraints;) {

            struct constraint *item = &self->constraints[i];

            // delete a = a constraints, and a = any constraints
            if (item->left == item->right || tess_type_equal(item->left, item->right) ||
                item->left->tag == type_any || item->right->tag == type_any) {

                u32 len = self->n_constraints - i - 1;
                memmove(&self->constraints[i], &self->constraints[i + 1],
                        len * sizeof(self->constraints[0]));
                self->n_constraints--;

                // i does not increment
                continue;
            }

            else {

                if (unify_one(self, *item)) {
                    // iterate through remainder of constraints and substitute
                    if (substitute_constraints(&item[1], &self->constraints[self->n_constraints], *item))
                        did_substitute = true;
                }
            }

            ++i;
        }

        // apply each substitution in sequence to constraints
        for (u32 i = 0; i < self->n_substitutions; ++i) {

            if (substitute_constraints(self->constraints, &self->constraints[self->n_constraints],
                                       self->substitutions[i]))
                did_substitute = true;
        }

        if (!did_substitute) break;
    }

    if (loop_count == -1) fatal("ti_run_solver: loop exhausted");
    dbg("ti_run_solver: exit loop_count = %i\n", loop_count);
}

// -- assign_type_variables --

struct assign_type_variables_ctx {
    ti_inferer *ti;
    allocator  *alloc;
    hashmap    *symbols;
};

void assign_type_variables(void *ctx_, ast_node *node) {
    struct assign_type_variables_ctx *ctx = ctx_;

    assert(null == node->type);

    if (ast_lambda_function == node->tag || ast_let == node->tag) {
        struct tess_type *left  = tess_type_create_type_var(ctx->alloc, ctx->ti->next_type_var++);
        struct tess_type *right = tess_type_create_type_var(ctx->alloc, ctx->ti->next_type_var++);
        struct tess_type *arrow = tess_type_create_arrow(ctx->alloc, left, right);

        if (ast_lambda_function == node->tag) {
            node->type = arrow;
        } else {
            node->let.arrow = arrow;
            node->type      = tess_type_create_type_var(ctx->alloc, ctx->ti->next_type_var++);
        }

    } else {
        struct tess_type *tv = tess_type_create_type_var(ctx->alloc, ctx->ti->next_type_var++);
        node->type           = tv;
    }
}

void ti_assign_type_variables(ti_inferer *self) {
    struct assign_type_variables_ctx ctx = {
      .ti      = self,
      .alloc   = self->type_arena,
      .symbols = map_create(self->type_arena, sizeof(struct tess_type *)),
    };

    for (size_t i = 0; i < self->n_nodes; ++i) {
        ast_pool_dfs(&ctx, self->nodes[i], assign_type_variables);
    }

    map_destroy(&ctx.symbols);
}

// -- collect_constraints --

static struct tess_type *arguments_to_tuple_type(allocator *alloc, ast_node const *arguments[], u16 n) {
    struct tess_type *tuple = tess_type_create_tuple(alloc, n);
    assert(!n || tuple->elements);

    for (u16 i = 0; i < n; ++i) tuple->elements[i] = arguments[i]->type;

    return tuple;
}

static bool is_type_compatible(struct tess_type const *a, struct tess_type const *b, bool strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    switch (a->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: return (a->tag == b->tag || (!strict && b->tag == type_type_var));

    case type_tuple:
        if (!strict && type_type_var == b->tag) return true;
        else if (type_tuple != b->tag) return false;
        else {
            if (a->n_elements != b->n_elements) return false;

            for (u32 i = 0; i < a->n_elements; ++i)
                if (!is_type_compatible(a->elements[i], b->elements[i], strict)) return false;

            return true;
        }

    case type_arrow:
        return b->tag == type_arrow && is_type_compatible(a->left, b->left, strict) &&
               is_type_compatible(a->right, b->right, strict);

    case type_user:     return tess_type_equal(a, b);

    case type_type_var: return !strict;

    case type_any:      return true;
    }
}

static ast_node *find_let_node(char const *name, struct tess_type **elements, u32 n_elements,
                               ast_node *nodes[], u32 count, bool strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    // TODO profile linear search versus hashmap

    if (!name) fatal("find_let_node: null search string");

    for (u32 i = 0; i < count; ++i) {
        ast_node *candidate = nodes[i];
        if (ast_let != candidate->tag) continue;

        if (0 != strcmp(name, mos_string_str(&candidate->let.name))) continue;

        assert(candidate->let.arrow && type_arrow == candidate->let.arrow->tag);

        assert(type_tuple == candidate->let.arrow->left->tag);

        struct tess_type *params = candidate->let.arrow->left;
        if (n_elements != params->n_elements) continue;

        for (u32 j = 0; j < n_elements; ++j) {
            struct tess_type *el    = elements[j];
            struct tess_type *param = params->elements[j];
            if (!is_type_compatible(el, param, strict)) goto skip;
        }

        return candidate;

    skip:;
    }

    return null;
}

void collect_constraints(void *ctx_, ast_node *node) {
    ti_inferer       *ctx = ctx_;
    struct constraint c   = {0};

#define push(L, R)                                                                                         \
    do {                                                                                                   \
        c = (struct constraint){(L), (R)};                                                                 \
        alloc_push_back(ctx->type_arena, &ctx->constraints, &ctx->n_constraints, &ctx->cap_constraints,    \
                        &c);                                                                               \
    } while (0)

    switch (node->tag) {
    case ast_eof:
    case ast_nil:               push(node->type, tess_type_prim(type_nil)); break;
    case ast_bool:              push(node->type, tess_type_prim(type_bool)); break;

    case ast_user_defined_type: break;

    case ast_symbol:            {
        char const *name_str = mos_string_str(&node->symbol.name);
        u16         name_len = (u16)mos_string_size(&node->symbol.name);

        // ensure every symbol usage matches its definition
        struct tess_type **found = map_get(ctx->symbols, name_str, name_len);

        if (found) {
            push(node->type, *found);
        } else {
            map_set(&ctx->symbols, name_str, name_len, &node->type);
        }

    } break;

    case ast_i64:
    case ast_u64:
        push(node->type, tess_type_prim(type_int)); // TODO unsigned
        break;

    case ast_f64:    push(node->type, tess_type_prim(type_float)); break;
    case ast_string: push(node->type, tess_type_prim(type_string)); break;

    case ast_tuple:  {
        struct tess_type *els =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->array.nodes, node->array.n);

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
        struct tess_type *params =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->array.nodes, node->array.n);

        push(node->let.arrow->left, params);

        // right side of arrow is same as function body type
        push(node->let.arrow->right, node->let.body->type);

        // result is nil
        push(node->type, tess_type_prim(type_nil));
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

        struct tess_type *tup =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->array.nodes, node->array.n);
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
        struct tess_type *args =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->array.nodes, node->array.n);
        struct tess_type *params =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)lambda->array.nodes, lambda->array.n);

        push(args, params);

        // result must be same as right hand of arrow
        assert(type_arrow == lambda->type->tag);
        push(node->type, lambda->type->right);
    } break;

    case ast_named_function_application:
        if (!ctx->constrain_function_applications) return;
        else {
            // do not constraint c_ or std_ applications
            char const *name = mos_string_str(&node->named_application.name);
            if (0 == strncmp("c_", name, 2) || 0 == strncmp("std_", name, 4)) return;

            // this pass happens after functions have been specialised
            ast_node *fun = node->named_application.specialized;
            assert(fun && fun->tag == ast_let);
            assert(fun->let.arrow && fun->let.arrow->tag == type_arrow);

            struct tess_type *args_type =
              arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->named_application.arguments,
                                      node->named_application.n_arguments);
            push(fun->let.arrow->left, args_type);
            push(fun->let.arrow->right, node->type);
        }

        break;
    }

#undef push
}

void ti_collect_constraints(ti_inferer *self) {

    for (size_t i = 0; i < self->n_nodes; ++i) ast_pool_dfs(self, self->nodes[i], collect_constraints);
}

void ti_inferer_dbg_constraints(ti_inferer const *self) {
    for (u32 i = 0; i < self->n_constraints; ++i) dbg_constraint(&self->constraints[i]);
}

void ti_inferer_dbg_substitutions(ti_inferer const *self) {
    dbg("substitutions count = %u\n", self->n_substitutions);
    for (u32 i = 0; i < self->n_substitutions; ++i) dbg_constraint(&self->substitutions[i]);
}

// -- specialize function applications --

struct specialize_functions_ctx {
    ti_inferer       *ti;

    struct ast_node **specials;
    u32               n_specials;
    u32               cap_specials;
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
    node->type = tess_type_create_type_var(ti->type_arena, ti->next_type_var++);
}

static ast_node *make_specialized(struct specialize_functions_ctx *ctx, ast_node *src,
                                  struct tess_type *args) {

    allocator *alloc = ctx->ti->type_arena;

    assert(src->let.arrow);

    ast_node *special = ast_node_clone(alloc, src);
    if (null == special) fatal("specialize_node: clone failed.");

    special->let.specialized_name = mos_string_init(
      ctx->ti->type_arena, make_specialized_name(ctx->ti, mos_string_str(&special->let.name)));

    char *str = tess_type_to_string(alloc, args);
    log(ctx->ti, "specialized '%s' for type %s", mos_string_str(&special->let.name), str);
    alloc_free(alloc, str);

    // assign new typevars to params and to body nodes that are not monotyped
    ast_pool_dfs(ctx->ti, special, reassign_typevars);

    // rename variables in the specialized function, since at this
    // point every variable name must have 1-1 map to its type
    // rename variables in specialised copies
    {
        struct rename_variables_ctx rename;
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
    if (0 == strncmp("c_", name, 2) || 0 == strncmp("std_", name, 4)) return;

    // does a specialised function already exist?

    struct tess_type *args_ty = arguments_to_tuple_type(
      alloc, (ast_node const **)node->named_application.arguments, node->named_application.n_arguments);

    ast_node *let = null;
    let =
      find_let_node(name, args_ty->elements, args_ty->n_elements, ctx->ti->nodes, ctx->ti->n_nodes, true);

    if (let) {
        node->named_application.specialized = let;
        return;
    }

    let =
      find_let_node(name, args_ty->elements, args_ty->n_elements, ctx->ti->nodes, ctx->ti->n_nodes, false);

    // TODO compiler error
    if (null == let) return;

    // specialize it and inject into callsite
    node->named_application.specialized = make_specialized(ctx, let, args_ty);

    // record the special to be added to the program
    alloc_push_back(alloc, &ctx->specials, &ctx->n_specials, &ctx->cap_specials,
                    &node->named_application.specialized);
}

static void ti_specialize_functions(ti_inferer *self, struct ast_node ***out_nodes, u32 *out_n) {
    struct specialize_functions_ctx ctx;
    ctx.ti           = self;
    ctx.cap_specials = 128;
    ctx.n_specials   = 0;
    ctx.specials     = alloc_malloc(self->type_arena, ctx.cap_specials * sizeof ctx.specials[0]);

    for (size_t i = 0; i < self->n_nodes; ++i) ast_pool_dfs(&ctx, self->nodes[i], specialize_node);

    if (ctx.n_specials) ctx.specials = alloc_realloc(self->type_arena, ctx.specials, ctx.n_specials);
    if (!ctx.specials) fatal("ti_specialize_functions: realloc failed.");
    *out_nodes = ctx.specials;
    *out_n     = ctx.n_specials;
}

//

void log(ti_inferer *self, char const *restrict fmt, ...) {

    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "parser: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
