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
    struct ast_node  **nodes;
    u32                n_nodes;

    struct constraint *constraints;
    u32                n_constraints;
    u32                cap_constraints;

    struct constraint *substitutions;
    u32                n_substitutions;
    u32                cap_substitutions;

    bool               verbose;

    bool               unify_monotypes;
    // can be set to false to find type variables that may have
    // contradictory constraints. This will aid in reporting to the
    // user which program forms are ill-typed.

    int indent_level;
};

struct constraint {
    struct tess_type *left;
    struct tess_type *right;
};

// -- ti_inferer --

static void ti_assign_type_variables(allocator *, ast_node *[], u32);
static void ti_collect_constraints(ti_inferer *);

static void ti_apply_substitutions_to_ast(struct constraint *, u32, ast_node *[], u32);
static void ti_specialize_functions(ti_inferer *, struct ast_node ***out_nodes, u32 *out_n);
void        ti_run_solver(ti_inferer *);

static void dbg_constraint(struct constraint const *);
static void log(ti_inferer *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

ti_inferer *ti_inferer_create(allocator *alloc, struct ast_node **nodes, u32 n, allocator *nodes_alloc) {
    ti_inferer *self        = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena        = alloc_arena_create(alloc, TYPE_ARENA_SIZE);
    self->strings           = alloc_arena_create(alloc, STRINGS_ARENA_SIZE);
    self->nodes_alloc       = nodes_alloc;
    self->nodes             = nodes;
    self->n_nodes           = n;

    self->cap_constraints   = CONSTRAINTS_SIZE;
    self->cap_substitutions = CONSTRAINTS_SIZE;
    self->constraints = alloc_malloc(self->type_arena, self->cap_constraints * sizeof self->constraints[0]);
    self->substitutions =
      alloc_malloc(self->type_arena, self->cap_substitutions * sizeof self->substitutions[0]);

    self->unify_monotypes = true;

    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **self) {
    alloc_arena_destroy(alloc, &(*self)->strings);
    alloc_arena_destroy(alloc, &(*self)->type_arena);
    alloc_free(alloc, *self);
    *self = null;
}

void ti_inferer_set_verbose(ti_inferer *self, bool val) {
    self->verbose = val;
}

int ti_inferer_run(ti_inferer *self) {

    ti_assign_type_variables(self->type_arena, self->nodes, self->n_nodes);

    if (self->verbose) {
        dbg("ti_inferer_run: input nodes:\n");
        {
            for (size_t i = 0; i < self->n_nodes; ++i) {
                char *str = ast_node_to_string_for_error(self->strings, self->nodes[i]);
                dbg("%p: %s\n", self->nodes[i], str);
                alloc_free(self->strings, str);
            }
        }
    }

    // 1
    ti_collect_constraints(self);

    // 2
    ti_run_solver(self);

    // 3
    ti_apply_substitutions_to_ast(self->substitutions, self->n_substitutions, self->nodes, self->n_nodes);

    // 4: specialize

    struct ast_node **specialized   = 0;
    u32               n_specialized = 0;
    ti_specialize_functions(self, &specialized, &n_specialized);

    // 5: add specialised nodes to program

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

    for (u32 i = 0; i < subs->size; ++i)
        apply_one_substitution(&node->type, subs->buffer[i].left, subs->buffer[i].right);
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
        case type_tuple:    {

            bool found = false;
            for (size_t i = 0; i < other->n_elements; ++i) {
                if (other->elements[i] == orig) {
                    found = true;
                    break;
                }
            }

            if (found) return false;

        } break;
        case type_arrow:
            if (other->left == orig || other->right == orig) return false;
            break;
        }

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

    if (loop_count == -1) fatal("solver_run: loop exhausted");
    dbg("solver_run: exit loop_count = %i\n", loop_count);
}

// -- assign_type_variables --

struct assign_type_variables_ctx {
    allocator *alloc;
    hashmap   *symbols;
    u32        next;
};

void assign_type_variables(void *ctx_, ast_node *node) {
    struct assign_type_variables_ctx *ctx = ctx_;

    // Ensure symbols with the same name are assigned the same type
    // variable. An early phase has renamed every variable to a unique
    // name, respecting lexical scope.

    if (ast_symbol == node->tag) {

        // Is it already assigned, possibly as an ast_let name, or a
        // user-declared type during parsing?
        if (null != node->type) return;

        struct tess_type **found = map_get(ctx->symbols, mos_string_str(&node->symbol.name),
                                           (u16)mos_string_size(&node->symbol.name));
        if (found) {
            node->type = *found;
            assert(node->type->tag != type_nil);
        } else {
            struct tess_type *assign = tess_type_create_type_var(ctx->alloc, ctx->next++);
            node->type               = assign;
            map_set(&ctx->symbols, mos_string_str(&node->symbol.name),
                    (u16)mos_string_size(&node->symbol.name), &assign);

            assert(node->type->tag != type_nil);
        }
    }

    else if (ast_lambda_function == node->tag || ast_let == node->tag) {
        struct tess_type *left  = tess_type_create_type_var(ctx->alloc, ctx->next++);
        struct tess_type *right = tess_type_create_type_var(ctx->alloc, ctx->next++);
        struct tess_type *arrow = tess_type_create_arrow(ctx->alloc, left, right);
        if (ast_lambda_function == node->tag) {
            node->type = arrow;
        } else {
            node->type           = tess_type_create_type_var(ctx->alloc, ctx->next++);
            node->let.name->type = arrow;
        }
    } else {
        struct tess_type *tv = tess_type_create_type_var(ctx->alloc, ctx->next++);
        node->type           = tv;
    }
}

void ti_assign_type_variables(allocator *alloc, ast_node *nodes[], u32 count) {
    struct assign_type_variables_ctx ctx = {
      .alloc   = alloc,
      .symbols = map_create(alloc, sizeof(struct tess_type *)),
      .next    = 1, // 0 not valid
    };

    for (size_t i = 0; i < count; ++i) {
        ast_pool_dfs(&ctx, nodes[i], assign_type_variables);
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

static ast_node const *find_let_node(char const *name, u16 arity, ast_node const *nodes[], u32 count) {
    // TODO profile linear search versus hashmap

    if (!name) fatal("find_let_node: null search string");

    // TODO possibly use type registry for this: we're creating a synthetic let node whose only purpose is
    // to hold an arrow type of any -> any.
    static struct tess_type any_type       = {.tag = type_any};
    static struct tess_type nil_type       = {.tag = type_nil};
    static struct tess_type any_arrow_type = {.tag = type_arrow, .left = &any_type, .right = &any_type};
    static ast_node         sym_any        = (struct ast_node){.tag = ast_symbol, .type = &any_type};
    static ast_node         sym_any_arrow  = (struct ast_node){.tag = ast_symbol, .type = &any_arrow_type};
    static ast_node         let_any_any    = (struct ast_node){
                 .tag = ast_let, .let = {.name = &sym_any_arrow, .body = &sym_any}, .type = &nil_type};

    if (0 == strncmp("c_", name, 2) || 0 == strncmp("std_", name, 4)) return &let_any_any;

    for (u32 i = 0; i < count; ++i) {
        if (ast_let == nodes[i]->tag) {
            assert(ast_symbol == nodes[i]->let.name->tag);
            char const *node_name = mos_string_str(&nodes[i]->let.name->symbol.name);
            if (0 == strcmp(name, node_name) && arity == nodes[i]->array.n) return nodes[i];
        }
    }
    return null;
}

static ast_node const *find_typed_let_node(char const *name, struct tess_type *params[], u16 n_params,
                                           ast_node const *nodes[], u32 n_nodes) {

    if (!name) fatal("find_typed_let_node: null search string");

    if (0 == strncmp("c_", name, 2) || 0 == strncmp("std_", name, 4))
        return find_let_node(name, n_params, nodes, n_nodes);

    for (u32 i = 0; i < n_nodes; ++i) {
        if (ast_let == nodes[i]->tag) {
            assert(ast_symbol == nodes[i]->let.name->tag);
            char const *node_name = mos_string_str(&nodes[i]->let.name->symbol.name);
            if (0 == strcmp(name, node_name) && n_params == nodes[i]->array.n) {

                struct tess_type *arrow = nodes[i]->let.name->type;
                assert(type_arrow == arrow->tag);
                assert(type_tuple == arrow->left->tag);

                for (u16 j = 0; j < n_params; ++j) {
                    if (params[j] != arrow->left->elements[j]) goto mismatch;
                }

                return nodes[i];
            }
        }

    mismatch:;
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

    case ast_user_defined_type:
    case ast_symbol:            break;

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

        // result must be same type
        push(node->type, node->infix.right->type);

        break;

    case ast_let_in:
        // variable name same type as value
        push(node->let_in.name->type, node->let_in.value->type);

        // result must be same type as value
        push(node->type, node->let_in.value->type);
        break;

    case ast_let: {
        assert(node->let.name->type->tag == type_arrow);
        struct tess_type const *name = node->let.name->type;

        // left side of arrow is same as parameter tuple type
        struct tess_type *params =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->array.nodes, node->array.n);
        push(name->left, params);

        // right side of arrow is same as function body type
        push(name->right, node->let.body->type);

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

    case ast_named_function_application: {
        // must find function definition in a prior ast_let node. Look
        // for matching symbol name.
        assert(ast_symbol == node->named_application.name->tag);
        char const     *name = mos_string_str(&node->named_application.name->symbol.name);
        ast_node const *let =
          find_let_node(name, node->array.n, (ast_node const **)ctx->nodes, ctx->n_nodes);
        if (null == let)
            fatal("collect_constraints: can't find let node for function application: '%s'", name);

        assert(let->let.name->type);
        assert(let->let.body->type);

        // name must match function type
        push(node->named_application.name->type, let->let.name->type);

        // arguments must match parameters
        struct tess_type *args =
          arguments_to_tuple_type(ctx->type_arena, (ast_node const **)node->array.nodes, node->array.n);

        // consider that the function may be any -> any
        struct tess_type *params = null;

        assert(type_arrow == let->let.name->type->tag);
        if (let->let.name->type->left->tag != type_any) {
            params =
              arguments_to_tuple_type(ctx->type_arena, (ast_node const **)let->array.nodes, let->array.n);
            push(args, params);
        } else {
            push(args, let->let.name->type->left);
        }

        // result must be same as right hand of arrow
        assert(type_arrow == let->let.name->type->tag);
        push(node->type, let->let.name->type->right);

        // and result must be same as body
        push(node->type, let->let.body->type);
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

static void specialize_node(void *ctx_, ast_node *node) {
    struct specialize_functions_ctx *ctx   = ctx_;
    allocator                       *alloc = ctx->ti->type_arena;

    if (node->tag != ast_named_function_application) return;
    assert(ast_symbol == node->named_application.name->tag);

    ast_node const *name = node->named_application.name;

    // does a specialised function already exist?

    struct tess_type *args_type = arguments_to_tuple_type(
      alloc, (ast_node const **)node->named_application.arguments, node->named_application.n_arguments);

    ast_node const *let = null;

    let = find_typed_let_node(ast_node_name_string(name), args_type->elements, args_type->n_elements,
                              (ast_node const **)ctx->ti->nodes, ctx->ti->n_nodes);

    if (!let)
        let = find_let_node(mos_string_str(&name->symbol.name), node->named_application.n_arguments,
                            (ast_node const **)ctx->ti->nodes, ctx->ti->n_nodes);

    // TODO compiler error
    if (null == let) fatal("specialize_node: can't find let node for function application.");

    assert(type_arrow == name->type->tag);

    // can we unify our arrow type with the generic function?

    // what if our arrow type also includes a type variable?? How
    // would we specialize the generic function then?

    // make a specialised function and later re-run constraint solving

    ast_node *special = ast_node_clone(alloc, let);
    if (null == special) fatal("specialize_node: clone failed.");

    // set special's arrow type to the specialised args from the callsite
    assert(type_arrow == special->let.name->type->tag);
    special->let.name->type->left = args_type;

    char *str                     = tess_type_to_string(alloc, special->let.name->type);
    log(ctx->ti, "specialized '%s' for type %s", ast_node_name_string(special->let.name), str);
    alloc_free(alloc, str);
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
