#include "infer.h"
#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "error.h"
#include "hash.h"
#include "parser.h"
#include "str.h"
#include "type.h"

#include "ast.h"
#include "hashmap.h"

#include "type_registry.h"
#include "types.h"

#include <stdarg.h>
#include <stdio.h>

#define DEBUG_RESOLVE            0
#define DEBUG_RENAME             0
#define DEBUG_CONSTRAIN          0
#define DEBUG_EXPLICIT_TYPE_ARGS 0
#define DEBUG_INVARIANTS         1 // Enable invariant checking at phase boundaries
#define DEBUG_INSTANCE_CACHE     0 // Log instance cache hits/misses and key components
#define DEBUG_RECURSIVE_TYPES    0 // Trace mutually recursive type specialization
#define DEBUG_TYPE_ALIAS         0 // Trace type alias registration and resolution

#if DEBUG_INVARIANTS
// Forward declarations for invariant checking
struct check_types_null_ctx {
    struct tl_infer *self;
    char const      *phase;
    int              failures;
};
static void check_types_null_cb(void *ctx_ptr, ast_node *node);
static void report_invariant_failure(tl_infer *self, char const *phase, char const *invariant,
                                     char const *detail, ast_node const *node);
static int  check_specialized_nfa_type_args(tl_infer *self, ast_node *node, char const *phase);

// Helper: check if a name is alpha-converted (contains "_v" followed by digits)
static int is_alpha_converted_name(str name) {
    char const *buf = str_buf(&name);
    u32         len = str_len(name);
    for (u32 i = 0; i + 2 < len; i++) {
        if (buf[i] == '_' && buf[i + 1] == 'v' && buf[i + 2] >= '0' && buf[i + 2] <= '9') {
            return 1;
        }
    }
    return 0;
}
#endif

// Helper: unwrap a type literal to get its target type, or return the type as-is
static inline tl_monotype *unwrap_type_literal(tl_monotype *mono) {
    return mono;
}

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
    hashmap             *attributes;     // str => ast_node* attribute_set (possibly null)
    str_array            hash_includes;
    tl_infer_error_array errors;

    // Context for single-pass parsing of user type definitions
    tl_type_registry_parse_type_ctx type_parse_ctx;

    u32                             next_var_name;
    u32                             next_instantiation;

    int                             verbose;
    int                             verbose_ast;
    int                             indent_level;

    int is_constrain_ignore_error; // non-zero if no error should be reported during unification
};

typedef struct {
    u64 name_hash;
    u64 type_hash;
    u64 type_args_hash;
} name_and_type;

typedef enum {
    npos_toplevel,
    npos_formal_parameter,
    npos_function_argument,
    npos_value_rhs,    // RHS of let-in or assignment (rejects type literals)
    npos_assign_lhs,   // LHS of struct field assignment
    npos_reassign_lhs, // LHS of reassignment to let-in symbol
    npos_operand,
    npos_field_name,
} node_position;

typedef struct {
    hashmap      *lexical_names;  // exists only during traverse_ast: hset str
    hashmap      *type_arguments; // map str -> tl_monotype*
    void         *user;
    tl_monotype  *result_type; // result type of current function being traversed
    node_position node_pos;    // set by traverse_ast based on parent node
    int           is_field_name;
    int           is_annotation;
} traverse_ctx;

typedef int (*traverse_cb)(tl_infer *, traverse_ctx *, ast_node *);

typedef struct {
    hashmap *lex;
    int      is_field;
} rename_variables_ctx;

static int resolve_node(tl_infer *, ast_node *sym, traverse_ctx *ctx, node_position pos);

// ============================================================================
// Public API
// ============================================================================

static void      apply_subs_to_ast(tl_infer *);
static str       next_instantiation(tl_infer *, str);
static void      cancel_last_instantiation(tl_infer *);

static void      toplevel_add(tl_infer *, str, ast_node *);
static ast_node *toplevel_iter(tl_infer *, hashmap_iterator *);
static void      toplevel_del(tl_infer *self, str name);

static void      dbg(tl_infer const *self, char const *restrict fmt, ...);
static void      log_toplevels(tl_infer const *);
static void      log_env(tl_infer const *);
static void      log_subs(tl_infer *);

tl_infer        *tl_infer_create(allocator *alloc, tl_infer_opts const *opts) {
    tl_infer *self           = new(alloc, tl_infer);

    self->opts               = *opts;

    self->transient          = arena_create(alloc, 4096);
    self->arena              = arena_create(alloc, 16 * 1024);
    self->env                = tl_type_env_create(self->arena);
    self->subs               = tl_type_subs_create(self->arena);
    self->registry           = tl_type_registry_create(self->arena, self->transient, self->subs);

    self->synthesized_nodes  = (ast_node_array){.alloc = self->arena};

    self->toplevels          = null;
    self->instances          = map_new(self->arena, name_and_type, str, 4096);
    self->instance_names     = hset_create(self->arena, 4096);
    self->attributes         = map_new(self->arena, str, void *, 4096);
    self->hash_includes      = (str_array){.alloc = self->arena};
    self->errors             = (tl_infer_error_array){.alloc = self->arena};

    self->next_var_name      = 0;
    self->next_instantiation = 0;

    self->verbose            = 0;
    self->verbose_ast        = 0;
    self->indent_level       = 0;
    self->is_constrain_ignore_error = 0;

    tl_type_registry_parse_type_ctx_init(self->arena, &self->type_parse_ctx, null);

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    if ((*p)->toplevels) map_destroy(&(*p)->toplevels);
    if ((*p)->instances) map_destroy(&(*p)->instances);
    if ((*p)->instance_names) hset_destroy(&(*p)->instance_names);
    if ((*p)->attributes) map_destroy(&(*p)->attributes);

    arena_destroy(&(*p)->transient);
    arena_destroy(&(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose      = verbose;
    self->env->verbose = verbose;
}

void tl_infer_set_verbose_ast(tl_infer *self, int verbose) {
    self->verbose_ast = verbose;
}

static void tl_infer_set_attributes(tl_infer *self, ast_node const *sym) {
    if (!ast_node_is_symbol(sym)) return;

    str name = ast_node_str(sym);
    str_map_set_ptr(&self->attributes, name, sym->symbol.attributes);
}

tl_type_registry *tl_infer_get_registry(tl_infer *self) {
    return self->registry;
}

void tl_infer_get_arena_stats(tl_infer *self, arena_stats *out) {
    arena_get_stats(self->arena, out);
}

static int constrain(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node);

static int env_insert_constrain(tl_infer *self, str name, tl_polytype *type, ast_node const *name_node) {

    // insert type in the environment, but constrains against existing type, if any.

    tl_polytype *exist = tl_type_env_lookup(self->env, name);
    if (exist) {
        if (constrain(self, exist, type, name_node)) return 1;
    } else {
        tl_type_env_insert(self->env, name, type);
        tl_infer_set_attributes(self, name_node);
    }
    return 0;
}

static void expected_type(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
}

static void expected_tagged_union(tl_infer *self, ast_node const *node) {
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_tagged_union_expected_tagged_union, .node = node}));
}

static void wrong_number_of_arguments(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_arity, .node = node}));
}

static void tagged_union_case_syntax_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_tagged_union_case_syntax_error, .node = node}));
}

static void create_type_constructor_from_user_type(tl_infer *self, ast_node *node) {
    assert(ast_node_is_utd(node));

    tl_type_registry_parse_type_ctx_reset(&self->type_parse_ctx);
    tl_monotype *mono = tl_type_registry_parse_type_with_ctx(self->registry, node, &self->type_parse_ctx);
    if (!mono) {
        expected_type(self, node);
        return;
    }

    str name = node->user_type_def.name->symbol.name;
    tl_type_registry_insert_mono(self->registry, name, mono);
    tl_polytype *poly = tl_type_registry_get(self->registry, name);

    env_insert_constrain(self, name, poly, node->user_type_def.name);
    ast_node_type_set(node, poly);
}

static int toplevel_hash_command(tl_infer *self, ast_node *node) {
    assert(ast_node_is_hash_command(node));

    // skip #ifc .. #endc blocks
    if (ast_node_is_ifc_block(node)) return 0;

    str_sized words = node->hash_command.words;

    if (words.size < 2) {
        wrong_number_of_arguments(self, node);
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
    } else if (str_eq(words.v[0], S("module_prelude"))) {
        return 0;
    } else {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_unknown_hash_command, .node = node}));
        return 1;
    }
}

static void specialize_type_alias(tl_infer *, ast_node *);

static void load_toplevel(tl_infer *self, ast_node_sized nodes) {
    // Types of toplevel nodes (see parser.c/toplevel())
    //
    // - struct/union type definition (utd, user_type_def)
    // - enum type definition (utd)
    // - function definition (let node)
    // - global value definition (let-in node)
    // - forward function declaration `(p1, p2, ...) -> r` (symbol with an arrow annotation)
    // - symbol annotation `sym : Type`
    // - type alias
    // - c chunks and hash directives, not processed here
    //
    // If the same symbol is seen more than once, it is usually an error. The exception is forward function
    // declarations.

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
                    resolve_node(self, (*p)->let.name, null, npos_toplevel);
                }
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&self->toplevels, name_str, &node);
                    resolve_node(self, node, null, npos_toplevel);
                }
            }
        }

        else if (ast_node_is_type_alias(node)) {
            // FIXME: assmues alias name is a symbol. Parser may produce an nfa.
            if (!ast_node_is_symbol(node->type_alias.name)) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_expected_type_alias_symbol, .node = node}));
                continue;
            }

            str          name = toplevel_name(node);
            tl_monotype *mono = tl_type_registry_parse_type(self->registry, node->type_alias.target);
            tl_polytype *poly = tl_monotype_generalize(self->arena, mono);
            if (1) {
                str poly_str = tl_polytype_to_string(self->transient, poly);
                dbg(self, "type_alias: %s = %s", str_cstr(&name), str_cstr(&poly_str));
            }
            tl_type_registry_type_alias_insert(self->registry, name, poly);
#if DEBUG_TYPE_ALIAS
            {
                tl_polytype *env_type = tl_type_env_lookup(self->env, name);
                str          poly_dbg = tl_polytype_to_string(self->transient, poly);
                fprintf(stderr, "[DEBUG_TYPE_ALIAS] load_toplevel: alias '%s' = %s\n",
                        str_cstr(&name), str_cstr(&poly_dbg));
                fprintf(stderr, "[DEBUG_TYPE_ALIAS]   in registry=YES, in env=%s\n",
                        env_type ? "YES" : "NO");
            }
#endif
            specialize_type_alias(self, node);
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

                if (node->let.name->symbol.annotation) {
                    resolve_node(self, node->let.name, null, npos_toplevel);
                } else {
                    // otherwise merge in the prior annotation
                    node->let.name->symbol.annotation      = (*p)->symbol.annotation;
                    node->let.name->symbol.annotation_type = (*p)->symbol.annotation_type;
                }

                // copy attributes over
                if ((*p)->symbol.attributes) {
                    // reject attributes on current symbol if they exist
                    if (node->let.name->symbol.attributes) {
                        array_push(self->errors,
                                   ((tl_infer_error){.tag = tl_err_attributes_exist, .node = node}));
                        continue;
                    }

                    node->let.name->symbol.attributes = (*p)->symbol.attributes;
                }

                // copy parameter annotations over
                if ((*p)->symbol.annotation) {
                    // The annotation is an AST arrow, which includes param annotations, if any. These are
                    // important to copy over, because they may declare type arguments.
                    ast_node *ast_arrow = (*p)->symbol.annotation;
                    assert(ast_node_is_arrow(ast_arrow));
                    ast_node *ast_param_tuple = ast_arrow->arrow.left;
                    assert(ast_node_is_tuple(ast_param_tuple));

                    tl_polytype *arrow = (*p)->symbol.annotation_type;
                    assert(arrow && tl_monotype_is_arrow(arrow->type));
                    tl_monotype *param_tuple = arrow->type->list.xs.v[0];
                    assert(tl_tuple == param_tuple->tag);
                    ast_arguments_iter iter = ast_node_arguments_iter(node);
                    ast_node          *arg;
                    u32                i = 0;
                    while ((arg = ast_arguments_next(&iter))) {
                        if (i >= param_tuple->list.xs.size) fatal("runtime error");
                        if (i >= ast_param_tuple->tuple.n_elements) fatal("runtime error");

                        if (!ast_node_is_symbol(arg)) goto next;

                        // Do not overwrite let node's annotated parameters
                        if (arg->symbol.annotation) goto next;

                        arg->symbol.annotation = ast_param_tuple->tuple.elements[i];
                        arg->symbol.annotation_type =
                          tl_polytype_absorb_mono(self->arena, param_tuple->list.xs.v[i]);

                    next:
                        i++;
                    }
                }

                // replace prior symbol entry with let node
                *p = node;
            } else {
                str_map_set(&self->toplevels, name_str, &node);
                resolve_node(self, node->let.name, null, npos_toplevel);
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
#if DEBUG_EXPLICIT_TYPE_ARGS
                fprintf(stderr, "[DEBUG UTD] Added to toplevels (%p): '%s' (len=%zu, hash=%llu) -> %p\n",
                        (void *)self->toplevels, str_cstr(&name_str), str_len(name_str),
                        (unsigned long long)str_hash64(name_str), (void *)node);
#endif
            }
        }

        else if (ast_node_is_let_in(node)) {
            str name_str = node->let_in.name->symbol.name;
            str_map_set(&self->toplevels, name_str, &node);
            resolve_node(self, node->let_in.name, null, npos_toplevel);
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
    hashmap  *names;  // str set
    hashmap  *recurs; // str set
} tree_shake_ctx;

hashmap *tree_shake(tl_infer *, ast_node const *);
void     do_tree_shake(void *, ast_node *);

// Helper for tree shaking value bindings (let_in and reassignment)
static void tree_shake_value_binding(tree_shake_ctx *ctx, ast_node *value) {
    if (!value) return;

    tl_infer *self = ctx->self;

    if (ast_node_is_symbol(value)) {
        str name = ast_node_str(value);

        // if it is a toplevel, recurse through it
        ast_node *next = toplevel_get(self, name);
        if (next) ast_node_dfs(ctx, next, do_tree_shake);
        str_hset_insert(&ctx->recurs, name);
        str_hset_insert(&ctx->names, name);
    } else {
        // recurse into value
        ast_node_dfs(ctx, value, do_tree_shake);
    }
}

void do_tree_shake(void *ctx_, ast_node *node) {
    tree_shake_ctx *ctx  = ctx_;
    tl_infer       *self = ctx->self;

    if (ast_node_is_nfa(node)) {
        str name = toplevel_name(node);

        str_hset_insert(&ctx->names, name);

        // add all symbol arguments because they could be function pointers
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (ast_node_is_assignment(arg)) arg = arg->assignment.value;
            if (!ast_node_is_symbol(arg)) continue;
            if (str_eq(name, arg->symbol.name)) continue;
            if (!str_hset_contains(ctx->recurs, arg->symbol.name)) {
                str_hset_insert(&ctx->recurs, arg->symbol.name);

                // if it is a toplevel, recurse through it
                ast_node *next = toplevel_get(self, arg->symbol.name);
                if (next) {
                    ast_node_dfs(ctx, next, do_tree_shake);

                    // and save the name
                    str_hset_insert(&ctx->names, arg->symbol.name);
                }
            }
        }

        if (!str_hset_contains(ctx->recurs, name)) {
            str_hset_insert(&ctx->recurs, name);

            ast_node *next = toplevel_get(ctx->self, name);
            if (next) {
                ast_node_dfs(ctx, next, do_tree_shake);
            }

            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_node_is_let_in(node)) {
        tree_shake_value_binding(ctx, node->let_in.value);

        // the let-in name
        str name = ast_node_str(node->let_in.name);
        str_hset_insert(&ctx->names, name);
    } else if (ast_node_is_reassignment(node)) {
        tree_shake_value_binding(ctx, node->assignment.value);
    }

    else if (ast_node_is_let(node) || ast_node_is_lambda_function(node)) {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(param)) continue;
            str name = ast_node_str(param);
            // dbg(self, "do_tree_shake: adding '%s'", str_cstr(&name));
            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_case == node->tag && node->case_.binary_predicate) {
        ast_node *pred = node->case_.binary_predicate;
        if (ast_node_is_symbol(pred)) {
            str name = ast_node_str(pred);
            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_node_is_symbol(node)) {
        // Handle bare symbols that may reference toplevel functions (e.g., function pointers in case arms)
        str       name = ast_node_str(node);
        ast_node *next = toplevel_get(self, name);
        if (next && !str_hset_contains(ctx->recurs, name)) {
            str_hset_insert(&ctx->recurs, name);
            ast_node_dfs(ctx, next, do_tree_shake);
            str_hset_insert(&ctx->names, name);
        }
    }
}

hashmap *tree_shake(tl_infer *self, ast_node const *node) {
    tree_shake_ctx ctx = {.self = self};
    ctx.names          = hset_create(self->transient, 1024);
    ctx.recurs         = hset_create(self->transient, 1024);

    str_hset_insert(&ctx.names, toplevel_name(node));

    ast_node_dfs(&ctx, (ast_node *)node, do_tree_shake);

    return ctx.names;
}

// -- inference --

static traverse_ctx *traverse_ctx_create(allocator *transient) {
    // Use a transient allocator because the destroy function leaks the maps.
    traverse_ctx *out   = new(transient, traverse_ctx);
    out->lexical_names  = hset_create(transient, 64);
    out->type_arguments = map_create_ptr(transient, 64);
    out->user           = null;
    out->result_type    = null;
    out->node_pos       = npos_operand;
    out->is_field_name  = 0;
    out->is_annotation  = 0;

    return out;
}

static void traverse_ctx_load_type_arguments(tl_infer *self, traverse_ctx *ctx, ast_node const *node) {
    // read type arguments out of ast node
    if (ast_node_is_let(node)) {
        for (u32 i = 0; i < node->let.n_type_parameters; i++) {
            ast_node *type_param = node->let.type_parameters[i];
            assert(ast_node_is_symbol(type_param));

#if DEBUG_INVARIANTS
            // Invariant: Type parameter names must be alpha-converted
            if (!is_alpha_converted_name(type_param->symbol.name)) {
                char detail[256];
                snprintf(detail, sizeof detail, "Type parameter '%.*s' is not alpha-converted",
                         str_ilen(type_param->symbol.name), str_buf(&type_param->symbol.name));
                report_invariant_failure(self, "traverse_ctx_load_type_arguments",
                                         "Type parameter name must be alpha-converted", detail, type_param);
            }

            // Invariant: No duplicate type parameter names in ctx->type_arguments
            if (str_map_contains(ctx->type_arguments, type_param->symbol.name)) {
                char detail[256];
                snprintf(detail, sizeof detail, "Duplicate type parameter name '%.*s'",
                         str_ilen(type_param->symbol.name), str_buf(&type_param->symbol.name));
                report_invariant_failure(self, "traverse_ctx_load_type_arguments",
                                         "No duplicate type parameter names allowed", detail, type_param);
            }
#endif

            // If the type parameter already has a type (set by concretize_params during
            // specialization, or by a previous traversal), use that instead of creating a fresh
            // type variable.
            if (type_param->type) {
                tl_monotype *mono = type_param->type->type;
#if DEBUG_EXPLICIT_TYPE_ARGS
                str mono_str = tl_monotype_to_string(self->transient, mono);
                fprintf(stderr, "[DEBUG LOAD TYPE ARGS] '%s' type_param '%s' has existing type: %s\n",
                        str_cstr(&node->let.name->symbol.name), str_cstr(&type_param->symbol.name),
                        str_cstr(&mono_str));
#endif

#if DEBUG_INVARIANTS
                // Invariant: When loading a type param for a specialized function,
                // if its type variable is already bound to a concrete type in substitutions,
                // that indicates the specialized function is being reused - potential pollution.
                // Note: This is informational - not all reuse is bad, but it helps debug pollution.
                if (tl_monotype_is_tv(mono) && ast_node_is_specialized(node)) {
                    tl_monotype *substituted = tl_monotype_clone(self->transient, mono);
                    tl_monotype_substitute(self->transient, substituted, self->subs, null);
                    if (tl_monotype_is_concrete(substituted) && !tl_monotype_is_tv(substituted)) {
                        char detail[512];
                        str  func_name = ast_node_str(node->let.name);
                        str  type_str  = tl_monotype_to_string(self->transient, substituted);
                        snprintf(detail, sizeof detail,
                                 "Specialized function '%s' type param '%s' already bound to '%s'",
                                 str_cstr(&func_name), str_cstr(&type_param->symbol.name),
                                 str_cstr(&type_str));
                        report_invariant_failure(
                          self, "traverse_ctx_load_type_arguments",
                          "Specialized function type param already bound (reuse detected)", detail,
                          type_param);
                    }
                }
#endif

                str param_name = type_param->symbol.name;

                tl_type_registry_add_type_argument(self->registry, param_name, mono, &ctx->type_arguments);
                assert(str_map_contains(ctx->type_arguments, param_name));

                // param names are alpha converted, and we use the environment to ensure constraints are
                // fully propagated
                tl_polytype *poly = tl_polytype_absorb_mono(self->arena, mono);
                env_insert_constrain(self, param_name, poly, type_param);

            } else {

#if DEBUG_EXPLICIT_TYPE_ARGS
                fprintf(
                  stderr,
                  "[DEBUG LOAD TYPE ARGS] '%s' type_param '%s' has NO existing type, creating fresh\n",
                  str_cstr(&node->let.name->symbol.name), str_cstr(&type_param->symbol.name));
#endif
                tl_monotype *mono = tl_type_registry_add_fresh_type_argument(
                  self->registry, type_param->symbol.name, &ctx->type_arguments);

                ast_node_type_set(type_param, tl_polytype_absorb_mono(self->arena, mono));

                assert(str_map_contains(ctx->type_arguments, type_param->symbol.name));
            }
        }
    }
}

// Forward declaration for explicit type argument specialization
static str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args,
                                       tl_polytype **out_type);

static int traverse_ctx_assign_type_arguments(tl_infer *self, traverse_ctx *ctx, ast_node const *node) {
    if (ast_node_is_nfa(node)) {
        if (!node->named_application.is_specialized) return 0;

        u32 argc = node->named_application.n_type_arguments;
        if (argc == 0) return 0;
        ast_node **argv   = node->named_application.type_arguments;

        ast_node  *let    = toplevel_get(self, ast_node_str(node->named_application.name));
        u32        paramc = (let && ast_node_is_let(let)) ? let->let.n_type_parameters : 0;

#if DEBUG_INVARIANTS
        // Invariant: Type argument count must match type parameter count
        if (argc > 0 && paramc > 0 && argc != paramc) {
            str  callee_name = ast_node_str(node->named_application.name);
            char detail[256];
            snprintf(detail, sizeof detail,
                     "Call site has %u type arguments but function '%.*s' has %u type parameters", argc,
                     str_ilen(callee_name), str_buf(&callee_name), paramc);
            report_invariant_failure(self, "traverse_ctx_assign_type_arguments",
                                     "Type argument count must match type parameter count", detail,
                                     (ast_node *)node);
        }
#endif

#if DEBUG_EXPLICIT_TYPE_ARGS
        str name = ast_node_str(node->named_application.name);
        fprintf(stderr, "[DEBUG EXPLICIT TYPE ARGS] traverse_ctx_assign_type_arguments:\n");
        fprintf(stderr, "  callee: %s\n", str_cstr(&name));
        fprintf(stderr, "  n_type_arguments: %u, n_type_parameters: %u\n", argc, paramc);
        fprintf(stderr, "  type_arguments contains: %i\n", str_map_contains(ctx->type_arguments, name));
#endif

        tl_type_registry_parse_type_ctx parse_ctx;
        tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, ctx->type_arguments);

        for (u32 i = 0; i < argc; i++) {
            ast_node *type_arg_node = argv[i];

#if DEBUG_EXPLICIT_TYPE_ARGS
            fprintf(stderr, "  type_arg[%u] AST tag=%d", i, type_arg_node->tag);
            if (ast_node_is_symbol(type_arg_node)) {
                str n = type_arg_node->symbol.name;
                fprintf(stderr, " name='%s'", str_cstr(&n));
                fprintf(stderr, " type_arguments contains: %i", str_map_contains(ctx->type_arguments, n));

            } else if (ast_node_is_nfa(type_arg_node)) {
                str n = ast_node_str(type_arg_node->named_application.name);
                fprintf(stderr, " nfa='%s' n_type_args=%u", str_cstr(&n),
                        type_arg_node->named_application.n_type_arguments);
            }
            if (type_arg_node->type) {
                str t = tl_polytype_to_string(self->transient, type_arg_node->type);
                fprintf(stderr, " type=%s", str_cstr(&t));
            }
            fprintf(stderr, "\n");
#endif

            tl_monotype *parsed = null;

            // If the type argument node already has a type set (from a previous pass), reuse it.
            // This happens when multiple calls within the same specialized function share type
            // argument AST nodes (e.g., sizeof[T]() and alignof[T]() both reference the same T).
            // We don't require the type to be concrete - type variables are valid and will be
            // unified later.
            if (type_arg_node->type) { // Re-enabled: use existing type on node
                parsed = type_arg_node->type->type;

#if DEBUG_EXPLICIT_TYPE_ARGS
                str reused_str = tl_monotype_to_string(self->transient, parsed);
                fprintf(stderr, "  type_arg[%u]: reused existing type = %s\n", i, str_cstr(&reused_str));
#endif
            } else {
#if DEBUG_EXPLICIT_TYPE_ARGS
                // Debug: show what's in the parse context
                str_array keys = str_map_keys(self->transient, parse_ctx.type_arguments);
                fprintf(stderr, "  parse_ctx.type_arguments has %u keys:", keys.size);
                forall(j, keys) fprintf(stderr, " '%s'", str_cstr(&keys.v[j]));
                fprintf(stderr, "\n");
#endif
                parsed = tl_type_registry_parse_type_with_ctx(self->registry, type_arg_node, &parse_ctx);
                if (!parsed) {
                    fatal("could not parse type"); // FIXME better error
                }
            }

#if DEBUG_EXPLICIT_TYPE_ARGS
            str parsed_str = tl_monotype_to_string(self->transient, parsed);
            fprintf(stderr, "  type_arg[%u]: parsed = %s\n", i, str_cstr(&parsed_str));
#endif

            // If the type argument is a type constructor instance with arguments, specialize it.
            // This is an exception to the normal design where specialization happens in
            // specialize_applications_cb. We must do it here because intrinsics (like
            // _tl_sizeof_) are skipped by specialize_applications_cb, so their explicit
            // type arguments would never be specialized otherwise.
            if (tl_monotype_is_inst(parsed) && parsed->cons_inst->args.size > 0) {
                tl_polytype *specialized = null;
                (void)specialize_type_constructor(self, parsed->cons_inst->def->generic_name,
                                                  parsed->cons_inst->args, &specialized);
                if (specialized && tl_monotype_is_inst_specialized(specialized->type)) {
                    parsed = specialized->type;
                }

#if DEBUG_INVARIANTS
                // Invariant: Type constructor instances in explicit type arguments must be specialized
                // After attempting specialization, verify the result is properly specialized.
                // If parsed is still an unspecialized type constructor instance, specialization failed.
                if (tl_monotype_is_inst(parsed) && !tl_monotype_is_inst_specialized(parsed)) {
                    char detail[256];
                    str  type_str = tl_monotype_to_string(self->transient, parsed);
                    str  callee   = ast_node_str(node->named_application.name);
                    snprintf(detail, sizeof detail,
                             "Type argument %u '%s' in call to '%.*s' was not specialized", i,
                             str_cstr(&type_str), str_ilen(callee), str_buf(&callee));
                    report_invariant_failure(self, "traverse_ctx_assign_type_arguments",
                                             "Type constructor instances in explicit type args must be "
                                             "specialized",
                                             detail, (ast_node *)node);
                }
#endif
            }

            // If the callee has a matching type parameter, add to the type argument context
            if (i < paramc) {
                assert(ast_node_is_symbol(let->let.type_parameters[i]));
                // Always use the alpha-converted name, not the original, because the type
                // environment relies on alpha conversion to prevent pollution between generic
                // and specialized phases.
                str param_name = let->let.type_parameters[i]->symbol.name;

#if DEBUG_EXPLICIT_TYPE_ARGS
                str parsed_str = tl_monotype_to_string(self->transient, parsed);
                fprintf(stderr, "  mapping type param '%s' -> %s\n", str_cstr(&param_name),
                        str_cstr(&parsed_str));
#endif

#if DEBUG_INVARIANTS
                // Invariant: If type parameter already has a binding, it must be the same type
                // Type pollution occurs when the same alpha-converted name gets different types
                // in different specialization contexts
                tl_monotype *existing_binding = str_map_get_ptr(ctx->type_arguments, param_name);
                if (existing_binding && existing_binding != parsed) {
                    char detail[512];
                    str  existing_str = tl_monotype_to_string(self->transient, existing_binding);
                    str  new_str      = tl_monotype_to_string(self->transient, parsed);
                    snprintf(detail, sizeof detail,
                             "Type parameter '%.*s' already bound to '%s', cannot rebind to '%s'",
                             str_ilen(param_name), str_buf(&param_name), str_cstr(&existing_str),
                             str_cstr(&new_str));
                    report_invariant_failure(self, "traverse_ctx_assign_type_arguments",
                                             "Type parameter binding conflict (type pollution)", detail,
                                             (ast_node *)node);
                }
#endif

                tl_type_registry_add_type_argument(self->registry, param_name, parsed,
                                                   &ctx->type_arguments);

                // param names are alpha converted, and we use the environment to ensure constraints are
                // fully propagated
                tl_polytype *poly = tl_polytype_absorb_mono(self->arena, parsed);
                env_insert_constrain(self, param_name, poly, argv[i]);

                assert(str_map_contains(ctx->type_arguments, param_name));
            }

            // Set type on the type argument AST node for the transpiler.
            ast_node_type_set((ast_node *)node->named_application.type_arguments[i],
                              tl_polytype_absorb_mono(self->arena, parsed));
        }
    }

    return 0;
}

static int traverse_ctx_is_param(traverse_ctx *self, str name) {
    return str_hset_contains(self->lexical_names, name);
}

static int type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_error, .node = node}));
    return 1;
}
static int unresolved_type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_unresolved_type, .node = node}));
    return 1;
}

static void log_constraint(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
static void log_constraint_mono(tl_infer *, tl_monotype *, tl_monotype *, ast_node const *);
static void log_type_error(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
static void log_type_error_mm(tl_infer *, tl_monotype *, tl_monotype *, ast_node const *);

static int  is_std_function(ast_node *);

typedef struct {
    tl_infer       *self;
    ast_node const *node;
} type_error_cb_ctx;

static void type_error_cb(void *ctx_, tl_monotype *left, tl_monotype *right) {
    type_error_cb_ctx *ctx = ctx_;
    if (!ctx->self->is_constrain_ignore_error) {
        log_type_error_mm(ctx->self, left, right, ctx->node);
        type_error(ctx->self, ctx->node);
    }
}

static int constrain_mono(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    type_error_cb_ctx error_ctx = {.self = self, .node = node};

#if DEBUG_CONSTRAIN
    {
        str left_str  = tl_monotype_to_string(self->transient, left);
        str right_str = tl_monotype_to_string(self->transient, right);
        fprintf(stderr, "[DEBUG CONSTRAIN] constrain_mono: %s <=> %s  (at %s:%d)\n", str_cstr(&left_str),
                str_cstr(&right_str), node->file, node->line);
    }
#endif

#if DEBUG_INVARIANTS
    // Invariant: should never constrain two different concrete cons_inst types directly
    // (excluding integer-compatible pairs). If this fires, a type variable was already
    // bound to the wrong type upstream.
    if (tl_monotype_is_inst(left) && tl_monotype_is_concrete(left) && tl_monotype_is_inst(right) &&
        tl_monotype_is_concrete(right) && !tl_monotype_is_integer_convertible(left) &&
        !tl_monotype_is_integer_convertible(right)) {
        if (!str_eq(left->cons_inst->def->name, right->cons_inst->def->name) &&
            !str_eq(left->cons_inst->def->generic_name, right->cons_inst->def->generic_name)) {
            char detail[512];
            str  ls = tl_monotype_to_string(self->transient, left);
            str  rs = tl_monotype_to_string(self->transient, right);
            snprintf(detail, sizeof detail,
                     "Constraining two different concrete type constructors: %s vs %s (left@%p right@%p)",
                     str_cstr(&ls), str_cstr(&rs), (void *)left, (void *)right);
            report_invariant_failure(self, "constrain_mono",
                                     "Should never constrain two different concrete type constructors",
                                     detail, node);
        }
    }
#endif

    hashmap *seen = hset_create(self->transient, 64);
    int      res  = tl_type_subs_unify_mono(self->subs, left, right, type_error_cb, &error_ctx, &seen);

#if DEBUG_CONSTRAIN
    if (res) {
        fprintf(stderr, "[DEBUG CONSTRAIN] constrain_mono: UNIFICATION FAILED\n");
    }
#endif

    return res;
}

static int constrain(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    if (left == right) return 0;
    if (0) {
        log_constraint(self, left, right, node);
    }

    tl_monotype *lhs = null, *rhs = null;

    if (left->quantifiers.size) lhs = tl_polytype_instantiate(self->arena, left, self->subs);
    else lhs = left->type;
    if (right->quantifiers.size) rhs = tl_polytype_instantiate(self->arena, right, self->subs);
    else rhs = right->type;

    return constrain_mono(self, lhs, rhs, node);
}

static int constrain_pm(tl_infer *self, tl_polytype *left, tl_monotype *right, ast_node const *node) {
    tl_polytype wrap = tl_polytype_wrap(right);
    return constrain(self, left, &wrap, node);
}

static void ensure_tv(tl_infer *self, tl_polytype **type) {
    if (!type) return;
    if (*type) return;
    *type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

static int infer_literal_type(tl_infer *self, ast_node *node,
                              tl_monotype *(*get_type)(tl_type_registry *)) {
    tl_monotype *ty = get_type(self->registry);
    ensure_tv(self, &node->type);
#if DEBUG_EXPLICIT_TYPE_ARGS
    {
        str ty_str        = tl_monotype_to_string(self->transient, ty);
        str node_type_str = node->type ? tl_polytype_to_string(self->transient, node->type) : str_empty();
        fprintf(stderr, "[DEBUG LITERAL] infer_literal_type at %s:%d:\n", node->file, node->line);
        fprintf(stderr, "  literal type: %s\n", str_cstr(&ty_str));
        fprintf(stderr, "  node->type before constrain: %s\n",
                str_is_empty(node_type_str) ? "(null)" : str_cstr(&node_type_str));
    }
#endif
    return constrain_pm(self, node->type, ty, node);
}

typedef struct {
    tl_monotype *parsed;
    hashmap     *type_arguments;
} annotation_parse_result;

static annotation_parse_result parse_type_annotation(tl_infer *self, traverse_ctx *ctx,
                                                     ast_node *annotation_node) {

    if (!ast_node_is_symbol(annotation_node) && !ast_node_is_nfa(annotation_node))
        return (annotation_parse_result){0};

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_monotype                    *parsed = tl_type_registry_parse_type_out_ctx(
      self->registry, annotation_node, self->transient, ctx ? ctx->type_arguments : null, &parse_ctx);

    if (0) {
        str tmp = v2_ast_node_to_string(self->transient, annotation_node);
        dbg(self, "parse_type_annotation: '%s' -> %p", str_cstr(&tmp), parsed);
    }

    return (annotation_parse_result){.parsed = parsed, .type_arguments = parse_ctx.type_arguments};
}

typedef struct {
    int add_to_lexicals;     // Add type args to ctx->lexical_names
    int check_type_arg_self; // Check if name is in type_arguments (for formal params)
} annotation_opts;

static int          constrain_or_set(tl_infer *, ast_node *, tl_polytype *);
static int          infer_struct_access(tl_infer *, ast_node *);
static int          add_generic(tl_infer *, ast_node *);
static int          is_type_literal(tl_infer *, traverse_ctx const *, ast_node const *);
static int          is_union_struct(tl_infer *, str);
static tl_polytype *make_arrow(tl_infer *, traverse_ctx *, ast_node_sized, ast_node *, int);
static tl_polytype *make_binary_predicate_arrow(tl_infer *, traverse_ctx *, ast_node *, ast_node *);
static void         toplevel_add(tl_infer *, str, ast_node *);

// Returns: 0 = no annotation, 1 = annotation processed, -1 = error
static int process_annotation(tl_infer *self, traverse_ctx *ctx, ast_node *node, annotation_opts opts) {
    if (!node) return 0;

    // parse_type_annotation knows how to look a symbol node's annotation
    annotation_parse_result result = parse_type_annotation(self, ctx, node);

    if (!result.parsed) return 0;

    // Merge type arguments into context
    if (ctx) {
        // FIXME: type arguments v2 may not be needed
        // map_merge(&ctx->type_arguments, result.type_arguments);

        // FIXME: with v2 type arguments, they should already be added to lexicals by the time any
        // annotation is processed; so this entire block could be removed.
        if (opts.add_to_lexicals) {
            str_array arr = str_map_keys(self->transient, result.type_arguments);
            forall(i, arr) {
#if DEBUG_RESOLVE
                fprintf(stderr, "resolve_node: adding type argument to lexicals: '%s'\n",
                        str_cstr(&arr.v[i]));
#endif
                str_hset_insert(&ctx->lexical_names, arr.v[i]);
            }
        }
    }

    tl_monotype *mono = result.parsed;

    // Handle type argument self-reference (for formal parameters)
    if (opts.check_type_arg_self && ast_node_is_symbol(node)) {
        str          name  = ast_node_str(node);
        tl_monotype *found = str_map_get_ptr(result.type_arguments, name);
        // FIXME explicit type args: do we need this secondary lookup? Doesn't seem to fix any of the
        // failing tests, though.

        if (!found && ctx) found = str_map_get_ptr(ctx->type_arguments, name);
        if (found) mono = found;
    }

    // For value annotations (not type arguments), unwrap any literal wrapper.
    // Type arguments stored in ctx->type_arguments are wrapped in literals,
    // but when used to annotate a value parameter, we need the underlying type.
    mono = unwrap_type_literal(mono);

    // Set annotation_type field of symbol nodes
    if (ast_node_is_symbol(node)) {
        node->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, mono);
        assert(node->symbol.annotation_type);
    }

    // Constrain node type
    tl_polytype *poly =
      ast_node_is_symbol(node) ? node->symbol.annotation_type : tl_polytype_absorb_mono(self->arena, mono);

#if DEBUG_RESOLVE
    str node_str = v2_ast_node_to_string(self->transient, node);
    str mono_str = tl_monotype_to_string(self->transient, mono);
    fprintf(stderr, "process_annotation %s : %s\n", str_cstr(&node_str), str_cstr(&mono_str));
#endif

    if (constrain_or_set(self, node, poly)) {
#if DEBUG_RESOLVE
        str node_str = v2_ast_node_to_string(self->transient, node);
        str poly_str = tl_polytype_to_string(self->transient, poly);
        fprintf(stderr, "[DEBUG process_annotation] ERROR: constrain_or_set failed for %s : %s\n",
                str_cstr(&node_str), str_cstr(&poly_str));
#endif
        return -1;
    }

    return 1;
}

// ============================================================================
// Special Case Handlers
// ============================================================================

static int is_std_function(ast_node *node) {
    return ast_node_is_std_application(node);
}

static int is_ptr_cast_annotation(ast_node *node) {
    return ast_node_is_symbol(node) && node->symbol.annotation_type &&
           tl_monotype_is_ptr(node->symbol.annotation_type->type);
}

// ============================================================================
// Type Inference Helpers
// ============================================================================

static int infer_nil(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
    return constrain_pm(self, node->type, weak, node);
}

static int infer_void(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (ctx->node_pos == npos_operand) {
        tl_monotype *nil = tl_type_registry_nil(self->registry);
        ast_node_type_set(node, tl_polytype_absorb_mono(self->arena, nil));
    }
    return 0;
}

static int infer_body(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    if (node->body.expressions.size) {
        u32       sz   = node->body.expressions.size;
        ast_node *last = node->body.expressions.v[sz - 1];
        ensure_tv(self, &last->type);

        if (ast_node_is_lambda_function(last)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_cannot_return_lambda, .node = last}));
            return 1;
        }

        return constrain(self, node->type, last->type, node);
    }
    return 0;
}

static int infer_tuple(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    ast_node_sized arr = ast_node_sized_from_ast_array(node);
    assert(arr.size > 0);

    tl_monotype_array tup_types = {.alloc = self->arena};
    array_reserve(tup_types, arr.size);
    forall(i, arr) {
        if (tl_polytype_is_scheme(arr.v[i]->type)) fatal("generic type");
        array_push(tup_types, arr.v[i]->type->type);
    }

    tl_monotype *tuple = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(tup_types));
    return constrain(self, node->type, tl_polytype_absorb_mono(self->arena, tuple), node);
}

static int infer_while(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *nil = tl_type_registry_nil(self->registry);
    return constrain_pm(self, node->type, nil, node);
}

static int infer_continue(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    return constrain_pm(self, node->type, tl_monotype_create_any(self->arena), node);
}

static int infer_return(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->return_.value, ctx, npos_operand)) return 1;

    if (node->return_.value && ast_node_is_lambda_function(node->return_.value)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_cannot_return_lambda, .node = node}));
        return 1;
    }

    ensure_tv(self, &node->type);
    if (!node->return_.is_break_statement && node->return_.value)
        if (constrain(self, node->type, node->return_.value->type, node)) return 1;

    if (ctx->result_type)
        if (constrain_pm(self, node->return_.value->type, ctx->result_type, node)) return 1;

    return 0;
}

static int check_const_strip_in_call(tl_infer *, tl_monotype *, tl_polytype *, ast_node *);

static int infer_lambda_function_application(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    tl_monotype *inst =
      tl_polytype_instantiate(self->arena, node->lambda_application.lambda->type, self->subs);
    ast_node_type_set(node->lambda_application.lambda, tl_polytype_absorb_mono(self->arena, inst));

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    tl_polytype       *app  = make_arrow(self, ctx, iter.nodes, node, 0);
    if (!app) return 1;

    if (self->verbose) {
        str inst_str = tl_monotype_to_string(self->transient, inst);
        str app_str  = tl_polytype_to_string(self->transient, app);
        dbg(self, "application: anon lambda %.*s callsite arrow: %.*s", str_ilen(inst_str),
            str_buf(&inst_str), str_ilen(app_str), str_buf(&app_str));
    }

    if (check_const_strip_in_call(self, inst, app, node)) return 1;
    tl_polytype wrap = tl_polytype_wrap(inst);
    if (constrain(self, &wrap, app, node)) return 1;

    if (constrain(self, node->type, node->lambda_application.lambda->lambda_function.body->type, node))
        return 1;

    return 0;
}

static int infer_lambda_function(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    ensure_tv(self, &node->type);

    tl_polytype *arrow =
      make_arrow(self, ctx, ast_node_sized_from_ast_array(node), node->lambda_function.body, 1);
    if (!arrow) return 1;
    tl_polytype_generalize(arrow, self->env, self->subs);
    if (constrain(self, node->type, arrow, node)) return 1;

    return 0;
}

static int infer_if_then_else(tl_infer *self, ast_node *node) {
    tl_monotype *bool_type = tl_type_registry_bool(self->registry);
    if (constrain_pm(self, node->if_then_else.condition->type, bool_type, node)) return 1;

    ensure_tv(self, &node->type);
    if (node->if_then_else.no) {
        if (constrain(self, node->if_then_else.yes->type, node->if_then_else.no->type, node)) return 1;
        if (constrain(self, node->type, node->if_then_else.yes->type, node)) return 1;
    } else {
        tl_monotype *nil      = tl_type_registry_nil(self->registry);
        tl_monotype *any_type = tl_monotype_create_any(self->arena);
        if (constrain_pm(self, node->type, nil, node)) return 1;

        if (constrain_pm(self, node->if_then_else.yes->type, any_type, node)) return 1;
    }

    return 0;
}

static int infer_assignment(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    // Struct field assignment only
    if (!node->assignment.is_field_name) fatal("runtime error");

    ctx->is_field_name = node->assignment.is_field_name;
    if (resolve_node(self, node->assignment.name, ctx, npos_assign_lhs)) return 1;
    ctx->is_field_name = 0;
    if (resolve_node(self, node->assignment.value, ctx, npos_value_rhs)) return 1;

    ensure_tv(self, &node->type);

    if (constrain(self, node->type, node->assignment.value->type, node)) return 1;

    // Do not constrain name type because field names are not unique

    return 0;
}

// Walk two types through Ptr layers in parallel. Returns 1 if the arg type
// has Const at any nesting level where the param type does not.
static int types_strip_const(tl_monotype *param, tl_monotype *arg) {
    while (tl_monotype_is_ptr(param) && tl_monotype_is_ptr(arg)) {
        tl_monotype *pt = tl_monotype_ptr_target(param);
        tl_monotype *at = tl_monotype_ptr_target(arg);
        if (tl_monotype_is_const(at) && !tl_monotype_is_const(pt)) return 1;
        // Unwrap Const if present on both sides before continuing
        if (tl_monotype_is_const(pt)) pt = tl_monotype_const_target(pt);
        if (tl_monotype_is_const(at)) at = tl_monotype_const_target(at);
        param = pt;
        arg   = at;
    }
    return 0;
}

// Check if a function call strips const from pointer arguments.
// Compares function parameter types against callsite argument types before unification.
static int check_const_strip_in_call(tl_infer *self, tl_monotype *func_type, tl_polytype *callsite,
                                     ast_node *node) {
    if (!tl_monotype_is_arrow(func_type)) return 0;
    tl_monotype *call_mono = callsite->type;
    if (!tl_monotype_is_arrow(call_mono)) return 0;

    tl_monotype_sized func_params = tl_monotype_arrow_get_args(func_type);
    tl_monotype_sized call_args   = tl_monotype_arrow_get_args(call_mono);

    u32               n           = func_params.size < call_args.size ? func_params.size : call_args.size;
    for (u32 i = 0; i < n; ++i) {
        if (types_strip_const(func_params.v[i], call_args.v[i])) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_const_violation, .node = node}));
            return 1;
        }
    }
    return 0;
}

// Check if the LHS of a reassignment involves dereferencing a Ptr(Const(T)).
// Returns 1 if a const violation is detected.
static int check_const_violation(tl_infer *self, ast_node *lhs) {
    if (!lhs) return 0;
    (void)self;

    // ptr.* = value: unary dereference of a const pointer
    if (lhs->tag == ast_unary_op && str_eq(ast_node_str(lhs->unary_op.op), S("*"))) {
        ast_node *operand = lhs->unary_op.operand;
        if (operand->type && operand->type->type) {
            tl_monotype *t = operand->type->type;
            if (tl_monotype_is_ptr_to_const(t)) return 1;
        }
    }

    // ptr->field = value, ptr.field = value, or ptr[i] = value
    if (lhs->tag == ast_binary_op) {
        str         op   = ast_node_str(lhs->binary_op.op);
        char const *op_s = str_cstr(&op);
        if (is_struct_access_operator(op_s) || is_index_operator(op_s)) {
            ast_node *left = lhs->binary_op.left;
            if (left->type && left->type->type) {
                tl_monotype *t = left->type->type;
                if (tl_monotype_is_ptr_to_const(t)) return 1;
            }
        }
    }

    return 0;
}

static int infer_reassignment(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    // Reassign to let-in bound symbol
    if (node->assignment.is_field_name) fatal("runtime error");

    if (resolve_node(self, node->assignment.name, ctx, npos_reassign_lhs)) return 1;
    if (resolve_node(self, node->assignment.value, ctx, npos_value_rhs)) return 1;

    ensure_tv(self, &node->type);

    // Check for const violations on the LHS
    if (check_const_violation(self, node->assignment.name)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_const_violation, .node = node}));
        return 1;
    }

    // reassignment nodes have void type
    tl_monotype *nil = tl_type_registry_nil(self->registry);
    if (constrain_pm(self, node->type, nil, node)) return 1;

    // name and value are same type
    if (constrain(self, node->assignment.name->type, node->assignment.value->type, node)) return 1;

    return 0;
}

static int infer_binary_op(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    ast_node   *left = node->binary_op.left, *right = node->binary_op.right;
    char const *op = str_cstr(&node->binary_op.op->symbol.name);

    if (resolve_node(self, left, ctx, npos_operand)) return 1;
    if (resolve_node(self, right, ctx, is_struct_access_operator(op) ? npos_field_name : npos_operand))
        return 1;

    ensure_tv(self, &node->type);

    if (is_arithmetic_operator(op)) {
        if (constrain(self, node->type, left->type, node)) return 1;
        if (constrain(self, left->type, right->type, node)) return 1;
    } else if (is_bitwise_operator(op)) {
        tl_monotype *int_type = tl_type_registry_int(self->registry);
        if (constrain_pm(self, left->type, int_type, node)) return 1;
        if (constrain_pm(self, right->type, int_type, node)) return 1;
        if (constrain_pm(self, node->type, int_type, node)) return 1;
    } else if (is_logical_operator(op) || is_relational_operator(op)) {
        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, node->type, bool_type, node)) return 1;
        if (constrain(self, left->type, right->type, node)) return 1;
    } else if (is_index_operator(op)) {
        tl_monotype *int_type = tl_type_registry_int(self->registry);
        if (constrain_pm(self, right->type, int_type, node)) return 1;

        // needed
        tl_monotype_substitute(self->arena, left->type->type, self->subs, null);
        tl_monotype_substitute(self->arena, right->type->type, self->subs, null);

        if (tl_monotype_has_ptr(left->type->type)) {
            tl_monotype *target = tl_monotype_ptr_target(left->type->type);
            if (constrain_pm(self, node->type, target, node)) return 1;
        } else if (tl_monotype_is_inst_of(left->type->type, S("CArray"))) {
            tl_monotype *target = left->type->type->cons_inst->args.v[0];
            if (constrain_pm(self, node->type, target, node)) return 1;
        }

    } else if (is_struct_access_operator(op)) {
        if (infer_struct_access(self, node)) return 1;
    } else {
        fatal("unknown operator type");
    }

    return 0;
}

static int infer_unary_op(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->unary_op.operand, ctx, npos_operand)) return 1;
    ast_node *operand = node->unary_op.operand;
    ensure_tv(self, &node->type);

    str op = ast_node_str(node->unary_op.op);
    if (str_eq(op, S("*"))) {
        tl_polytype_substitute(self->arena, operand->type, self->subs); // needed
        if (tl_monotype_has_ptr(operand->type->type)) {
            assert(!tl_polytype_is_scheme(operand->type));
            tl_monotype *target = tl_monotype_ptr_target(operand->type->type);
            if (constrain_pm(self, node->type, target, node)) return 1;
        } else if (tl_polytype_is_concrete(operand->type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
            return 1;
        }
    } else if (str_eq(op, S("&"))) {
        if (!tl_polytype_is_scheme(operand->type)) {
            tl_monotype *ptr = tl_type_registry_ptr(self->registry, operand->type->type);
            if (constrain_pm(self, node->type, ptr, node)) return 1;
        } else {
            tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
            tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
            if (constrain_pm(self, node->type, ptr, node)) return 1;
        }
    } else if (str_eq(op, S("!"))) {
        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, node->type, bool_type, node)) return 1;
    } else if (str_eq(op, S("~")) || str_eq(op, S("-")) || str_eq(op, S("+"))) {
        if (constrain(self, node->type, operand->type, node)) return 1;
    } else {
        fatal("unknown unary operator");
    }

    return 0;
}

static int infer_case(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->case_.expression, ctx, npos_operand)) return 1;
    if (resolve_node(self, node->case_.binary_predicate, ctx, npos_operand)) return 1;

    ensure_tv(self, &node->type);
    tl_monotype *nil       = tl_type_registry_nil(self->registry);
    tl_monotype *any_type  = tl_monotype_create_any(self->arena);
    tl_polytype *expr_type = node->case_.expression->type;

    if (node->case_.conditions.size != node->case_.arms.size) fatal("logic error");

    if (node->case_.is_union) {
        // Tagged union case expression: conditions are binding patterns like "c: Circle"
        // The condition symbol (c) is bound to the variant type (Circle) for use in the arm

        // Get wrapper type and extract valid variants
        tl_monotype *wrapper_type = expr_type->type;
        tl_monotype_substitute(self->arena, wrapper_type, self->subs, null); // needed

        // If there is an explicit type annotation (e.g., "case x: Option(T)"), always parse and
        // use it as the wrapper type. This is essential for generic functions with type-predicate
        // branching (e.g., `if x :: Option { case x: Option(T) { ... } } else if x :: Result ...`)
        // where different branches constrain the same variable to different tagged union types.
        // Without this, the first branch's constraint would permanently unify the variable's type,
        // making subsequent branches fail. The constraint is non-fatal here because after
        // specialization, only the matching branch will be valid.
        if (node->case_.union_annotation) {
            annotation_parse_result result = parse_type_annotation(self, ctx, node->case_.union_annotation);
            if (result.parsed && tl_monotype_is_inst(result.parsed)) {
                wrapper_type = result.parsed;
                // Constrain expression type to match annotation (non-fatal: in generic functions,
                // a prior branch may have already constrained to a different type)
                int save                        = self->is_constrain_ignore_error;
                self->is_constrain_ignore_error = 1;
                constrain_pm(self, expr_type, wrapper_type, node->case_.expression);
                self->is_constrain_ignore_error = save;
            }
        }

        if (!tl_monotype_is_inst(wrapper_type)) {
            expected_tagged_union(self, node->case_.expression);
            return 1;
        }

        // Find the 'u' (union) field in the wrapper type
        i32 u_index = tl_monotype_type_constructor_field_index(wrapper_type, S("u"));
        if (u_index < 0) {
            // wrapper type missing 'u'
            expected_tagged_union(self, node->case_.expression);
            return 1;
        }

        tl_monotype *union_type     = wrapper_type->cons_inst->args.v[u_index];
        str_sized    valid_variants = union_type->cons_inst->def->field_names;

        // Track which variants are covered (for exhaustiveness checking)
        int *variant_covered = alloc_malloc(self->transient, valid_variants.size * sizeof(int));
        memset(variant_covered, 0, valid_variants.size * sizeof(int));
        int has_else_arm = 0;

        forall(i, node->case_.conditions) {
            // Detect `else` condition on final arm
            if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) {
                has_else_arm = 1;
                break;
            }

            ast_node *cond = node->case_.conditions.v[i];
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation) {
                // "tagged union case condition must be 'binding: VariantType'"
                tagged_union_case_syntax_error(self, cond);
            }

            // Find the variant by name in the union type
            // Use the original (unmangled) name from the annotation, as union field names are unmangled
            str variant_name  = ast_node_name_original(cond->symbol.annotation);
            int variant_found = -1;
            forall(j, valid_variants) {
                if (str_eq(valid_variants.v[j], variant_name)) {
                    variant_found = (int)j;
                    break;
                }
            }

            if (variant_found < 0) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_tagged_union_unknown_variant, .node = cond}));
                return 1;
            }

            // Mark this variant as covered
            variant_covered[variant_found] = 1;

            // Get the variant type from the union type (which already has concrete types after
            // substitution) This handles both generic and non-generic variants correctly
            tl_monotype *variant_type = union_type->cons_inst->args.v[variant_found];

            // Set the binding's type (not as a literal - this is a value, not a type expression).
            // Note that we set both the condition node and the annotation_type.
            // If the case variable is mutable (var.&), we have a pointer type.
            tl_polytype *variant_poly = null;
            if (node->case_.is_union == AST_TAGGED_UNION_MUTABLE) {
                variant_poly =
                  tl_polytype_absorb_mono(self->arena, tl_type_registry_ptr(self->registry, variant_type));
            } else {
                variant_poly = tl_polytype_absorb_mono(self->arena, variant_type);
            }

            ast_node_type_set(cond, variant_poly);
            cond->symbol.annotation_type = variant_poly;
        }

        // Exhaustiveness check: if no else arm, verify all variants are covered
        if (!has_else_arm) {
            forall(j, valid_variants) {
                if (!variant_covered[j]) {
                    array_push(self->errors,
                               ((tl_infer_error){.tag = tl_err_tagged_union_missing_case, .node = node}));
                    return 1;
                }
            }
        }

    } else {
        // Standard case expression: conditions are expressions compared for equality
        forall(i, node->case_.conditions) {
            // Detect `else` condition on final arm
            if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) break;

            if (resolve_node(self, node->case_.conditions.v[i], ctx, npos_operand)) return 1;
            ensure_tv(self, &node->case_.conditions.v[i]->type);
            if (constrain(self, expr_type, node->case_.conditions.v[i]->type, node)) return 1;
        }
    }

    switch (node->case_.arms.size) {
    case 0:
        if (constrain_pm(self, node->type, nil, node)) return 1;
        break;
    case 1:
        if (constrain_pm(self, node->type, nil, node)) return 1;
        if (resolve_node(self, node->case_.arms.v[0], ctx, npos_operand)) return 1;
        if (constrain_pm(self, node->case_.arms.v[0]->type, any_type, node)) return 1;
        break;

    default: {
        if (resolve_node(self, node->case_.arms.v[0], ctx, npos_operand)) return 1;
        tl_polytype *arm_type = node->case_.arms.v[0]->type;
        if (constrain(self, node->type, arm_type, node->case_.arms.v[0])) return 1;

        forall(i, node->case_.arms) {
            if (resolve_node(self, node->case_.arms.v[i], ctx, npos_operand)) return 1;
            ensure_tv(self, &node->case_.arms.v[i]->type);
            if (constrain(self, node->case_.arms.v[i]->type, arm_type, node)) return 1;
        }
    } break;
    }

    if (node->case_.binary_predicate && node->case_.conditions.size) {
        tl_polytype *pred_arrow =
          make_binary_predicate_arrow(self, ctx, node->case_.expression, node->case_.conditions.v[0]);
        if (constrain(self, node->case_.binary_predicate->type, pred_arrow, node)) return 1;
    }

    return 0;
}

static int infer_let_in(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->let_in.name, ctx, npos_formal_parameter)) return 1;
    if (resolve_node(self, node->let_in.value, ctx, npos_value_rhs)) return 1;

    ensure_tv(self, &node->type);
    if (node->let_in.body) ensure_tv(self, &node->let_in.body->type);

    if (ast_node_is_lambda_function(node->let_in.value)) {
        str name = node->let_in.name->symbol.name;

        if (add_generic(self, node)) return 1;

        node->let_in.name->type = null;

        if (node->let_in.body)
            if (constrain(self, node->type, node->let_in.body->type, node)) return 1;

        {
            ast_node *let_in_lambda    = ast_node_clone(self->arena, node);
            let_in_lambda->let_in.body = null;
            toplevel_add(self, name, let_in_lambda);
        }

    } else {
        if (is_std_function(node->let_in.value)) {
            node->let_in.value->type = tl_polytype_nil(self->arena, self->registry);
        }

        tl_polytype *name_type            = node->let_in.name->type;
        tl_polytype *name_annotation_type = node->let_in.name->symbol.annotation_type;
        tl_polytype *value_type           = node->let_in.value->type;

        {
            int is_cast = is_ptr_cast_annotation(node->let_in.name);

            if (is_cast) self->is_constrain_ignore_error = 1;
            if (name_annotation_type) {
                name_type = name_annotation_type;

                str name  = ast_node_str(node->let_in.name);
                str tmp   = tl_polytype_to_string(self->transient, name_annotation_type);

                dbg(self, "let_in cast '%s': using annotation type '%s'", str_cstr(&name), str_cstr(&tmp));
            }

            // Skip constraint for CArray-to-Ptr casts: CArray and Ptr are different type
            // constructors so unification would corrupt type state.
            int skip = 0;
            if (is_cast && value_type) {
                // tl_polytype_substitute(self->arena, value_type, self->subs);
                skip = tl_monotype_is_inst_of(value_type->type, S("CArray"));
            }
            if (!skip) {
                if (constrain(self, name_type, value_type, node) && !is_cast) return 1;
            }
            self->is_constrain_ignore_error = 0;
        }

        env_insert_constrain(self, node->let_in.name->symbol.name, name_type, node->let_in.name);

        if (node->let_in.body)
            if (constrain(self, node->type, node->let_in.body->type, node)) return 1;
    }
    return 0;
}

static int infer_named_function_application(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node, ctx, ctx->node_pos)) return 1;

    str          name     = ast_node_str(node->named_application.name);
    str          original = ast_node_name_original(node->named_application.name);
    tl_polytype *type     = tl_type_env_lookup(self->env, name);
    if (!type) {
        type = tl_type_registry_get(self->registry, name);
        if (!type) return 0;
    }

    if (is_type_literal(self, ctx, node)) return 0;

    if (tl_polytype_is_type_constructor(type)) {
        // FIXME: this comment is incorrect about type literals.
        // This nfa can be either a type literal, or a type value constructor. Value constructors are of the
        // form `Foo(a=1, b=2)`. Type literals are of the form `Foo(Int)` for generics or plain `Foo` for
        // concrete.
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (resolve_node(self, arg, ctx, npos_function_argument)) return 1;
        }

        tl_monotype *inst        = null;
        u32          n_type_args = node->named_application.n_type_arguments;

        if (n_type_args > 0) {
            // Use explicit type arguments for instantiation
            tl_monotype_sized args = {
              .v    = alloc_malloc(self->transient, n_type_args * sizeof(tl_monotype *)),
              .size = n_type_args,
            };

            for (u32 i = 0; i < n_type_args; i++) {
                ast_node *type_arg_node = node->named_application.type_arguments[i];
                if (type_arg_node && type_arg_node->type) {
                    args.v[i] = unwrap_type_literal(type_arg_node->type->type);
#if DEBUG_EXPLICIT_TYPE_ARGS
                    str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
                    fprintf(
                      stderr,
                      "[DEBUG EXPLICIT TYPE ARGS] type constructor: using explicit type for arg %u: %s\n",
                      i, str_cstr(&arg_str));
#endif
                } else {
                    args.v[i] = null; // will create fresh type variable
                }
            }

            inst = tl_type_registry_instantiate_with(self->registry, name, args);
        } else {
            inst = tl_type_registry_instantiate(self->registry, name);
        }

        if (!inst) {
            wrong_number_of_arguments(self, node);
            return 1;
        }

#if DEBUG_EXPLICIT_TYPE_ARGS
        {
            str inst_str = tl_monotype_to_string(self->transient, inst);
            fprintf(stderr,
                    "[DEBUG EXPLICIT TYPE ARGS] infer_named_function_application (type constructor):\n");
            fprintf(stderr, "  name: %s\n", str_cstr(&name));
            fprintf(stderr, "  instantiated type: %s\n", str_cstr(&inst_str));
            fprintf(stderr, "  inst->cons_inst->args.size: %u\n", (u32)inst->cons_inst->args.size);
            for (u32 j = 0; j < inst->cons_inst->args.size; j++) {
                str arg_str = tl_monotype_to_string(self->transient, inst->cons_inst->args.v[j]);
                fprintf(stderr, "    field[%u] type: %s\n", j, str_cstr(&arg_str));
            }
        }
#endif

        {
            str          inst_str = tl_monotype_to_string(self->transient, inst);
            tl_polytype *app      = make_arrow(self, ctx, iter.nodes, null, 0);
            if (!app) return 1;

            if (self->verbose) {
                str app_str = tl_polytype_to_string(self->transient, app);
                dbg(self, "type constructor: callsite '%s' (%s) arrow: %s", str_cstr(&name),
                    str_cstr(&inst_str), str_cstr(&app_str));
            }
        }

        if (!is_union_struct(self, name)) {
            iter = ast_node_arguments_iter(node);
            if (iter.nodes.size != inst->cons_inst->args.size) {
                wrong_number_of_arguments(self, node);
                return 1;
            }
        }

        u32 i = 0;
        iter  = ast_node_arguments_iter(node);
        while ((arg = ast_arguments_next(&iter))) {
            if (i >= inst->cons_inst->args.size) fatal("runtime error");

            if (ast_node_is_assignment(arg)) {
                // This is a type value constructor
                i32 found = tl_monotype_type_constructor_field_index(
                  inst, ast_node_name_original(arg->assignment.name));

                if (-1 == found) {
                    array_push(self->errors,
                               ((tl_infer_error){.tag = tl_err_field_not_found, .node = arg}));
                    return 1;
                }
                assert(found < (i32)inst->cons_inst->args.size);

                int is_cast = is_ptr_cast_annotation(arg->assignment.name);
                if (is_cast) {
                    tl_polytype *annotation_type = arg->assignment.name->symbol.annotation_type;
                    // Constrain annotation type against struct field to propagate concrete type info
                    if (constrain_pm(self, annotation_type, inst->cons_inst->args.v[found], node)) return 1;
                    // Constrain value type against annotation permissively (cast)
                    self->is_constrain_ignore_error = 1;
                    constrain_pm(self, arg->type, inst->cons_inst->args.v[found], node);
                    self->is_constrain_ignore_error = 0;
                } else {
#if DEBUG_EXPLICIT_TYPE_ARGS
                    {
                        str field_name = ast_node_name_original(arg->assignment.name);
                        str field_type_str =
                          tl_monotype_to_string(self->transient, inst->cons_inst->args.v[found]);
                        fprintf(stderr, "[DEBUG EXPLICIT TYPE ARGS] constraining field '%s':\n",
                                str_cstr(&field_name));
                        if (arg->type) {
                            str arg_type_str = tl_polytype_to_string(self->transient, arg->type);
                            fprintf(stderr, "  arg->type (value): %s\n", str_cstr(&arg_type_str));
                        } else {
                            fprintf(stderr, "  arg->type (value): (null)\n");
                        }
                        fprintf(stderr, "  field type from inst: %s\n", str_cstr(&field_type_str));
                    }
#endif
                    if (constrain_pm(self, arg->type, inst->cons_inst->args.v[found], node)) return 1;
                }
            } else {
                // In this branch, node is a type literal.
            }
            ++i;
        }

        if (constrain_pm(self, node->type, inst, node)) return 1;

    } else {
        if (tl_polytype_is_concrete(type)) {
            if (!str_is_empty(original)) {
                tl_polytype *found = tl_type_env_lookup(self->env, original);
                if (found) type = found;
            }
        }

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        tl_monotype       *inst = null;

        // Check for explicit type arguments
        u32       n_type_args = node->named_application.n_type_arguments;
        ast_node *let         = toplevel_get(self, name);
        u32       n_quants    = type->quantifiers.size;

        if (n_type_args > 0 && let && ast_node_is_let(let) && n_quants > 0) {
            // Build args array from explicit type arguments
            tl_monotype_sized args = {
              .v    = alloc_malloc(self->transient, n_quants * sizeof(tl_monotype *)),
              .size = n_quants,
            };

            for (u32 i = 0; i < n_quants; i++) {
                args.v[i] = null; // default: create fresh type variable

                if (i < let->let.n_type_parameters) {
                    str param_name = let->let.type_parameters[i]->symbol.name;

#if DEBUG_INVARIANTS
                    // Invariant: Type argument lookup must use alpha-converted names
                    if (!is_alpha_converted_name(param_name)) {
                        char detail[256];
                        snprintf(detail, sizeof detail,
                                 "Looking up '%.*s' which is not alpha-converted in type_arguments",
                                 str_ilen(param_name), str_buf(&param_name));
                        report_invariant_failure(self, "infer_named_function_application",
                                                 "Type argument lookup must use alpha-converted names",
                                                 detail, node);
                    }
#endif

                    tl_monotype *explicit_type = str_map_get_ptr(ctx->type_arguments, param_name);
                    if (explicit_type) {
                        args.v[i] = unwrap_type_literal(explicit_type);
#if DEBUG_EXPLICIT_TYPE_ARGS
                        str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
                        fprintf(stderr,
                                "[DEBUG EXPLICIT TYPE ARGS] using explicit type for quantifier %u: %s\n", i,
                                str_cstr(&arg_str));
#endif
                    }
                }
            }

            inst = tl_polytype_instantiate_with(self->arena, type, args, self->subs);
        } else {
            inst = tl_polytype_instantiate(self->arena, type, self->subs);
        }

        str          inst_str = tl_monotype_to_string(self->transient, inst);
        tl_polytype *app      = make_arrow(self, ctx, iter.nodes, node, 0);
        if (!app) return 1;

#if DEBUG_EXPLICIT_TYPE_ARGS
        {
            str type_str = tl_polytype_to_string(self->transient, type);
            str app_str  = tl_polytype_to_string(self->transient, app);
            fprintf(stderr,
                    "[DEBUG EXPLICIT TYPE ARGS] infer_named_function_application (function call):\n");
            fprintf(stderr, "  name: %s\n", str_cstr(&name));
            fprintf(stderr, "  callee type from env: %s\n", str_cstr(&type_str));
            fprintf(stderr, "  instantiated: %s\n", str_cstr(&inst_str));
            fprintf(stderr, "  callsite arrow: %s\n", str_cstr(&app_str));
            fprintf(stderr, "  n_type_arguments: %u\n", node->named_application.n_type_arguments);
        }
#endif

        if (self->verbose) {
            str app_str = tl_polytype_to_string(self->transient, app);
            dbg(self, "application: callsite '%s' (%s) arrow: %s", str_cstr(&name), str_cstr(&inst_str),
                str_cstr(&app_str));
        }
        if (check_const_strip_in_call(self, inst, app, node)) return 1;
        tl_polytype wrap = tl_polytype_wrap(inst);
        if (constrain(self, &wrap, app, node)) return 1;
    }

    return 0;
}

static void         rename_variables(tl_infer *, ast_node *, rename_variables_ctx *, int);
static void         concretize_params(tl_infer *self, ast_node *, tl_monotype *, hashmap *type_arguments,
                                      ast_node_sized callsite_type_arguments);
static tl_polytype *make_arrow(tl_infer *, traverse_ctx *, ast_node_sized, ast_node *, int is_params);
static tl_polytype *make_arrow_result_type(tl_infer *, traverse_ctx *, ast_node_sized, tl_polytype *,
                                           int is_params);
static tl_polytype *make_arrow_with(tl_infer *, traverse_ctx *, ast_node *, tl_polytype *);
static tl_polytype *make_binary_predicate_arrow(tl_infer *, traverse_ctx *, ast_node *lhs, ast_node *rhs);
static int          traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb);

//

static void      add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow);
static void      concretize_params(tl_infer *self, ast_node *node, tl_monotype *callsite,
                                   hashmap *type_arguments, ast_node_sized callsite_type_arguments);
static void      toplevel_name_replace(ast_node *node, str name_replace);
static void      collect_annotation_type_vars(tl_infer *self, ast_node *node, rename_variables_ctx *ctx);

static ast_node *clone_generic_for_arrow(tl_infer *self, ast_node const *node, tl_monotype *arrow,
                                         str inst_name, hashmap *type_arguments,
                                         ast_node_sized callsite_type_arguments) {
    ast_node *clone = ast_node_clone(self->arena, node);
    ast_node *name  = toplevel_name_node(clone);
    assert(ast_node_is_symbol(name));

    // rename variables: also erases type information
    rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};

    // Before clearing the annotation, collect type variables from it.
    // These are type variables like 'a', 'b' in forward declarations that appear in the
    // function body (e.g., `sizeof[b]()`). They need to be in lexical scope for renaming.
    if (name->symbol.annotation) {
        collect_annotation_type_vars(self, name->symbol.annotation, &ctx);
    }

    // Count type variables collected from annotation
    u32 n_annotation_type_vars   = map_size(ctx.lex);

    name->symbol.annotation_type = null;
    name->symbol.annotation      = null;

    rename_variables(self, clone, &ctx, 0);

#if DEBUG_INVARIANTS
    // Invariant: After clone + rename_variables, all types must be erased (null)
    {
        struct check_types_null_ctx check_ctx = {
          .self = self, .phase = "clone_generic_for_arrow", .failures = 0};
        ast_node_dfs(&check_ctx, clone, check_types_null_cb);
        if (check_ctx.failures) {
            fprintf(stderr, "ERROR: Type pollution detected in cloned AST\n");
        }
    }

    // Invariant: Alpha-converted type parameter names must not already exist in the environment
    // This ensures each specialization gets truly fresh names
    if (ast_node_is_let(clone)) {
        for (u32 i = 0; i < clone->let.n_type_parameters; i++) {
            ast_node    *tp       = clone->let.type_parameters[i];
            str          tp_name  = tp->symbol.name;

            tl_polytype *existing = tl_type_env_lookup(self->env, tp_name);
            if (existing) {
                char detail[256];
                str  type_str = tl_polytype_to_string(self->transient, existing);
                snprintf(detail, sizeof detail,
                         "Type parameter '%.*s' already exists in environment with type: %s",
                         str_ilen(tp_name), str_buf(&tp_name), str_cstr(&type_str));
                report_invariant_failure(self, "clone_generic_for_arrow",
                                         "New specialization type parameters must have fresh names", detail,
                                         tp);
            }
        }
    }
#endif

    // If we collected type variables from the annotation, add them as type parameters to the let node.
    // This ensures traverse_ctx_load_type_arguments can find them later.
    if (n_annotation_type_vars > 0 && ast_node_is_let(clone) && clone->let.n_type_parameters == 0) {
        str_array keys = str_map_keys(self->transient, ctx.lex);

        // Filter to just the type variables we added (not value parameters)
        // Type vars are lowercase, single letter or short names
        ast_node **type_params   = alloc_malloc(self->arena, sizeof(ast_node *) * keys.size);
        u32        n_type_params = 0;

        forall(i, keys) {
            str original = keys.v[i];
            // Type variables are typically lowercase letters
            // FIXME: do NOT assume any convention about uppercase, lowercase, length, etc.
            char first = str_len(original) > 0 ? str_buf(&original)[0] : 0;
            if (first >= 'a' && first <= 'z' && str_len(original) <= 3) {
                str *renamed = str_map_get(ctx.lex, original);
                if (renamed) {
                    ast_node *type_param         = ast_node_create(self->arena, ast_symbol);
                    type_param->symbol.name      = *renamed;
                    type_param->symbol.original  = original;
                    type_params[n_type_params++] = type_param;
#if DEBUG_RENAME
                    fprintf(stderr, "[DEBUG] Added type_parameter: %s (original: %s)\n", str_cstr(renamed),
                            str_cstr(&original));
#endif
                }
            }
        }

        if (n_type_params > 0) {
            clone->let.type_parameters   = type_params;
            clone->let.n_type_parameters = (u8)n_type_params;
        }
    }

    // recalculate free variables, because symbol names have been renamed
    tl_polytype wrap = tl_polytype_wrap(arrow);
    add_free_variables_to_arrow(self, clone, &wrap);

    concretize_params(self, clone, arrow, type_arguments, callsite_type_arguments);

#if DEBUG_INVARIANTS
    // Invariant: Type parameters with explicit bindings in type_arguments must have concrete types
    if (ast_node_is_let(clone) && type_arguments) {
        for (u32 i = 0; i < clone->let.n_type_parameters; i++) {
            ast_node *tp      = clone->let.type_parameters[i];
            str       tp_name = tp->symbol.name;

            // Only check type parameters that have an explicit binding in type_arguments
            tl_monotype *bound_type = str_map_get_ptr(type_arguments, tp_name);
            if (!bound_type) continue; // No explicit binding, skip this type parameter

            if (!tp->type || !tl_polytype_is_concrete(tp->type)) {
                char detail[256];
                if (tp->type) {
                    str type_str = tl_polytype_to_string(self->transient, tp->type);
                    snprintf(detail, sizeof detail,
                             "Type parameter '%.*s' with explicit binding has non-concrete type: %s",
                             str_ilen(tp_name), str_buf(&tp_name), str_cstr(&type_str));
                } else {
                    snprintf(detail, sizeof detail,
                             "Type parameter '%.*s' with explicit binding has null type", str_ilen(tp_name),
                             str_buf(&tp_name));
                }
                report_invariant_failure(
                  self, "clone_generic_for_arrow",
                  "Type parameter with explicit binding must have concrete type after concretize_params",
                  detail, tp);
            }
        }
    }
#endif

    toplevel_name_replace(clone, inst_name);

    return clone;
}

static int traverse_ast_node_params(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    // hashmap *types = tl_type_registry_parse_parameters(self->registry, self->transient, node);
    // map_merge(&ctx->param_types, types);

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *param;
    while ((param = ast_arguments_next(&iter))) {
        assert(ast_node_is_symbol(param));

        ensure_tv(self, &param->type);

        ctx->node_pos = npos_formal_parameter;
        if (cb(self, ctx, param)) return 1;
    }
    return 0;
}

static int traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    if (null == node) return 0;

    switch (node->tag) {
    case ast_attribute_set:
        // not traversed
        break;

    case ast_let: {
        // Save outer context: when specializing nested functions (via post_specialize → specialize_arrow),
        // the inner function's let case would otherwise clobber the outer type_arguments and lexical_names.
        hashmap *save_type_arguments = map_copy(ctx->type_arguments);
        hashmap *save_lexical_names  = map_copy(ctx->lexical_names);

        // Note: this node is being processed as a toplevel function definition. It must clear all lexical
        // contexts.
        map_reset(ctx->type_arguments);
        map_reset(ctx->lexical_names);

        traverse_ctx_load_type_arguments(self, ctx, node);

        ctx->node_pos = npos_toplevel;
        // Note: traversing the name as a symbol currently causes invalid constraints to be applied when
        // specializing generic functions. The name's node->type should not in any case be relied upon: the
        // canonical arrow type of a function name is in the environment, not the ast.

        ctx->node_pos = npos_formal_parameter;
        if (traverse_ast_node_params(self, ctx, node, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->let.body, cb)) return 1;

        // Note: let nodes are intentionally not processed with the callback.

        // Restore outer context
        map_destroy(&ctx->type_arguments);
        map_destroy(&ctx->lexical_names);
        ctx->type_arguments = save_type_arguments;
        ctx->lexical_names  = save_lexical_names;

    } break;

    case ast_let_in: {

        hashmap *save = map_copy(ctx->lexical_names);
        assert(ast_node_is_symbol(node->let_in.name));

        // process name first, for lexical scope
        ctx->node_pos = npos_formal_parameter;
        if (cb(self, ctx, node->let_in.name)) return 1;

        // process node parent before children, because there may be side effects required before traversing
        // body.
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        // traverse value first, then traverse name and body
        ctx->node_pos = npos_value_rhs;
        if (traverse_ast(self, ctx, node->let_in.value, cb)) return 1;
        ctx->node_pos = npos_formal_parameter;
        if (traverse_ast(self, ctx, node->let_in.name, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->let_in.body, cb)) return 1;

        // process node again: for specialised types, typing the name depends on typing the value.
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lexical_names);
        ctx->lexical_names = save;

    } break;

    case ast_named_function_application: {
        if (traverse_ctx_assign_type_arguments(self, ctx, node)) return 1;

        // traverse arguments

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            ctx->node_pos = npos_function_argument;
            if (traverse_ast(self, ctx, arg, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_lambda_function: {

        hashmap *save = map_copy(ctx->lexical_names);

        if (traverse_ast_node_params(self, ctx, node, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->lambda_function.body, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lexical_names);
        ctx->lexical_names = save;

    } break;

    case ast_lambda_function_application: {

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            ctx->node_pos = npos_function_argument;
            if (traverse_ast(self, ctx, arg, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->lambda_application.lambda, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_if_then_else: {

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.condition, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.yes, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.no, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, arr.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_body:
        forall(i, node->body.expressions) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->body.expressions.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_case: {
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->case_.expression, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->case_.binary_predicate, cb)) return 1;

        if (node->case_.is_union) {
            // For union cases, conditions are handled by infer_case() directly.
            // We only need to add condition symbols to lexical scope before traversing arms.
            forall(i, node->case_.conditions) {
                hashmap  *save = map_copy(ctx->lexical_names);
                ast_node *cond = node->case_.conditions.v[i];

                // Skip nil condition (else arm)
                if (ast_node_is_nil(cond)) {
                    if (i < node->case_.arms.size) {
                        ctx->node_pos = npos_operand;
                        if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
                    }
                    ctx->lexical_names = save;
                    continue;
                }

                // Add condition symbol to lexical scope (don't traverse it - infer_case handles it)
                if (ast_node_is_symbol(cond)) {
                    str_hset_insert(&ctx->lexical_names, cond->symbol.name);
                }

                // Process only the corresponding arm (not the condition)
                if (i < node->case_.arms.size) {
                    ctx->node_pos = npos_operand;
                    if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
                }

                ctx->lexical_names = save;
            }
        } else {
            // Original behavior for non-union cases
            forall(i, node->case_.conditions) {
                ctx->node_pos = npos_operand;
                if (traverse_ast(self, ctx, node->case_.conditions.v[i], cb)) return 1;
            }
            forall(i, node->case_.arms) {
                ctx->node_pos = npos_operand;
                if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
            }
        }
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_binary_op:
        // don't traverse op, it's just an operator
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->binary_op.left, cb)) return 1;

        // when traversing to the right of . and ->, we could encounter field names that should not be
        // considered free variables, so signal that in the traverse_ctx. Note that other binary ops like
        // arithmetic should not trigger the field_name case.
        {
            char const *op               = str_cstr(&node->binary_op.op->symbol.name);
            int         is_symbol        = ast_node_is_symbol(node->binary_op.right);
            int         is_struct_access = is_struct_access_operator(op);
            int         is_field_name    = is_symbol && is_struct_access;
            int         save             = 0;
            if (is_field_name) {
                save               = ctx->is_field_name;
                ctx->is_field_name = 1;
            }

            if (is_struct_access_operator(op)) ctx->node_pos = npos_field_name;
            else ctx->node_pos = npos_operand;

            if (traverse_ast(self, ctx, node->binary_op.right, cb)) return 1;
            if (is_field_name) ctx->is_field_name = save;
        }

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_unary_op:
        // don't traverse op, it's just an operator
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->unary_op.operand, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_assignment:
        ctx->node_pos      = npos_assign_lhs;
        ctx->is_field_name = node->assignment.is_field_name;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;

        ctx->node_pos      = npos_value_rhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_reassignment:
    case ast_reassignment_op:
        // don't traverse op, it's just an operator
        ctx->node_pos      = npos_assign_lhs;
        ctx->is_field_name = node->assignment.is_field_name;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;

        ctx->node_pos      = npos_value_rhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_return:
        //
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->return_.value, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_while:
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->while_.condition, cb)) return 1;

        if (node->while_.update) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->while_.update, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->while_.body, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_hash_command:
    case ast_nil:
    case ast_void:
    case ast_continue:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_string:
    case ast_c_string:
    case ast_char:
    case ast_symbol:
    case ast_u64:
    case ast_type_alias:
    case ast_type_predicate:
    case ast_user_type_definition:

        // operate on the leaf node
        if (cb(self, ctx, node)) return 1;

        break;
    }

    return 0;
}

static str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args,
                                       tl_polytype **out_type);

static int type_literal_specialize(tl_infer *self, ast_node *node) {
    // specialize a type id, e.g. `Point(Int)`. Contrast to specialize_type_constructor, which specialises
    // based on a callsite like `Point(1, 2)`. Assuming Point(a) { x : a, y : a }.
    // return 1 if node is not a type identifier or other error occurs.

    if (ast_node_is_symbol(node)) return 1;

    if (0) {
        str tmp = v2_ast_node_to_string(self->transient, node);
        dbg(self, "type_literal_specialize: '%s'", str_cstr(&tmp));
    }

    tl_monotype *parsed = tl_type_registry_parse_type(self->registry, node);
    if (parsed) {
        tl_monotype *target = parsed;
        if (!tl_monotype_is_inst(target)) return 1;
        str name = target->cons_inst->def->generic_name;

#if DEBUG_RECURSIVE_TYPES
        {
            str parsed_str = tl_monotype_to_string(self->transient, parsed);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] type_literal_specialize: parsed=%s name='%s'\n",
                    str_cstr(&parsed_str), str_cstr(&name));
        }
#endif

        tl_monotype_sized args = target->cons_inst->args;
        tl_monotype      *inst = tl_type_registry_get_cached_specialization(self->registry, name, args);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] type_literal_specialize: cache %s for '%s'\n",
                inst ? "HIT" : "MISS", str_cstr(&name));
#endif
        str          name_inst    = str_empty();
        tl_polytype *special_type = null;
        if (!inst) {
            name_inst = specialize_type_constructor(self, name, args, &special_type);
#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES] type_literal_specialize: specialize returned '%s' type=%p\n",
                    str_is_empty(name_inst) ? "(empty)" : str_cstr(&name_inst), (void *)special_type);
#endif
            // ok to fail: enums, nullary builtins, etc
            if (str_is_empty(name_inst)) return 0;
            if (!special_type) return 0;
        } else {
            name_inst    = inst->cons_inst->special_name;
            special_type = tl_polytype_absorb_mono(self->arena, inst);
        }

        if (ast_node_is_symbol(node)) {
            ast_node_name_replace(node, name_inst);
            if (node->type) {
                if (constrain(self, node->type, special_type, node)) return 1;
            } else {
                ast_node_type_set(node, special_type);
            }
        } else if (ast_node_is_nfa(node)) {
            ast_node_name_replace(node->named_application.name, name_inst);
            if (node->named_application.name->type) {
                if (constrain(self, node->named_application.name->type, special_type, node)) return 1;
            } else {
                ast_node_type_set(node->named_application.name, special_type);
            }

        } else fatal("logic error");

        return 0;
    }

    return 1;
}

static int add_generic(tl_infer *, ast_node *);

static int constrain_or_set(tl_infer *self, ast_node *node, tl_polytype *type) {
#if DEBUG_RESOLVE
    str name     = ast_node_is_symbol(node) ? ast_node_str(node) : str_empty();
    str poly_str = tl_polytype_to_string(self->transient, type);
#endif

    if (node->type) {
#if DEBUG_RESOLVE
        str node_type_str = tl_polytype_to_string(self->transient, node->type);
        fprintf(stderr, "constrain_or_set: '%s' : %s :: %s\n", str_cstr(&name), str_cstr(&node_type_str),
                str_cstr(&poly_str));
#endif
        if (constrain(self, node->type, type, node)) return type_error(self, node);
    }

    else {
#if DEBUG_RESOLVE
        fprintf(stderr, "constrain_or_set: '%s': %s\n", str_cstr(&name), str_cstr(&poly_str));
#endif
        ast_node_type_set(node, type);
    }
    return 0;
}

static int expected_symbol(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_symbol, .node = node}));
    return 1;
}

static void sync_with_env(tl_infer *self, traverse_ctx *ctx, ast_node *node, int want_fresh) {
    // If it's a symbol, if it has a type, add it to the environment. If it doesn't have a type, look it up
    // from the environment.
    if (!ast_node_is_symbol(node)) goto finish;

    str name = ast_node_str(node);
    if (ctx && str_map_contains(ctx->type_arguments, name)) goto finish;

    if (want_fresh && !node->type) {
        node->type = tl_polytype_create_fresh_tv(self->arena, self->subs);
    }

    if (node->type) {
        // A symbol with an existing type: either insert it to the environment, or constrain it with the
        // existing type if already in env.
        env_insert_constrain(self, name, node->type, node);
    } else {
        // No type: look up its type from the environment, if any
        tl_polytype *type = tl_type_env_lookup(self->env, name);
#if DEBUG_TYPE_ALIAS
        if (!type) {
            int is_alias = tl_type_registry_is_type_alias(self->registry, name);
            fprintf(stderr, "[DEBUG_TYPE_ALIAS] sync_with_env: '%s' not in type env\n", str_cstr(&name));
            if (is_alias)
                fprintf(stderr, "[DEBUG_TYPE_ALIAS]   BUT exists in registry as type alias!\n");
        }
#endif
        if (type) ast_node_type_set(node, type);
    }

finish:
    // regardless, node must have a type, even if it's a fresh tv
    if (!node->type) node->type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

static int check_is_pointer(tl_infer *self, tl_polytype *type, ast_node *node) {
    if (tl_monotype_is_inst(type->type)) {
        if (!tl_monotype_is_ptr(type->type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
            return 1;
        }
    }
    return 0;
}

static void ensure_symbol_type_from_env(tl_infer *self, ast_node *node) {
    if (!ast_node_is_symbol(node)) return;

    tl_polytype *poly = tl_type_env_lookup(self->env, ast_node_str(node));

#if DEBUG_TYPE_ALIAS
    {
        str name_dbg = ast_node_str(node);
        if (tl_type_registry_is_type_alias(self->registry, name_dbg)) {
            char const *type_s = "(null)";
            char const *node_s = "(null)";
            str         type_dbg, node_dbg;
            if (poly) { type_dbg = tl_polytype_to_string(self->transient, poly); type_s = str_cstr(&type_dbg); }
            if (node->type) { node_dbg = tl_polytype_to_string(self->transient, node->type); node_s = str_cstr(&node_dbg); }
            fprintf(stderr, "[DEBUG_TYPE_ALIAS] ensure_symbol_type_from_env: '%s' env_lookup=%s node_type=%s\n",
                    str_cstr(&name_dbg), type_s, node_s);
        }
    }
#endif

    // Note: do not override node->type if it is already concrete. There is some confusion with the handling
    // of type literals that makes the constrain fail unless it is guarded behind this if condition.
    if (!node->type || !tl_polytype_is_concrete(node->type)) constrain_or_set(self, node, poly);
}

static int infer_struct_access(tl_infer *self, ast_node *node) {
    if (!ast_node_is_binary_op_struct_access(node)) fatal("logic error");
    ensure_tv(self, &node->type);
    ensure_tv(self, &node->binary_op.left->type);
    ensure_tv(self, &node->binary_op.right->type);
    ast_node    *left = node->binary_op.left, *right = node->binary_op.right;
    char const  *op          = str_cstr(&node->binary_op.op->symbol.name);

    tl_monotype *struct_type = null;

    // tl_monotype_substitute(self->arena, left->type->type, self->subs, null);
    // tl_monotype_substitute(self->arena, right->type->type, self->subs, null);

    // handle -> vs . access
    if (0 == strcmp("->", op)) {

        // FIXME: it should be an error if inference completes and struct access has never checked
        // field names being valid. Possibly do this check in a later phase rather than here.

        // if type is not a constructor instance, all we can assert is that the left side must be a
        // pointer
        if (check_is_pointer(self, left->type, left)) return 1;
        ensure_symbol_type_from_env(self, left);

        if (tl_monotype_is_ptr(left->type->type)) {
            struct_type = tl_monotype_ptr_target(left->type->type);
        } else {
            tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
            tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
            if (constrain_pm(self, left->type, ptr, node)) return 1;
            struct_type = weak;
        }

    } else {
        ensure_symbol_type_from_env(self, left);
        struct_type = (tl_monotype *)left->type->type;
#if DEBUG_TYPE_ALIAS
        if (ast_node_is_symbol(left)) {
            str left_name = ast_node_str(left);
            if (tl_type_registry_is_type_alias(self->registry, left_name)) {
                str st_str = tl_monotype_to_string(self->transient, struct_type);
                fprintf(stderr,
                        "[DEBUG_TYPE_ALIAS] infer_struct_access: left='%s' struct_type=%s is_inst=%d\n",
                        str_cstr(&left_name), str_cstr(&st_str), tl_monotype_is_inst(struct_type));
            }
        }
#endif
    }

    // Note: must substitute to resolve type of chained field access, eg: foo.bar.baz
    tl_monotype_substitute(self->arena, struct_type, self->subs, null); // needed

    // Const(T) is transparent for field access: unwrap to access T's fields
    if (tl_monotype_is_const(struct_type)) {
        struct_type = tl_monotype_const_target(struct_type);
    }

#if DEBUG_TYPE_ALIAS
    if (ast_node_is_symbol(left) && tl_type_registry_is_type_alias(self->registry, ast_node_str(left))) {
        str st_str2 = tl_monotype_to_string(self->transient, struct_type);
        fprintf(stderr,
                "[DEBUG_TYPE_ALIAS] infer_struct_access: after subst, struct_type=%s is_inst=%d\n",
                str_cstr(&st_str2), tl_monotype_is_inst(struct_type));
    }
#endif

    if (tl_monotype_is_inst(struct_type)) {
        // Note: this handling of nfas supports terms like: `obj.fun_ptr()` where a field called
        // fun_ptr is a function pointer.
        ast_node *nfa = null;
        if (ast_node_is_nfa(right)) {
            nfa   = right;
            right = right->named_application.name;
        }
        ensure_tv(self, &right->type);
        if (ast_node_is_symbol(right)) {
            str                       field_name = right->symbol.name;
            tl_type_constructor_inst *inst       = struct_type->cons_inst;
            i32 found = tl_monotype_type_constructor_field_index(struct_type, field_name);

            dbg(self, "searched struct '%s' field name %s", str_cstr(&inst->def->name),
                str_cstr(&field_name));

            if (found != -1) {
                if (!inst->args.size) {
                    // empty struct
                    if (constrain_pm(self, right->type, struct_type, node)) return 1;
                    if (constrain_pm(self, node->type, struct_type, node)) return 1;
                    if (constrain(self, node->type, right->type, node)) return 1;
                    goto end_struct_access_op;
                }

                if ((u32)found >= inst->args.size) fatal("out of range");
                tl_monotype *field_type = inst->args.v[found];
                if (nfa) {
                    tl_monotype *result_type;
                    if (tl_monotype_is_arrow(field_type)) {
                        result_type = tl_monotype_arrow_result(field_type);
                    } else {
                        // Field type is not yet an arrow (e.g., a type variable).
                        result_type = tl_monotype_create_fresh_tv(self->subs);
                    }
                    // right = nfa's name
                    if (constrain_pm(self, right->type, field_type, node)) return 1;
                    if (constrain_pm(self, nfa->type, result_type, node)) return 1;
                    if (constrain_pm(self, node->type, result_type, node)) return 1;
                } else {
                    if (constrain_pm(self, right->type, field_type, node)) return 1;

                    if (tl_monotype_is_inst_of(field_type, S("CArray"))) {
                        tl_monotype *target   = field_type->cons_inst->args.v[0];
                        tl_monotype *ptr_type = tl_type_registry_ptr(self->registry, target);
                        if (constrain_pm(self, node->type, ptr_type, node)) return 1;
                    } else {
                        if (constrain_pm(self, node->type, field_type, node)) return 1;
                        if (constrain(self, node->type, right->type, node)) return 1;
                    }
                }
            } else {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_field_not_found, .node = right}));
                return 1;
            }

        } else {
            // not a symbol
            fatal("unreachable");
        }
    }

    else {
        // struct type is not a type constructor
        dbg(self, "warning: infer struct access without a struct type");
#if DEBUG_TYPE_ALIAS
        if (ast_node_is_symbol(left)) {
            str left_name2 = ast_node_str(left);
            int is_alias2  = tl_type_registry_is_type_alias(self->registry, left_name2);
            str st_str3    = tl_monotype_to_string(self->transient, struct_type);
            fprintf(stderr,
                    "[DEBUG_TYPE_ALIAS] infer_struct_access: FALLTHROUGH left='%s' struct_type=%s is_alias=%d\n",
                    str_cstr(&left_name2), str_cstr(&st_str3), is_alias2);
            if (is_alias2) {
                tl_polytype *alias_poly = tl_type_registry_get(self->registry, left_name2);
                if (alias_poly) {
                    str alias_str = tl_polytype_to_string(self->transient, alias_poly);
                    fprintf(stderr,
                            "[DEBUG_TYPE_ALIAS]   alias points to: %s\n", str_cstr(&alias_str));
                }
            }
        }
#endif
    }

end_struct_access_op:
    // always substitute operands immediately
    // tl_polytype_substitute(self->arena, node->binary_op.left->type, self->subs);
    // tl_polytype_substitute(self->arena, node->binary_op.right->type, self->subs);
    return 0;
}

static void maybe_handle_null(tl_infer *self, ast_node *node) {
    // Note: special case: if `null` appears and there is no node type yet, or if it's not a Ptr, assign a
    // Ptr(tv). The reason we do this is to assist non-annotated nodes such as struct fields that are
    // initialised to null. Without this handling, Foo(ptr = Null) would need to be Foo(ptr: Ptr(T) = null).
    //
    // Note: special case: if `void` appears, we assign a fresh type variable. The transpiler will detect
    // void nodes and leave the struct field uninitialised.

    if (ast_node_is_nil(node) || ast_node_is_void(node)) {
        if (!node->type || !tl_monotype_is_ptr(node->type->type)) {
            ast_node_type_set(node, tl_polytype_create_fresh_tv(self->arena, self->subs));
        }
    }
}

static int resolve_node(tl_infer *self, ast_node *node, traverse_ctx *ctx, node_position pos) {
    // Note: ctx may be null if this processing is initiated from load_toplevel()

    if (!node) return 0;

    switch (pos) {

    case npos_toplevel:
    case npos_formal_parameter:
        if (!ast_node_is_symbol(node)) {
#if DEBUG_RESOLVE
            fprintf(stderr, "[DEBUG resolve_node] ERROR: npos_toplevel/formal_parameter expected symbol\n");
#endif
            return expected_symbol(self, node);
        }

        {
            int res = process_annotation(
              self, ctx, node,
              (annotation_opts){
                .add_to_lexicals     = 1, // Add type args (e.g., T in `x: T`) to lexical_names
                .check_type_arg_self = 1, // Handle self-referential type args
              });
            if (res < 0) {
#if DEBUG_RESOLVE
                str name = ast_node_is_symbol(node) ? node->symbol.name : S("<non-symbol>");
                fprintf(stderr,
                        "[DEBUG resolve_node] ERROR: npos_toplevel/formal_parameter process_annotation "
                        "failed for '%s'\n",
                        str_cstr(&name));
#endif
                return 1;
            }
        }

        if (ctx) {
            // Add the symbol's own name to lexical_names (distinct from type args added above)
            str_hset_insert(&ctx->lexical_names, node->symbol.name);
        }

        // Sync with existing symbol, if any.
        sync_with_env(self, ctx, node, 0);
        break;

    case npos_function_argument:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, node);

        // handle null/void, if any
        maybe_handle_null(self, node);

        if (ast_node_is_symbol(node) || ast_node_is_nfa(node)) {
            if (!ctx) fatal("logic error");

            // Note: type literals as arguments are no longer supported. They must be supplied as explicit
            // type arguments.
            sync_with_env(self, ctx, node, 1);

            //             // Try to parse as type literal; if successful, wrap in tl_literal
            //             int res = process_annotation(self, ctx, node, (annotation_opts){.wrap_in_literal
            //             = 1}); if (res < 0) {
            // #if DEBUG_RESOLVE
            //                 str name = ast_node_is_symbol(node) ? node->symbol.name : S("<nfa>");
            //                 fprintf(
            //                   stderr,
            //                   "[DEBUG resolve_node] ERROR: npos_function_argument process_annotation
            //                   failed for '%s'\n", str_cstr(&name));
            // #endif
            //                 return 1;
            //             }
            //             if (res == 0) {
            //                 // Not a type literal: ensure fresh type variable and update environment
            //                 sync_with_env(self, ctx, node, 1);
            //             }
            //             // If res > 0 (was type literal): do not sync_with_env
        }

        break;

    case npos_assign_lhs:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, node);

        // Support annotations on lhs of field name assignments
        if (ast_node_is_symbol(node)) {
            if (!ctx) fatal("logic error");
            // No special opts - just parse the annotation if present, which mutates the ctx
            int res = process_annotation(self, ctx, node, (annotation_opts){0});
            if (res < 0) {
#if DEBUG_RESOLVE
                fprintf(stderr,
                        "[DEBUG resolve_node] ERROR: npos_assign_lhs process_annotation failed for '%s'\n",
                        str_cstr(&node->symbol.name));
#endif
                return 1;
            }
            ensure_tv(self, &node->type);
        }
        break;

    case npos_reassign_lhs:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, node);

        // Take symbol's existing type: this ensures let-in symbols retain their type info through
        // subsequent mutations.
        sync_with_env(self, ctx, node, 0);
        break;

    case npos_field_name:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, node);

        // assign a fresh type variable to field name, because we can't know its generic instantiated type
        ensure_tv(self, &node->type);
        break;

    case npos_operand: {
        if (!ctx) fatal("logic error");

        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, node);

        maybe_handle_null(self, node);

        //         // Try to parse as type literal; if successful, wrap in tl_literal
        //         int res = process_annotation(self, ctx, node, (annotation_opts){.wrap_in_literal = 1});
        //         if (res < 0) {
        // #if DEBUG_RESOLVE
        //             str name = ast_node_is_symbol(node) ? node->symbol.name : S("<non-symbol>");
        //             fprintf(stderr, "[DEBUG resolve_node] ERROR: npos_operand process_annotation failed
        //             for '%s'\n",
        //                     str_cstr(&name));
        // #endif
        //             return 1;
        //         }

        // always sync with environment
        sync_with_env(self, ctx, node, 0);
    } break;

    case npos_value_rhs:

        maybe_handle_null(self, node);

        // Ensure fresh type variable for any rhs value node
        sync_with_env(self, ctx, node, 1);
        break;
    }

#if DEBUG_RESOLVE
    if (ast_node_is_symbol(node)) {
        str name = ast_node_str(node);
        str tmp  = tl_polytype_to_string(self->transient, node->type);

        fprintf(stderr, "resolve_node '%s' : %s\n", str_cstr(&name), str_cstr(&tmp));
    }

    str node_str = v2_ast_node_to_string(self->transient, node);
    str mono_str = tl_monotype_to_string(self->transient, node->type->type);
    dbg(self, "resolve_node pos %i:  %s : %s", pos, str_cstr(&node_str), str_cstr(&mono_str));
#endif

    return 0;
}

static int is_type_literal(tl_infer *self, traverse_ctx const *ctx, ast_node const *node) {
    // If the node has any assignment arguments (e.g., Wrapper[Int](v = 1.0)),
    // it's a value constructor call, not a type literal.
    if (ast_node_is_nfa(node)) {
        ast_arguments_iter iter = ast_node_arguments_iter((ast_node *)node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (ast_node_is_assignment(arg)) {
                return 0; // has value arguments, not a type literal
            }
        }
    }

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_monotype *mono = tl_type_registry_parse_type_out_ctx(self->registry, node, self->transient,
                                                            ctx->type_arguments, &parse_ctx);
#if DEBUG_EXPLICIT_TYPE_ARGS
    if (mono && ast_node_is_nfa(node)) {
        str name     = ast_node_str(node->named_application.name);
        str mono_str = tl_monotype_to_string(self->transient, mono);
        fprintf(stderr, "[DEBUG EXPLICIT TYPE ARGS] is_type_literal: %s parsed as type: %s\n",
                str_cstr(&name), str_cstr(&mono_str));
    }
#endif
    return !!mono;
}

static int check_type_predicate(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {

    // Note: special case: using type predicate operator `::` as an attribute predicate
    if (ast_attribute_set == node->type_predicate.rhs->tag) {
        if (!ast_node_is_symbol(node->type_predicate.lhs)) {
            return expected_symbol(self, node->type_predicate.lhs);
        }
        // lookup symbol attributes in type environment
        ast_node *sym_attributes = str_map_get_ptr(self->attributes, node->type_predicate.lhs->symbol.name);

        ast_node **want_attributes   = node->type_predicate.rhs->attribute_set.nodes;
        u8         want_attributes_n = node->type_predicate.rhs->attribute_set.n;

        int        found_all         = 1; // default true
        for (u8 i = 0; i < want_attributes_n; i++) {
            ast_node *want      = want_attributes[i];
            u64       want_hash = ast_node_hash(want);
            if (0 == want_hash) fatal("runtime error"); // 0 hash illegal and breaks logic here

            int found_one = 0;
            if (sym_attributes && ast_attribute_set == sym_attributes->tag) {
                for (u8 j = 0; j < sym_attributes->attribute_set.n; j++) {
                    ast_node *one           = sym_attributes->attribute_set.nodes[j];
                    u64       has_hash      = ast_node_hash(one);
                    u64       let_name_hash = 0;

                    // also support general match of NFA names, e.g. `[[nfa(123)]] foo := 1 foo :: [[nfa]]`
                    if (ast_node_is_nfa(one)) let_name_hash = ast_node_hash(one->let.name);

                    if (has_hash == want_hash || (let_name_hash == want_hash)) {
                        found_one = 1;
                        break;
                    }
                }
            }
            if (!found_one) {
                found_all = 0;
                break;
            }
        }

        node->type_predicate.is_valid = found_all;
        return 0;
    }

    // Check if LHS is a type argument (pattern: T :: ConcreteType)
    if (ast_node_is_symbol(node->type_predicate.lhs)) {
        str          lhs_name     = ast_node_str(node->type_predicate.lhs);
        tl_monotype *lhs_type_arg = str_map_get_ptr(traverse_ctx->type_arguments, lhs_name);

        if (lhs_type_arg) {
            // LHS is a type argument - handle it specially
            tl_type_registry_parse_type_ctx parse_ctx;
            tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, traverse_ctx->type_arguments);

            tl_monotype *rhs_type =
              tl_type_registry_parse_type_with_ctx(self->registry, node->type_predicate.rhs, &parse_ctx);

            tl_monotype *lhs_mono = lhs_type_arg;

            if (!tl_monotype_is_concrete(lhs_mono)) {
                tl_monotype_substitute(self->arena, lhs_mono, self->subs, null);
                if (!tl_monotype_is_concrete(lhs_mono)) {
                    log_subs(self);
                    return unresolved_type_error(self, node->type_predicate.lhs);
                }
            }

            // Compare types using constrain with error suppression
            int save                        = self->is_constrain_ignore_error;
            self->is_constrain_ignore_error = 1;

            tl_polytype *lhs_poly           = tl_polytype_absorb_mono(self->arena, lhs_mono);
            if (!rhs_type || constrain_pm(self, lhs_poly, rhs_type, node)) {
                node->type_predicate.is_valid = 0;
            } else {
                node->type_predicate.is_valid = 1;
            }

            self->is_constrain_ignore_error = save;
            ast_node_type_set(node, tl_polytype_bool(self->arena, self->registry));
            return 0;
        }
    }
    // Fall through to existing expression handling...

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, traverse_ctx->type_arguments);

    tl_monotype *type =
      tl_type_registry_parse_type_with_ctx(self->registry, node->type_predicate.rhs, &parse_ctx);

    if (resolve_node(self, node->type_predicate.lhs, traverse_ctx, npos_operand)) {
        dbg(self, "assert resolve node failed");
        return 1;
    }
    tl_polytype *name_type = node->type_predicate.lhs->type;
    if (!tl_polytype_is_concrete(name_type)) {
        tl_polytype_substitute(self->arena, name_type, self->subs);
        if (!tl_polytype_is_concrete(name_type)) {
            log_subs(self);
            return unresolved_type_error(self, node->type_predicate.lhs);
        }
    }

    // Rather than generate a type error during compilation, we now treat the node as a boolean. Set flag to
    // ignore constraint errors.
    {
        int save                        = self->is_constrain_ignore_error;
        self->is_constrain_ignore_error = 1;

        if (!type || constrain_pm(self, node->type_predicate.lhs->type, type, node)) {
            node->type_predicate.is_valid = 0;
        } else {
            node->type_predicate.is_valid = 1;
        }

        self->is_constrain_ignore_error = save;
    }

    ast_node_type_set(node, tl_polytype_bool(self->arena, self->registry));
    return 0;
}

int is_union_struct(tl_infer *self, str name);

// ============================================================================
// Type Constraint Generation
// ============================================================================

static int infer_traverse_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (null == node) return 0;

#if DEBUG_RESOLVE
    str node_str = v2_ast_node_to_string(self->transient, node);
    fprintf(stderr, "infer_traverse_cb: %s:  %s\n", ast_tag_to_string(node->tag), str_cstr(&node_str));
#endif

    switch (node->tag) {

    case ast_attribute_set:
        // not traversed
        break;

    case ast_type_predicate: return infer_literal_type(self, node, tl_type_registry_bool);

    case ast_nil:
        // tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
        return infer_nil(self, node);

    case ast_void:
        // else handled by maybe_handle_null()
        return infer_void(self, traverse_ctx, node);

    case ast_c_string: return infer_literal_type(self, node, tl_type_registry_ptr_char);
    case ast_string:   fatal("logic error"); // parser should not generate
    case ast_char:     return infer_literal_type(self, node, tl_type_registry_char);
    case ast_f64:      return infer_literal_type(self, node, tl_type_registry_float);
    case ast_i64:      return infer_literal_type(self, node, tl_type_registry_int);
    case ast_u64: // FIXME unsigned
        return infer_literal_type(self, node, tl_type_registry_int);
    case ast_bool:      return infer_literal_type(self, node, tl_type_registry_bool);
    case ast_body:      return infer_body(self, node);
    case ast_case:      return infer_case(self, traverse_ctx, node);
    case ast_return:    return infer_return(self, traverse_ctx, node);

    case ast_binary_op: return infer_binary_op(self, traverse_ctx, node);
    case ast_unary_op:  return infer_unary_op(self, traverse_ctx, node);
    case ast_let_in:    return infer_let_in(self, traverse_ctx, node);

    case ast_symbol:
        // When resolving a symbol, we need to know what context it's in. This is specified by its parent
        // node, and is communicated via the `node_pos` field in traverse_ctx.
        if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) return 1;
        assert(node->type);
        break;

    case ast_named_function_application: return infer_named_function_application(self, traverse_ctx, node);
    case ast_lambda_function_application:
        return infer_lambda_function_application(self, traverse_ctx, node);
    case ast_lambda_function:      return infer_lambda_function(self, traverse_ctx, node);
    case ast_if_then_else:         return infer_if_then_else(self, node);

    case ast_tuple:                return infer_tuple(self, node);

    case ast_user_type_definition: break;

    case ast_assignment:           return infer_assignment(self, traverse_ctx, node);

    case ast_reassignment:
    case ast_reassignment_op:      return infer_reassignment(self, traverse_ctx, node);

    case ast_continue:
        // use 'any' for continue so it can unify with any other conditional arm
        return infer_continue(self, node);

    case ast_while:        return infer_while(self, node);

    case ast_let:          // intentionally not processed
    case ast_hash_command:
    case ast_arrow:
    case ast_ellipsis:
    case ast_eof:
    case ast_type_alias:   break;
    }

    return 0;
}

static name_and_type make_instance_key(tl_infer *self, str generic_name, tl_monotype *arrow,
                                       ast_node_sized type_arguments, hashmap *outer_type_arguments) {

    // TODO: we shouldn't make a new one every time this is called.
    tl_type_registry_parse_type_ctx parse_ctx;
    tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, outer_type_arguments);

    tl_monotype_sized type_arg_types = {
      .size = type_arguments.size,
      .v    = alloc_malloc(self->transient, type_arguments.size * sizeof(tl_monotype *)),
    };

    forall(i, type_arguments) {
        ast_node *type_arg  = type_arguments.v[i];
        type_arg_types.v[i] = tl_type_registry_parse_type_with_ctx(self->registry, type_arg, &parse_ctx);
        if (!type_arg_types.v[i]) continue;

        if (!tl_monotype_is_concrete(type_arg_types.v[i])) {
            // attempt to substitute
            tl_monotype_substitute(self->arena, type_arg_types.v[i], self->subs, null);
        }

        if (!tl_monotype_is_concrete(type_arg_types.v[i])) {
            // Non-concrete type arguments are nulled out so they don't contribute to the
            // instance key hash.  This is safe because concretize_params (called from
            // clone_generic_for_arrow) now resolves type parameters positionally from the
            // callsite's explicit type argument AST nodes.  Before that fix, two calls
            // with different non-concrete type args (e.g. sizeof[Point_T]() vs
            // sizeof[Rect_T]()) would produce identical keys and incorrectly share a
            // single specialization.
            type_arg_types.v[i] = null;
        }
    }

    name_and_type key = {
      .name_hash      = str_hash64(generic_name),
      .type_hash      = tl_monotype_hash64(arrow),
      .type_args_hash = tl_monotype_sized_hash64(hash64("args", 4), type_arg_types),
    };

#if DEBUG_INSTANCE_CACHE
    {
        str arrow_str = tl_monotype_to_string(self->transient, arrow);
        fprintf(stderr, "[INSTANCE_KEY] name='%s' arrow='%s' n_type_args=%u\n", str_cstr(&generic_name),
                str_cstr(&arrow_str), type_arguments.size);
        fprintf(stderr, "  -> name_hash=%016llx type_hash=%016llx type_args_hash=%016llx\n",
                (unsigned long long)key.name_hash, (unsigned long long)key.type_hash,
                (unsigned long long)key.type_args_hash);
        forall(i, type_arg_types) {
            str ta_str = tl_monotype_to_string(self->transient, type_arg_types.v[i]);
            fprintf(stderr, "  type_arg[%u] = '%s' (hash=%016llx)\n", i, str_cstr(&ta_str),
                    (unsigned long long)tl_monotype_hash64(type_arg_types.v[i]));
        }
        if (outer_type_arguments) {
            fprintf(stderr, "  outer_type_arguments present (size=%zu)\n", map_size(outer_type_arguments));
        }
    }
#endif

    return key;
}

static str *instance_lookup(tl_infer *self, name_and_type *key) {
    return map_get(self->instances, key, sizeof *key);
}

static str *instance_lookup_arrow(tl_infer *self, str generic_name, tl_monotype *arrow,
                                  ast_node_sized type_arguments, hashmap *outer_type_arguments) {
    if (!tl_monotype_is_concrete(arrow)) return null;

    // de-duplicate instances: hashes give us structural equality (barring hash collisions), which we need
    // because types are frequently cloned.
    name_and_type key = make_instance_key(self, generic_name, arrow, type_arguments, outer_type_arguments);
    str          *result = instance_lookup(self, &key);

#if DEBUG_INSTANCE_CACHE
    if (result) {
        fprintf(stderr, "[CACHE HIT] '%s' -> '%s'\n", str_cstr(&generic_name), str_cstr(result));
    } else {
        fprintf(stderr, "[CACHE MISS] '%s'\n", str_cstr(&generic_name));
    }
#endif

    return result;
}

static int instance_name_exists(tl_infer *self, str instance_name) {
    // NB: here, the set is keyed by _instance_ name, not generic name.
    return str_hset_contains(self->instance_names, instance_name);
}

static void instance_add(tl_infer *self, name_and_type *key, str instance_name) {
#if DEBUG_INSTANCE_CACHE
    size_t count_before = map_size(self->instances);
    fprintf(stderr, "[INSTANCE ADD] '%s' (cache size: %zu -> %zu)\n", str_cstr(&instance_name),
            count_before, count_before + 1);
    fprintf(stderr, "  key: name_hash=%016llx type_hash=%016llx type_args_hash=%016llx\n",
            (unsigned long long)key->name_hash, (unsigned long long)key->type_hash,
            (unsigned long long)key->type_args_hash);
#endif
    map_set(&self->instances, key, sizeof *key, &instance_name);
    str_hset_insert(&self->instance_names, instance_name);
}

static str specialize_type_constructor_(tl_infer *self, str name, tl_monotype_sized args,
                                        tl_polytype **out_type, hashmap **seen) {
    if (out_type) *out_type = null;

#if DEBUG_RECURSIVE_TYPES
    {
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_type_constructor_ ENTER: name='%s' n_args=%u\n",
                str_cstr(&name), args.size);
        forall(i, args) {
            str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   arg[%u] = %s  (is_inst=%d, is_inst_spec=%d)\n", i,
                    str_cstr(&arg_str), tl_monotype_is_inst(args.v[i]),
                    tl_monotype_is_inst(args.v[i]) ? tl_monotype_is_inst_specialized(args.v[i]) : 0);
        }
    }
#endif

    // do not specialize if it's an enum
    {
        ast_node *utd = toplevel_get(self, name);
        if (utd && ast_node_is_enum_def(utd)) return str_empty();
    }

    if (1) {
        name_and_type key = {.name_hash = str_hash64(name), .type_hash = tl_monotype_sized_hash64(0, args)};
        if (hset_contains(*seen, &key, sizeof key)) {
#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES]   CYCLE DETECTED in 'seen' for name='%s' "
                    "(name_hash=%016llx, type_hash=%016llx)\n",
                    str_cstr(&name), (unsigned long long)key.name_hash, (unsigned long long)key.type_hash);
#endif
            return str_empty();
        }
        hset_insert(seen, &key, sizeof key);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   added to 'seen': name='%s'\n", str_cstr(&name));
#endif
    }

    // To keep track of monotypes that are recursive references to the type being specialized.
    tl_monotype_ptr_array recur_refs = {.alloc = self->transient};

    // specialize args first
    forall(i, args) {
        if (tl_monotype_is_inst(args.v[i]) && !tl_monotype_is_inst_specialized(args.v[i])) {
            tl_polytype *poly         = null;
            str          generic_name = args.v[i]->cons_inst->def->generic_name;

            // Do not recurse: fixup after
            if (str_eq(name, generic_name)) {
#if DEBUG_RECURSIVE_TYPES
                fprintf(stderr,
                        "[DEBUG_RECURSIVE_TYPES]   DIRECT SELF-REF: arg[%u] '%s' matches name='%s'\n", i,
                        str_cstr(&generic_name), str_cstr(&name));
#endif
                {
                    tl_monotype **_t = &args.v[i];
                    array_push(recur_refs, _t);
                }
                continue;
            }

            // // Do not recurse into pointer target: fixup after
            if (tl_monotype_is_ptr(args.v[i])) {
                tl_monotype *target = tl_monotype_ptr_target(args.v[i]);
                if (tl_monotype_is_inst(target) && str_eq(name, target->cons_inst->def->generic_name)) {
#if DEBUG_RECURSIVE_TYPES
                    fprintf(
                      stderr,
                      "[DEBUG_RECURSIVE_TYPES]   PTR SELF-REF: arg[%u] Ptr to '%s' matches name='%s'\n", i,
                      str_cstr(&generic_name), str_cstr(&name));
#endif
                    {
                        tl_monotype **_t = &args.v[i]->cons_inst->args.v[0];
                        array_push(recur_refs, _t);
                    }
                    continue;
                }
            }

#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES]   RECURSE for arg[%u]: generic_name='%s' (parent='%s')\n", i,
                    str_cstr(&generic_name), str_cstr(&name));
#endif
            (void)specialize_type_constructor_(self, generic_name, args.v[i]->cons_inst->args, &poly, seen);
#if DEBUG_RECURSIVE_TYPES
            {
                str poly_str =
                  poly ? tl_polytype_to_string(self->transient, poly) : str_init(self->transient, "(null)");
                fprintf(
                  stderr, "[DEBUG_RECURSIVE_TYPES]   RECURSE result arg[%u] '%s': poly=%s concrete=%d\n", i,
                  str_cstr(&generic_name), str_cstr(&poly_str), poly ? tl_polytype_is_concrete(poly) : 0);
            }
#endif
            if (poly && tl_polytype_is_concrete(poly)) {
                args.v[i] = tl_polytype_concrete(poly);
            }
        }
    }

    str                             out_str   = str_empty();
    str                             name_inst = next_instantiation(self, name); // may be cancelled later
    tl_type_registry_specialize_ctx inst_ctx =
      tl_type_registry_specialize_begin(self->registry, name, name_inst, args);

#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   registry_begin: name='%s' name_inst='%s' specialized=%p\n",
            str_cstr(&name), str_cstr(&name_inst), (void *)inst_ctx.specialized);
    if (inst_ctx.specialized) {
        str spec_str = tl_monotype_to_string(self->transient, inst_ctx.specialized);
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   specialized type = %s\n", str_cstr(&spec_str));
    }
#endif

#if DEBUG_EXPLICIT_TYPE_ARGS
    fprintf(stderr, "[DEBUG specialize_type_constructor_] name=%s\n", str_cstr(&name));
    fprintf(stderr, "  inst_ctx.specialized = %p\n", (void *)inst_ctx.specialized);
#endif

    if (!inst_ctx.specialized) {
#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "  -> cancel: inst_ctx.specialized is null\n");
#endif
        goto cancel;
    }
    if (!tl_monotype_is_inst(inst_ctx.specialized)) fatal("runtime error");

    name_and_type key      = make_instance_key(self, name, inst_ctx.specialized, (ast_node_sized){0}, null);
    str          *existing = instance_lookup(self, &key);
    if (existing) {
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   EXISTING instance: '%s' -> '%s'\n", str_cstr(&name),
                str_cstr(existing));
#endif
        tl_polytype *poly = tl_type_env_lookup(self->env, *existing);
        if (out_type) *out_type = poly;
        out_str = *existing;

#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "  -> cancel: existing instance found: %s\n", str_cstr(existing));
#endif
        goto cancel;
    }

    // Look up generic type using the generic_name field, not the name parameter, because the latter may be
    // a type alias.
    ast_node *utd = toplevel_get(self, inst_ctx.specialized->cons_inst->def->generic_name);
#if DEBUG_EXPLICIT_TYPE_ARGS
    {
        str gn = inst_ctx.specialized->cons_inst->def->generic_name;
        fprintf(stderr, "  generic_name for toplevel_get: '%s' (len=%zu, hash=%llu)\n", str_cstr(&gn),
                str_len(gn), (unsigned long long)str_hash64(gn));
        fprintf(stderr, "  utd = %p, toplevels = %p\n", (void *)utd, (void *)self->toplevels);
    }
#endif
    if (!utd) {
#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "  -> cancel: utd not found\n");
#endif
        goto cancel;
    }

    instance_add(self, &key, name_inst);

    utd = ast_node_clone(self->arena, utd);
    ast_node_name_replace(utd->user_type_def.name, name_inst);
    utd->type = tl_polytype_absorb_mono(self->arena, inst_ctx.specialized);
    toplevel_add(self, name_inst, utd);
    tl_type_env_insert(self->env, name_inst, utd->type);
    tl_infer_set_attributes(self, utd->user_type_def.name);
    array_push(self->synthesized_nodes, utd);

    assert(tl_monotype_is_inst_specialized(utd->type->type));
    tl_polytype *save_type = utd->type;
    if (out_type) *out_type = utd->type; // Note: this helps the transpiler

#if DEBUG_EXPLICIT_TYPE_ARGS
    fprintf(stderr, "[DEBUG specialize] Added synthesized node: %s\n", str_cstr(&name_inst));
#endif

    // fixup recur refs
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   FIXUP: %u recur_refs for '%s'\n", recur_refs.size,
            str_cstr(&name));
    if (recur_refs.size) {
        str fixup_str = tl_monotype_to_string(self->transient, utd->type->type);
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   fixup target = %s\n", str_cstr(&fixup_str));
    }
#endif
    forall(i, recur_refs) {
        *recur_refs.v[i] = utd->type->type;
    }
    array_free(recur_refs);

    tl_type_registry_specialize_commit(self->registry, inst_ctx);
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   COMMIT: '%s' -> '%s'\n", str_cstr(&name),
            str_cstr(&name_inst));
#endif

    // rename variables: also erases type information
    {
        rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};
        rename_variables(self, utd, &ctx, 0);

        // restore type, for the transpiler
        utd->type = save_type;
    }

    return name_inst;

cancel:
    cancel_last_instantiation(self);
    return out_str;
}

static str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args,
                                       tl_polytype **out_type) {

#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_type_constructor ENTRY: name='%s' n_args=%u\n",
            str_cstr(&name), args.size);
    forall(i, args) {
        str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   arg[%u] = %s\n", i, str_cstr(&arg_str));
    }
#endif
    hashmap *seen = hset_create(self->transient, 64);
    str      out  = specialize_type_constructor_(self, name, args, out_type, &seen);
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_type_constructor RESULT: name='%s' => '%s'\n",
            str_cstr(&name), str_is_empty(out) ? "(empty)" : str_cstr(&out));
#endif
    return out;
}

int is_union_struct(tl_infer *self, str name) {
    ast_node *utd = toplevel_get(self, name);
    if (utd && ast_node_is_union_def(utd)) return 1;
    return 0;
}

static int specialize_value_arguments(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node,
                                      tl_monotype_sized expected_types);

static int specialize_user_type(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // divert if type constructor application is actually a type literal
    if (0 == type_literal_specialize(self, node)) return 0;

    if (!ast_node_is_nfa(node)) return 0;

    str name = node->named_application.name->symbol.name;
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_user_type ENTRY: name='%s'\n", str_cstr(&name));
#endif

    tl_monotype_array arr       = {.alloc = self->transient};
    tl_monotype_sized arr_sized = {0};

    // Check if type being constructed is concrete. If so, we want to take its arguments' concrete types
    // rather than instantiate into new type variables.
    tl_polytype *existing = tl_type_registry_get(self->registry, name);
    if (existing && tl_polytype_is_concrete(existing)) {
        assert(tl_monotype_is_inst(existing->type));

        arr_sized = existing->type->cons_inst->args;
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr,
                "[DEBUG_RECURSIVE_TYPES] specialize_user_type: concrete existing for '%s' n_args=%u\n",
                str_cstr(&name), arr_sized.size);
#endif

        // If name is a type alias pointing to a concrete type, we want the transpiler to ignore the alias
        // name, and act as if the alias' target was referenced directly. This ensures the same type is used
        // in the generated C code, allowing variables to be assignable.
        if (tl_type_registry_is_type_alias(self->registry, name)) {
            name = existing->type->cons_inst->def->generic_name;
        }

    } else if (is_union_struct(self, name)) {
        // For unions, get args from the node's inferred type, not AST arguments.
        // Union constructions pass only one variant value, but specialization
        // needs all variant types from the inferred type.
        if (node->type && tl_monotype_is_inst(node->type->type)) {
            tl_monotype *mono = node->type->type;
            // tl_monotype_substitute(self->arena, mono, self->subs, null);
            arr_sized = mono->cons_inst->args;
        } else {
            return 0; // Type not ready yet
        }
    } else {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {

            tl_monotype *type_id = null;
            if ((type_id = tl_type_registry_parse_type(self->registry, arg))) {
                // a literal type
                {
                    fatal("oops: a type literal?");
                    array_push(arr, type_id);
                }
                continue;
            }

            // For struct field assignments with a Ptr annotation (cast), use the
            // annotation type instead of the value type for specialization.
            tl_polytype *arg_type = arg->type;
            if (ast_node_is_assignment(arg) && is_ptr_cast_annotation(arg->assignment.name)) {
                arg_type = arg->assignment.name->symbol.annotation_type;
            }

            tl_monotype *mono = null;
            if (!tl_polytype_is_concrete(arg_type)) {
                mono = tl_polytype_instantiate(self->arena, arg_type, self->subs);
                tl_monotype_substitute(self->arena, mono, self->subs, null); // needed
            } else {
                mono = arg_type->type;
            }

            array_push(arr, mono);
        }

        assert(arr.size == node->named_application.n_arguments);
        arr_sized = (tl_monotype_sized)array_sized(arr);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_user_type: iterated args for '%s' n_args=%u\n",
                str_cstr(&name), arr_sized.size);
        forall(j, arr_sized) {
            str arg_str = tl_monotype_to_string(self->transient, arr_sized.v[j]);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   arg[%u] = %s (concrete=%d)\n", j, str_cstr(&arg_str),
                    tl_monotype_is_concrete(arr_sized.v[j]));
        }
#endif
    }

    tl_polytype *special_type = null;
    str          name_inst    = specialize_type_constructor(self, name, arr_sized, &special_type);
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_user_type: result for '%s' => '%s' type=%p\n",
            str_cstr(&name), str_is_empty(name_inst) ? "(empty)" : str_cstr(&name_inst),
            (void *)special_type);
#endif
    if (str_is_empty(name_inst)) return 0;

    // update callsite
    ast_node_name_replace(node->named_application.name, name_inst);
    ast_node_set_is_specialized(node);
    if (special_type) {
        // fprintf(stderr, "specialize_user_type: replacing node type.\n");

        assert(tl_monotype_is_inst_specialized(special_type->type));

        // Note: For type constructors being specialized, we must always override the node type.
        ast_node_type_set(node, special_type);
    }

    // Specialize function pointer arguments in struct initialization.
    // This must be done for both generic and non-generic type constructors,
    // as function pointer arguments need to reference specialized function names.
    if (node->named_application.n_arguments > 0) {
        if (specialize_value_arguments(self, traverse_ctx, node, arr_sized)) {
            return 1;
        }
    }

    return 0;
}

static ast_node *get_infer_target(ast_node *node) {
    if (ast_node_is_let(node) || ast_node_is_lambda_function(node)) {
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

static int  specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node);

static void toplevel_name_replace(ast_node *node, str name_replace) {
    if (ast_node_is_let(node)) {
        ast_node_name_replace(node->let.name, name_replace);
        ast_node_set_is_specialized(node);
    } else if (ast_node_is_let_in_lambda(node)) {
        ast_node_name_replace(node->let_in.name, name_replace);
    } else if (ast_node_is_symbol(node)) {
        // no body
        ;
    } else {
        fatal("logic error");
    }
}

static void specialized_add_to_env(tl_infer *self, str inst_name, tl_monotype *mono) {
    // add to type environment
    if (!tl_monotype_is_concrete(mono)) {
        // Note: functions like c_malloc etc will not have concrete types but still need to exist in the
        // environment.
        str arrow_str = tl_monotype_to_string(self->transient, mono);
        dbg(self, "note: adding non-concrete type to environment: '%s' : %s", str_cstr(&inst_name),
            str_cstr(&arrow_str));
    }
    tl_type_env_insert_mono(self->env, inst_name, mono);
}

static void do_apply_subs(void *ctx, ast_node *node);

static void apply_subs_to_ast_node(tl_infer *self, ast_node *node) {
    ast_node_dfs(self, node, do_apply_subs);
}

static int post_specialize(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *special,
                           tl_monotype *callsite) {
    // Do this after creating a specialised function
    ast_node *infer_target = get_infer_target(special);
    if (infer_target) {
        // set result type into traverse_ctx
        if (callsite) {
            tl_monotype *result_type  = tl_monotype_arrow_result(callsite);
            traverse_ctx->result_type = result_type;
        }
        if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) {
            dbg(self, "note: post_specialize failed infer");
            return 1;
        }
        // Apply substitutions to AST before specialization, so types are concrete
        apply_subs_to_ast_node(self, infer_target);

#if DEBUG_INVARIANTS
        // Invariant: After infer + apply_subs, all types in a specialized function must be concrete.
        // Non-concrete types indicate type variable leakage between specializations.
        if (ast_node_is_let(infer_target) && ast_node_is_specialized(infer_target)) {
            str func_name = ast_node_str(infer_target->let.name);
            // Check parameter types
            ast_node_sized params = ast_node_sized_from_ast_array(infer_target);
            forall(i, params) {
                if (params.v[i]->type) {
                    tl_monotype *mono = params.v[i]->type->type;
                    if (mono && !tl_monotype_is_concrete(mono)) {
                        char detail[512];
                        str  mono_str   = tl_monotype_to_string(self->transient, mono);
                        str  param_name = ast_node_str(params.v[i]);
                        snprintf(
                          detail, sizeof detail,
                          "In specialized '%.*s': param '%.*s' has non-concrete type after apply_subs: %s",
                          str_ilen(func_name), str_buf(&func_name), str_ilen(param_name),
                          str_buf(&param_name), str_cstr(&mono_str));
                        report_invariant_failure(self, "post_specialize:apply_subs",
                                                 "All param types must be concrete after apply_subs",
                                                 detail, params.v[i]);
                    }
                }
            }
            // Check type parameter types
            for (u32 i = 0; i < infer_target->let.n_type_parameters; i++) {
                ast_node *tp = infer_target->let.type_parameters[i];
                if (tp->type) {
                    tl_monotype *mono = tp->type->type;
                    if (mono && !tl_monotype_is_concrete(mono)) {
                        char detail[512];
                        str  mono_str = tl_monotype_to_string(self->transient, mono);
                        str  tp_name  = ast_node_str(tp);
                        snprintf(detail, sizeof detail,
                                 "In specialized '%.*s': type param '%.*s' has non-concrete type after "
                                 "apply_subs: %s",
                                 str_ilen(func_name), str_buf(&func_name), str_ilen(tp_name),
                                 str_buf(&tp_name), str_cstr(&mono_str));
                        report_invariant_failure(self, "post_specialize:apply_subs",
                                                 "All type param types must be concrete after apply_subs",
                                                 detail, tp);
                    }
                }
            }
        }
#endif

        if (traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb)) {
            dbg(self, "note: post_specialize failed specialize");
            return 1;
        }

#if DEBUG_INVARIANTS
        // Invariant: After specialization, all specialized NFA type arguments must be concrete
        check_specialized_nfa_type_args(self, infer_target, "post_specialize");
#endif
    }
    return 0;
}

static void add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow);

static str  specialize_arrow(tl_infer *self, traverse_ctx *traverse_ctx, str name, tl_monotype *arrow,
                             ast_node_sized callsite_type_arguments) {

#if DEBUG_INVARIANTS
    // Invariant: The callsite arrow type must be concrete when entering specialization.
    // A non-concrete arrow means unresolved type variables from another context could leak in.
    if (!tl_monotype_is_concrete(arrow)) {
        char detail[512];
        str  arrow_str = tl_monotype_to_string(self->transient, arrow);
        snprintf(detail, sizeof detail, "Specializing '%.*s' with non-concrete callsite arrow: %s",
                 str_ilen(name), str_buf(&name), str_cstr(&arrow_str));
        report_invariant_failure(self, "specialize_arrow", "Callsite arrow must be concrete", detail, null);
    }
#endif

    // 1. Check if already specialized
    if (instance_name_exists(self, name)) return name;

    // 2. Check cache for this name+type combination
    hashmap *outer_type_args = traverse_ctx ? traverse_ctx->type_arguments : null;
    str     *found = instance_lookup_arrow(self, name, arrow, callsite_type_arguments, outer_type_args);
    if (found) return *found;

    // 2a. Check that name is valid
    ast_node *toplevel = toplevel_get(self, name);
    if (!toplevel) return str_empty();

    // 3. Create unique instance name(e.g., "identity_0")
    name_and_type key = make_instance_key(self, name, arrow, callsite_type_arguments, outer_type_args);
    str           inst_name = next_instantiation(self, name);
    instance_add(self, &key, inst_name);

    // 4. Clone generic function's AST
    ast_node *generic_node =
      clone_generic_for_arrow(self, toplevel, arrow, inst_name,
                              traverse_ctx ? traverse_ctx->type_arguments : null, callsite_type_arguments);

    // 5. Add to environment and toplevel
    specialized_add_to_env(self, inst_name, arrow);
    toplevel_add(self, inst_name, generic_node);
    tl_infer_set_attributes(self, toplevel_name_node(generic_node));
    dbg(self, "toplevel_add: %s", str_cstr(&inst_name));

    // 6. CRITICAL: Process the specialized function body
    ast_node *special = toplevel_get(self, inst_name);
    if (post_specialize(self, traverse_ctx, special, arrow)) return str_empty();
    return inst_name;
}

static int specialize_arrow_with_name(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *fun_name_node,
                                      tl_monotype *callsite, ast_node_sized callsite_type_arguments) {
    if (!tl_monotype_is_arrow(callsite)) return 0;

    str instance_name =
      specialize_arrow(self, traverse_ctx, ast_node_str(fun_name_node), callsite, callsite_type_arguments);
    if (str_is_empty(instance_name)) return 1;
    ast_node_name_replace(fun_name_node, instance_name);
    return 0;
}

static int specialize_operand(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // Here we handle function pointers in operand positions. When this is called after the function being
    // pointed to has been specialised, the arrow types will be concrete. We use those types to look up
    // (using specialize_arrow) the specialised version and replace the symbol name with the specialised
    // name. This ensures the transpiler refers to an existant concrete function rather than the generic
    // template.

    tl_polytype *value_type = node->type;

    // Note: important: need to substitute to ensure type is concrete if possible.
    // seems not needed now
    // tl_polytype_substitute(self->arena, value_type, self->subs);

    if (!value_type || !tl_monotype_is_arrow(value_type->type)) return 0;
    if (!tl_polytype_is_concrete(value_type)) return 0;
    if (!ast_node_is_symbol(node)) return 0;

    str value_name = ast_node_str(node);
    // TODO: function pointers with callsite type arguments
    str inst_name = specialize_arrow(self, traverse_ctx, value_name, value_type->type, (ast_node_sized){0});
    if (str_is_empty(inst_name)) return 0; // FIXME: ignores error
    ast_node_name_replace(node, inst_name);
    return 0;
}

static int specialize_let_in(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // Here we handle let fptr = id in ... function pointers.
    assert(ast_node_is_let_in(node));
    tl_polytype *name_type = node->let_in.name->type;

    if (!name_type || !tl_polytype_is_concrete(name_type)) return 0;
    return specialize_operand(self, traverse_ctx, node->let_in.value);
}

static int specialize_reassignment(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_assignment(node));
    return specialize_operand(self, traverse_ctx, node->assignment.value);
}

static int specialize_case(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_case(node));

    // Handle tagged union case: specialize variant types in conditions
    if (node->case_.is_union) {
        forall(i, node->case_.conditions) {
            ast_node *cond = node->case_.conditions.v[i];

            // Skip nil condition (else arm)
            if (ast_node_is_nil(cond)) continue;

            // Condition should be a symbol with annotation_type set by infer_case
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation_type) continue;

            tl_monotype *variant_type = cond->symbol.annotation_type->type;

            // Handle pointer types (for mutable case bindings)
            tl_monotype *inner_type = variant_type;
            if (tl_monotype_is_ptr(variant_type)) {
                inner_type = tl_monotype_ptr_target(variant_type);
            }

            // If the variant type is a generic inst that needs specialization
            if (tl_monotype_is_inst(inner_type) && !tl_monotype_is_inst_specialized(inner_type)) {
                str               generic_name = inner_type->cons_inst->def->generic_name;
                tl_monotype_sized args         = inner_type->cons_inst->args;
                tl_polytype      *special_type = null;

                str inst_name = specialize_type_constructor(self, generic_name, args, &special_type);
                if (!str_is_empty(inst_name) && special_type) {
                    // Update the annotation_type with the specialized type
                    if (tl_monotype_is_ptr(variant_type)) {
                        tl_monotype *new_ptr =
                          tl_type_registry_ptr(self->registry, tl_polytype_concrete(special_type));
                        cond->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, new_ptr);
                    } else {
                        cond->symbol.annotation_type = special_type;
                    }
                    ast_node_type_set(cond, cond->symbol.annotation_type);

                    // Update the annotation node's name to the specialized name
                    if (cond->symbol.annotation && ast_node_is_symbol(cond->symbol.annotation)) {
                        ast_node_name_replace(cond->symbol.annotation, inst_name);
                    }
                }
            }
        }
        return 0;
    }

    // Handle binary predicate case (non-union)
    if (!node->case_.binary_predicate) return 0;

    ast_node *predicate = node->case_.binary_predicate;
    if (!ast_node_is_symbol(predicate)) return 0; // FIXME: what about lambdas?
    if (!node->case_.conditions.size) return 0;

    tl_polytype *pred_arrow =
      make_binary_predicate_arrow(self, traverse_ctx, node->case_.expression, node->case_.conditions.v[0]);

    str predicate_name = ast_node_str(predicate);
    str inst_name =
      specialize_arrow(self, traverse_ctx, predicate_name, pred_arrow->type, (ast_node_sized){0});

    if (str_is_empty(inst_name)) return 0; // FIXME: ignores error
    ast_node_name_replace(predicate, inst_name);

    return 0;
}

static void specialize_type_alias(tl_infer *self, ast_node *node) {
    // if target of type alias is a generic instantiation, ensure it it is properly specialized.
    // load_toplevel will have generalized the target type and inserted it into the alias registry.
    // we will need to specialize it and replace it.
    assert(ast_node_is_type_alias(node));

    ast_node    *target = node->type_alias.target;
    tl_monotype *parsed = tl_type_registry_parse_type(self->registry, target);
#if DEBUG_TYPE_ALIAS
    {
        str name_dbg = toplevel_name(node);
        str type_dbg = tl_monotype_to_string(self->transient, parsed);
        fprintf(stderr,
                "[DEBUG_TYPE_ALIAS] specialize_type_alias: '%s' parsed=%s is_inst=%d is_concrete=%d\n",
                str_cstr(&name_dbg), str_cstr(&type_dbg), tl_monotype_is_inst(parsed),
                tl_monotype_is_concrete(parsed));
    }
#endif
    if (tl_monotype_is_inst(parsed) && tl_monotype_is_concrete(parsed)) {
        str name = toplevel_name(node);
        str tmp  = tl_monotype_to_string(self->transient, parsed);
        dbg(self, "specialize_type_alias: %s = %s", str_cstr(&name), str_cstr(&tmp));
        tl_type_registry_type_alias_insert(self->registry, name,
                                           tl_polytype_absorb_mono(self->arena, parsed));
        assert(tl_polytype_is_concrete(tl_type_registry_get(self->registry, name)));
    } else if (tl_monotype_is_inst(parsed)) {
        str name = toplevel_name(node);
        str tmp  = tl_monotype_to_string(self->transient, parsed);
        dbg(self, "specialize_type_alias: not concrete: %s = %s", str_cstr(&name), str_cstr(&tmp));
    }
}

static int is_toplevel_function_name(tl_infer *self, ast_node *arg) {
    str       arg_name = ast_node_str(arg);
    ast_node *top      = toplevel_get(self, arg_name);
    if (!top) return 0;
    if (top->type && !tl_monotype_is_arrow(top->type->type)) return 0;
    return 1;
}

static int specialize_value_arguments(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node,
                                      tl_monotype_sized expected_types) {
    // Visits arguments to check for symbols referring to toplevel functions.
    // When found, specializes the function according to the expected type.

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    u32                i = 0;
    while ((arg = ast_arguments_next(&iter))) {

        if (ast_node_is_assignment(arg))
            if (specialize_reassignment(self, traverse_ctx, arg)) return 1;

        // Handle let_in_lambda arguments: specialize the lambda via its name
        if (ast_node_is_let_in_lambda(arg)) {
            ast_node *name_node = arg->let_in.name;

            if (!ast_node_is_symbol(name_node)) fatal("runtime error");
            if (i >= expected_types.size) fatal("runtime error");

            tl_monotype *expected = expected_types.v[i];
            if (!tl_monotype_is_arrow(expected)) fatal("runtime error");

            str old_name = name_node->symbol.name;

            // Specialize the lambda argument
            if (specialize_arrow_with_name(self, traverse_ctx, name_node, expected, (ast_node_sized){0}))
                return 1;

            str new_name = name_node->symbol.name;

            // Update body references to use the specialized name: recall that hoisted lambdas have a unique
            // body: just a symbol with the mangled name of the hoised function. See
            // parser.c:maybe_wrap_lambda_function_in_let_in
            if (!str_eq(old_name, new_name)) {
                ast_node *body = arg->let_in.body;

                if (!ast_node_is_body(body)) fatal("runtime error");
                forall(j, body->body.expressions) {
                    ast_node *expr = body->body.expressions.v[j];
                    if (ast_node_is_symbol(expr) && str_eq(expr->symbol.name, old_name))
                        ast_node_name_replace(expr, new_name);
                }
            }

            goto next;
        }

        if (!ast_node_is_symbol(arg)) goto next;
        if (!is_toplevel_function_name(self, arg)) goto next;
        if (i >= expected_types.size) fatal("runtime error");
        if (specialize_arrow_with_name(self, traverse_ctx, arg, expected_types.v[i], (ast_node_sized){0}))
            return 1;

    next:
        ++i;
    }
    return 0;
}

static int specialize_arguments(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node,
                                tl_monotype *arrow) {
    // Visits arguments used in node (function call arguments, etc) to check for symbols which refer to
    // toplevel functions. When found, that function is specialized according to the callsite's expected
    // type.

    tl_monotype_sized app_args = tl_monotype_arrow_args(arrow)->list.xs;
    return specialize_value_arguments(self, traverse_ctx, node, app_args);
}

// ============================================================================
// Generic Function Specialization
// ============================================================================

static int specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (ast_node_is_nfa(node)) {
        str name = ast_node_str(node->named_application.name);

        dbg(self, "specialize_applications_cb: enter '%s'", str_cstr(&name));
    }

    // Important: resolve the node, so that traverse_ctx is properly updated, including type arguments. For
    // example, node could be a symbol which is a formal argument that carries an annotation referring to a
    // type variable.
    if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) {
        return 1;
    }

    // check for nullary type constructors
    if (ast_node_is_symbol(node)) return specialize_operand(self, traverse_ctx, node);
    // check for let_in nodes and assignments
    if (ast_node_is_let_in(node)) return specialize_let_in(self, traverse_ctx, node);
    if (ast_node_is_assignment(node)) return specialize_reassignment(self, traverse_ctx, node);
    if (ast_node_is_case(node)) return specialize_case(self, traverse_ctx, node);

    // check for type predicate
    if (ast_node_is_type_predicate(node)) return check_type_predicate(self, traverse_ctx, node);

    int is_anon = ast_node_is_lambda_application(node);

    // or else the remainder of this function handles nfas and anon lambda applications
    if (!ast_node_is_nfa(node) && !ast_node_is_lambda_application(node)) return 0;

    if (ast_node_is_specialized(node)) return 0;

    tl_polytype *callsite = null;
    if (!is_anon) {
        str name = ast_node_str(node->named_application.name);

        if (self->verbose) {
            str tmp = v2_ast_node_to_string(self->transient, node);
            dbg(self, "specialize_applications_cb: nfa '%s'", str_cstr(&tmp));
            str_deinit(self->transient, &tmp);
        }

        // do not process a second time
        if (ast_node_is_specialized(node)) return 0;

        // do not process intrinsic calls or their arguments
        if (is_intrinsic(name)) return 0;

        // may be too early, e.g. for pointers
        // but type aliases and type constructors are always available
        if (!toplevel_get(self, name) && !tl_type_registry_exists(self->registry, name)) {
            dbg(self, "specialize_applications_cb: skipping '%s'", str_cstr(&name));

            return 0; // too early
        }

        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) {
            // check if it's a type alias or type constructor in the registry
            type = tl_type_registry_get(self->registry, name);
            if (!type) return 0; // mutual recursion or variable holding function pointer
        }

        // divert if this is a type constructor
        if (tl_polytype_is_type_constructor(type)) return specialize_user_type(self, traverse_ctx, node);

        // remember this callsite is specialized
        ast_node_set_is_specialized(node);

        // if the generic function type is concrete with no weak vars and no arrow args, use its type rather
        // than callsite, because the callsite should not override any concrete constraints identified at
        // the time the function is defined.
        if (tl_polytype_is_concrete_no_weak(type)) {
            // however, if any of the args is an arrow type, it must follow the non-concrete path
            if (!tl_monotype_arrow_has_arrow(type->type)) {
                callsite = type;
                {
                    str tmp = tl_polytype_to_string(self->transient, type);
                    dbg(
                      self,
                      "specialize_applications_cb: type is concrete, ignoring callsite type. Concrete : %s",
                      str_cstr(&tmp));
                    str_deinit(self->transient, &tmp);
                }
            }
        }

        if (!callsite) {
            // Important: use _with variant to copy free variables info to the arrow, which is added to the
            // environment further down.
            callsite = make_arrow_with(self, traverse_ctx, node, type);
            if (!callsite) {
                return 1;
            }
        }

#if DEBUG_RECURSIVE_TYPES
        {
            str call_str = tl_polytype_to_string(self->transient, callsite);
            fprintf(
              stderr,
              "[DEBUG_RECURSIVE_TYPES] specialize_applications_cb: name='%s' callsite=%s concrete=%d\n",
              str_cstr(&name), str_cstr(&call_str), tl_polytype_is_concrete(callsite));
        }
#endif

#if DEBUG_SPECIALIZE
        str app_str = tl_polytype_to_string(self->transient, callsite);
        fprintf(stderr, "specialize application: callsite '%s' arrow: %s\n",
                str_cstr(&name) str_cstr(&app_str));

        {
            u32                             argc = node->named_application.n_type_arguments;
            ast_node                      **argv = node->named_application.type_arguments;

            tl_type_registry_parse_type_ctx parse_ctx;
            tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, null);

            tl_monotype_sized type_arg_types = {
              .size = argc,
              .v    = alloc_malloc(self->transient, argc * sizeof(tl_monotype *)),
            };

            for (u32 i = 0; i < argc; i++) {
                ast_node *type_arg = argv[i];
                type_arg_types.v[i] =
                  tl_type_registry_parse_type_with_ctx(self->registry, type_arg, &parse_ctx);
            }

            u64 type_args_hash = tl_monotype_sized_hash64(hash64("args", 4), type_arg_types);

            fprintf(stderr, "  type_args_hash = %llu\n", type_args_hash);
        }

#endif
        // Specialize type constructors appearing in explicit type arguments.
        // E.g., sizeof[Point[Int]]() needs Point[Int] specialized to Point_8.
#if DEBUG_RECURSIVE_TYPES
        if (node->named_application.n_type_arguments > 0) {
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES] specialize_applications_cb: '%s' has %u explicit type args\n",
                    str_cstr(&name), node->named_application.n_type_arguments);
        }
#endif
        for (u32 i = 0; i < node->named_application.n_type_arguments; i++) {
            ast_node *type_arg = node->named_application.type_arguments[i];
            if (ast_node_is_nfa(type_arg) && 0 == type_literal_specialize(self, type_arg)) {
                // type_literal_specialize sets the type on the NFA's name node, but
                // concretize_params and traverse_ctx_assign_type_arguments check the NFA
                // node itself. Propagate the type up so both paths find it.
                if (!type_arg->type && type_arg->named_application.name->type) {
                    ast_node_type_set(type_arg, type_arg->named_application.name->type);
                }
#if DEBUG_RECURSIVE_TYPES
                {
                    str ta_str = type_arg->type ? tl_polytype_to_string(self->transient, type_arg->type)
                                                : str_init(self->transient, "(null)");
                    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   type_arg[%u] after specialize: %s\n", i,
                            str_cstr(&ta_str));
                }
#endif
            }
        }

        // try to specialize
        ast_node_sized callsite_type_args = {.size = node->named_application.n_type_arguments,
                                             .v    = node->named_application.type_arguments};
#if DEBUG_RECURSIVE_TYPES
        {
            str arrow_str = tl_monotype_to_string(self->transient, callsite->type);
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES] specialize_applications_cb: about to specialize_arrow '%s' "
                    "concrete=%d arrow=%s\n",
                    str_cstr(&name), tl_monotype_is_concrete(callsite->type), str_cstr(&arrow_str));
        }
#endif
        if (specialize_arrow_with_name(self, traverse_ctx, node->named_application.name, callsite->type,
                                       callsite_type_args)) {
            dbg(self, "note: failed to specialize '%s'", str_cstr(&name));
            return 1;
        }
        // and recurse over any arguments which are toplevel functions
        if (specialize_arguments(self, traverse_ctx, node, callsite->type)) {
            dbg(self, "note: failed to specialize arguments of '%s'", str_cstr(&name));
            return 1;
        }

        dbg(self, "specialize_applications_cb done: nfa '%s'",
            str_cstr(&node->named_application.name->symbol.name));

    } else {
        dbg(self, "specialize_applications_cb: anon");
        callsite = make_arrow(self, traverse_ctx, ast_node_sized_from_ast_array(node), node, 0);

        concretize_params(self, node, callsite->type, null, (ast_node_sized){0});
        if (post_specialize(self, traverse_ctx, node->lambda_application.lambda, callsite->type)) {
            return 1;
        }
        if (specialize_arguments(self, traverse_ctx, node, callsite->type)) {
            return 1;
        }
    }

    return 0;
}

// --

static str next_variable_name(tl_infer *, str);

// Performs alpha-conversion on the AST to ensure all bound variables have globally unique names while
// preserving lexical scope. This simplifies later passes by removing name collision concerns.

static void rename_let_in(tl_infer *self, ast_node *node, rename_variables_ctx *ctx) {
    // For toplevel definitions, rename them and keep them in lexical scope.
    if (!ast_node_is_let_in(node)) return;

    str name = node->let_in.name->symbol.name;
    if (is_c_symbol(name)) return;

    str newvar = next_variable_name(self, name);
    ast_node_name_replace(node->let_in.name, newvar);

#if DEBUG_RENAME
    dbg(self, "rename %.*s => %.*s", str_ilen(node->let_in.name->symbol.original),
        str_buf(&node->let_in.name->symbol.original), str_ilen(node->let_in.name->symbol.name),
        str_buf(&node->let_in.name->symbol.name));
#endif

    str_map_set(&ctx->lex, name, &newvar);
}

// Helper to collect and rename type variables from annotation ASTs.
// Type variables are lowercase symbols that appear in type position (not parameter names).
// When a symbol has an annotation (like `f: (a) -> b`), `f` is a parameter name - skip it
// and only recurse into its annotation where the actual type variables are.
static void collect_annotation_type_vars(tl_infer *self, ast_node *node, rename_variables_ctx *ctx) {
    if (!node) return;

    if (ast_node_is_symbol(node)) {
        // If symbol has an annotation, it's a parameter name (like `f` in `f: (a) -> b`).
        // Skip the name itself, only recurse into the type annotation.
        if (node->symbol.annotation) {
            collect_annotation_type_vars(self, node->symbol.annotation, ctx);
            return;
        }

        // Bare symbol in type position - this is a type variable candidate
        str name = node->symbol.name;

        // Skip if already in lexical scope
        if (str_map_contains(ctx->lex, name)) return;

        // Skip uppercase names (concrete types like Int, Ptr, etc.)
        char first = str_len(name) > 0 ? str_buf(&name)[0] : 0;
        if (first >= 'A' && first <= 'Z') return;

        // Skip c_ prefixed names
        if (str_len(name) >= 2 && str_buf(&name)[0] == 'c' && str_buf(&name)[1] == '_') return;

        // This looks like a type variable - add it to the lexical scope
        str newvar = next_variable_name(self, name);
        str_map_set(&ctx->lex, name, &newvar);

#if DEBUG_RENAME
        fprintf(stderr, "rename type var %s => %s\n", str_cstr(&name), str_cstr(&newvar));
#endif
    }

    else if (ast_node_is_arrow(node)) {
        collect_annotation_type_vars(self, node->arrow.left, ctx);
        collect_annotation_type_vars(self, node->arrow.right, ctx);
    }

    else if (ast_node_is_tuple(node)) {
        for (u32 i = 0; i < node->tuple.n_elements; i++) {
            collect_annotation_type_vars(self, node->tuple.elements[i], ctx);
        }
    }

    else if (ast_node_is_nfa(node)) {
        // For type applications like Arr[a], collect from type arguments
        u32        argc = node->named_application.n_type_arguments;
        ast_node **argv = node->named_application.type_arguments;
        for (u32 i = 0; i < argc; i++) {
            collect_annotation_type_vars(self, argv[i], ctx);
        }
        // Also check regular arguments for nested annotations
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            collect_annotation_type_vars(self, arg, ctx);
        }
    }
}

static void rename_one_function_param(tl_infer *self, ast_node *param, rename_variables_ctx *ctx,
                                      int level) {
    if (ast_node_is_nfa(param)) {
        // T[a] vs T[Int] -- what to do?
        rename_one_function_param(self, param->named_application.name, ctx, level + 1);

        u32        argc = param->named_application.n_type_arguments;
        ast_node **argv = param->named_application.type_arguments;
        for (u32 i = 0; i < argc; i++) {
            rename_one_function_param(self, argv[i], ctx, level + 1);
        }

    } else if (ast_node_is_symbol(param)) {

        ast_node_type_set(param, null);

        str *found;

        if ((found = str_map_get(ctx->lex, param->symbol.name))) {
            ast_node_name_replace(param, *found);
#if DEBUG_RENAME
            fprintf(stderr, "rename %.*s => %.*s\n", str_ilen(param->symbol.original),
                    str_buf(&param->symbol.original), str_ilen(param->symbol.name),
                    str_buf(&param->symbol.name));
#endif
        } else if (param->symbol.is_mangled && (found = str_map_get(ctx->lex, param->symbol.original))) {
            // name was mangled because it conflicts with a toplevel name. But lexical rename is meant
            // to take precedence over mangling to match toplevel names.
            ast_node_name_replace(param, *found);
#if DEBUG_RENAME
            fprintf(stderr, "rename mangled %.*s => %.*s\n", str_ilen(param->symbol.original),
                    str_buf(&param->symbol.original), str_ilen(param->symbol.name),
                    str_buf(&param->symbol.name));
#endif
        } else {
            // a param or type argument seen for the first time: add renamed var to lexical scope

            str name   = param->symbol.name;
            str newvar = next_variable_name(self, name);
            ast_node_name_replace(param, newvar);
            str_map_set(&ctx->lex, name, &newvar);
            rename_variables(self, param, ctx, level + 1);

#if DEBUG_RENAME
            fprintf(stderr, "rename new %s => %s\n", str_cstr(&name), str_cstr(&newvar));
#endif
        }
    }
}

static hashmap *rename_function_params(tl_infer *self, ast_node *node, rename_variables_ctx *ctx,
                                       int level) {
    hashmap *save = map_copy(ctx->lex);

    // alpha conversion of type arguments
    if (ast_node_is_let(node)) {
        u32        argc = node->let.n_type_parameters;
        ast_node **argv = node->let.type_parameters;
        for (u32 i = 0; i < argc; i++) {
            rename_one_function_param(self, argv[i], ctx, level);
        }

        // Also collect type variables from the function's annotation (from forward declarations).
        // These type variables (like 'a', 'b' in `map(f: (a) -> b, arr: Arr[a]) -> Arr[b]`) need
        // to be added to the lexical scope so they get renamed consistently in the body.
        if (node->let.name && ast_node_is_symbol(node->let.name)) {
#if DEBUG_RENAME
            str       fn_name = node->let.name->symbol.name;
            ast_node *annot   = node->let.name->symbol.annotation;
            fprintf(stderr, "[DEBUG] rename_function_params: fn='%s', annotation=%p (tag=%d)\n",
                    str_cstr(&fn_name), (void *)annot, annot ? (int)annot->tag : -1);
#endif
            collect_annotation_type_vars(self, node->let.name->symbol.annotation, ctx);
        }
    }

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *param;
    while ((param = ast_arguments_next(&iter))) {
        rename_one_function_param(self, param, ctx, level);
    }

    return save;
}

// ============================================================================
// Variable Renaming (Alpha Conversion)
// ============================================================================

static void rename_variables(tl_infer *self, ast_node *node, rename_variables_ctx *ctx, int level) {
    // level should be 0 on entry. It is used to recognize toplevel let nodes which assign static values
    // that must remain in lexical scope throughout the program.

    if (null == node) return;

    // ensure all types are removed: important for the post-clone rename of functions being specialized.
    ast_node_type_set(node, null);

    // also clear types attached to any explicit type arguments
    {
        u32        argc = 0;
        ast_node **argv = null;
        if (ast_node_is_let(node)) {
            argc = node->let.n_type_parameters;
            argv = node->let.type_parameters;
        } else if (ast_node_is_nfa(node)) {
            argc = node->named_application.n_type_arguments;
            argv = node->named_application.type_arguments;
        } else if (ast_node_is_utd(node)) {
            argc = node->user_type_def.n_type_arguments;
            argv = node->user_type_def.type_arguments;
        }

        for (u32 i = 0; i < argc; i++) ast_node_type_set(argv[i], null);
    }

    switch (node->tag) {

    case ast_if_then_else:
        rename_variables(self, node->if_then_else.condition, ctx, level + 1);
        rename_variables(self, node->if_then_else.yes, ctx, level + 1);
        rename_variables(self, node->if_then_else.no, ctx, level + 1);
        break;

    case ast_let_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, ctx, level + 1);

        hashmap *save = null;
        str      name = node->let_in.name->symbol.name;
        if (is_c_symbol(name)) break;
        if (level) {
            // do not rename toplevel symbols again (see rename_let_in)
            str newvar = next_variable_name(self, name);

            // establish lexical scope of the let-in binding and recurse
            save = map_copy(ctx->lex);
            str_map_set(&ctx->lex, name, &newvar);

            rename_variables(self, node->let_in.name, ctx, level + 1);
        }

        rename_variables(self, node->let_in.body, ctx, level + 1);

        // restore prior scope
        if (save) {
            map_destroy(&ctx->lex);
            ctx->lex = save;
        }
    } break;

    case ast_symbol: {

        str *found;
        if (!ctx->is_field) {
            // Do not rename symbols found immediately after a struct access
            if ((found = str_map_get(ctx->lex, node->symbol.name))) {
                ast_node_name_replace(node, *found);
#if DEBUG_RENAME
                dbg(self, "rename %.*s => %.*s", str_ilen(node->symbol.original),
                    str_buf(&node->symbol.original), str_ilen(node->symbol.name),
                    str_buf(&node->symbol.name));
#endif
            } else if (node->symbol.is_mangled && (found = str_map_get(ctx->lex, node->symbol.original))) {
                // name was mangled because it conflicts with a toplevel name. But lexical rename is meant
                // to take precedence over mangling to match toplevel names.
                ast_node_name_replace(node, *found);
#if DEBUG_RENAME
                dbg(self, "rename mangled %.*s => %.*s", str_ilen(node->symbol.original),
                    str_buf(&node->symbol.original), str_ilen(node->symbol.name),
                    str_buf(&node->symbol.name));
#endif
            } else {
                // a free variable, a field name, a toplevel function name, etc
            }
        }

        // No matter what, reset field_name processing from this point forward
        ctx->is_field = 0;

        // ensure renamed symbols do not carry a type
        ast_node_type_set(node, null);
        node->symbol.annotation_type = null;

        // traverse into annotation too, to support type arguments.
        // Note: keep in sync with ast_let_in arm.
        rename_variables(self, node->symbol.annotation, ctx, level + 1);
    } break;

    case ast_lambda_function: {
        hashmap *save = rename_function_params(self, node, ctx, level);
        rename_variables(self, node->lambda_function.body, ctx, level + 1);
        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_let: {
        hashmap *save = rename_function_params(self, node, ctx, level);
        rename_variables(self, node->let.body, ctx, level + 1);
        ast_node_type_set(node->let.name, null);
        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_lambda_function_application: {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, ctx, level + 1);

        // establishes scope for lambda body
        rename_variables(self, node->lambda_application.lambda, ctx, level + 1);

    } break;

    case ast_named_function_application: {
        rename_variables(self, node->named_application.name, ctx, level + 1);

        // type arguments
        u32        argc = node->named_application.n_type_arguments;
        ast_node **argv = node->named_application.type_arguments;
        for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;

        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, ctx, level + 1);

    } break;

    case ast_user_type_definition: {
        // type arguments
        u32        argc = node->user_type_def.n_type_arguments;
        ast_node **argv = node->user_type_def.type_arguments;
        for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);

        // traverse into field annotations
        if (node->user_type_def.field_annotations) { // may be null for enums
            argc = node->user_type_def.n_fields;
            argv = node->user_type_def.field_annotations;
            for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);
        }

    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) rename_variables(self, arr.v[i], ctx, level + 1);
    } break;

    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:
        // Note: no longer rename lhs of assignment, because it is used for named arguments of type
        // constructors. However, the type must be erased, because cloning generic functions relies on
        // rename_variables to erase types.
        if (!node->assignment.is_field_name) rename_variables(self, node->assignment.name, ctx, level + 1);
        else {
            // Note: however, field names may now be annotated, so the annotations have to be processed.
            // This handles type variables in the field assignment annotation.
            if (ast_node_is_symbol(node->assignment.name))
                rename_variables(self, node->assignment.name->symbol.annotation, ctx, level + 1);

            ast_node_type_set(node->assignment.name, null);
        }

        rename_variables(self, node->assignment.value, ctx, level + 1);
        break;

    case ast_binary_op: {
        rename_variables(self, node->binary_op.left, ctx, level + 1);

        // Note: If op is a struct access operator (. or ->), signal it
        char const *op = str_cstr(&node->binary_op.op->symbol.name);
        if (is_struct_access_operator(op)) ctx->is_field = 1;
        rename_variables(self, node->binary_op.right, ctx, level + 1);
    } break;

    case ast_unary_op: rename_variables(self, node->unary_op.operand, ctx, level + 1); break;

    case ast_return:   rename_variables(self, node->return_.value, ctx, level + 1); break;

    case ast_while:
        rename_variables(self, node->while_.condition, ctx, level + 1);
        rename_variables(self, node->while_.update, ctx, level + 1);
        rename_variables(self, node->while_.body, ctx, level + 1);
        break;

    case ast_body:
        //
        forall(i, node->body.expressions) {
            rename_variables(self, node->body.expressions.v[i], ctx, level + 1);
        }
        break;

    case ast_case: {
        int is_union = node->case_.is_union;

        rename_variables(self, node->case_.expression, ctx, level + 1);
        rename_variables(self, node->case_.binary_predicate, ctx, level + 1);
        if (node->case_.conditions.size != node->case_.arms.size) fatal("runtime error");
        forall(i, node->case_.conditions) {
            hashmap *save = null;
            if (is_union && ast_node_is_symbol(node->case_.conditions.v[i])) {
                // node may be ast_nil for an else clause
                str name   = ast_node_str(node->case_.conditions.v[i]);
                str newvar = next_variable_name(self, name);

                // establish lexical scope of the union case binding
                save = map_copy(ctx->lex);
                str_map_set(&ctx->lex, name, &newvar);
            }

            rename_variables(self, node->case_.conditions.v[i], ctx, level + 1);
            rename_variables(self, node->case_.arms.v[i], ctx, level + 1);

            if (save) {
                map_destroy(&ctx->lex);
                ctx->lex = save;
            }
        }
    } break;

    case ast_type_predicate:
        //
        rename_variables(self, node->type_predicate.lhs, ctx, level + 1);
        // Also rename type argument references in RHS (e.g., T in "x :: T")
        rename_variables(self, node->type_predicate.rhs, ctx, level + 1);
        break;

    case ast_attribute_set:
    case ast_hash_command:
    case ast_continue:
    case ast_string:
    case ast_c_string:
    case ast_char:
    case ast_nil:
    case ast_void:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_u64:
    case ast_type_alias:    break;
    }
}

static void add_free_variables_to_arrow(tl_infer *, ast_node *, tl_polytype *);

static void concretize_params(tl_infer *self, ast_node *node, tl_monotype *callsite,
                              hashmap *type_arguments, ast_node_sized callsite_type_arguments) {
    if (ast_node_is_symbol(node)) return;

    ast_node      *body   = null;
    ast_node_sized params = {0};
    if (ast_node_is_let(node)) {
        body   = node->let.body;
        params = ast_node_sized_from_ast_array(node);
    } else if (ast_node_is_let_in_lambda(node)) {
        body   = node->let_in.value->lambda_function.body;
        params = ast_node_sized_from_ast_array(node->let_in.value);
    } else if (ast_node_is_lambda_application(node)) {
        body   = node->lambda_application.lambda->lambda_function.body;
        params = ast_node_sized_from_ast_array(node->lambda_application.lambda);
    } else {
        fatal("logic error");
    }

    // assign concrete types to parameters based on callsite arguments

    assert(tl_arrow == callsite->tag);
    assert(callsite->list.xs.size == 2);
    assert(tl_tuple == callsite->list.xs.v[0]->tag);
    tl_monotype_sized callsite_args = callsite->list.xs.v[0]->list.xs;
    assert(callsite_args.size == params.size);

    forall(i, params) {
        ast_node    *param         = params.v[i];
        tl_polytype *callsite_type = tl_polytype_absorb_mono(self->arena, callsite_args.v[i]);
        if (!ast_node_is_symbol(param)) fatal("runtime error");
        ast_node_type_set(param, callsite_type);

        // this ensures the environment is also updated, since symbol types are in the env
        env_insert_constrain(self, ast_node_str(param), callsite_type, param);

        // Force-update the env entry to the concrete callsite type.  env_insert_constrain only
        // constrains (unifies) when the name already exists, which keeps the old type-variable
        // entry and loses metadata such as free-variable lists.  Overwriting ensures the env
        // carries the full callsite type including free variables.
        tl_type_env_insert(self->env, ast_node_str(param), callsite_type);
    }

    // assign concrete types to type parameters based on explicit type arguments from callsite
    if (type_arguments && ast_node_is_let(node)) {
#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "[DEBUG CONCRETIZE TYPE PARAMS] node has %u type params, type_arguments=%p\n",
                node->let.n_type_parameters, (void *)type_arguments);
#endif
        for (u32 i = 0; i < node->let.n_type_parameters; i++) {
            ast_node *type_param = node->let.type_parameters[i];
            assert(ast_node_is_symbol(type_param));

            // Always use the alpha-converted name, not the original, because the type
            // environment relies on alpha conversion to prevent pollution between generic
            // and specialized phases.
            str          param_name = type_param->symbol.name;
            tl_monotype *bound_type = str_map_get_ptr(type_arguments, param_name);

            // If direct lookup failed (because clone's alpha-converted name differs from caller's),
            // resolve positionally through the callsite type arguments. The callsite NFA's i-th type
            // argument corresponds to this clone's i-th type parameter.
            if (!bound_type && i < callsite_type_arguments.size) {
                ast_node *callsite_arg = callsite_type_arguments.v[i];

                // Try 1: If the callsite type arg is a symbol, look up its name in type_arguments.
                // This bridges from the caller's alpha-converted name to the concrete type.
                if (ast_node_is_symbol(callsite_arg)) {
                    bound_type = str_map_get_ptr(type_arguments, callsite_arg->symbol.name);
                }

                // Try 2: If the callsite type arg has a resolved type on it, use that.
                if (!bound_type && callsite_arg->type) {
                    tl_monotype *resolved = tl_monotype_clone(self->arena, callsite_arg->type->type);
                    tl_monotype_substitute(self->arena, resolved, self->subs, null);
                    if (tl_monotype_is_concrete(resolved)) {
                        bound_type = resolved;
                    }
                }

                // Try 3: Parse the callsite type arg node using the type_arguments context.
                if (!bound_type) {
                    tl_type_registry_parse_type_ctx parse_ctx;
                    tl_type_registry_parse_type_ctx_init(self->transient, &parse_ctx, type_arguments);
                    tl_monotype *parsed =
                      tl_type_registry_parse_type_with_ctx(self->registry, callsite_arg, &parse_ctx);
                    if (parsed) {
                        tl_monotype_substitute(self->arena, parsed, self->subs, null);
                        if (tl_monotype_is_concrete(parsed)) {
                            bound_type = parsed;
                        }
                    }
                }
            }

#if DEBUG_EXPLICIT_TYPE_ARGS
            fprintf(stderr, "[DEBUG CONCRETIZE TYPE PARAMS] type_param[%u]: name='%s', bound=%p\n", i,
                    str_cstr(&param_name), (void *)bound_type);
#endif

            if (bound_type) {

                tl_polytype *callsite_type = tl_polytype_absorb_mono(self->arena, bound_type);
#if DEBUG_EXPLICIT_TYPE_ARGS
                str type_str = tl_polytype_to_string(self->transient, callsite_type);
                fprintf(stderr, "[DEBUG CONCRETIZE TYPE PARAMS] setting type on '%s' to: %s\n",
                        str_cstr(&type_param->symbol.name), str_cstr(&type_str));
#endif
                ast_node_type_set(type_param, callsite_type);

                // Mirror the handling of value parameters: insert into env
                env_insert_constrain(self, param_name, callsite_type, type_param);
                tl_type_env_insert(self->env, param_name, callsite_type);
            }
        }
    }

    tl_monotype *inst_result = tl_monotype_sized_last(callsite->list.xs);
    body->type               = tl_polytype_absorb_mono(self->arena, inst_result);
}

static str next_variable_name(tl_infer *self, str name) {
    char buf[64];
    if (0 == str_cmp_nc(name, "tl_", 3))
        snprintf(buf, sizeof buf, "%s_v%u", str_cstr(&name), self->next_var_name++);
    else snprintf(buf, sizeof buf, "tl_%s_v%u", str_cstr(&name), self->next_var_name++);
    return str_init(self->arena, buf);
}

static str next_instantiation(tl_infer *self, str name) {
    if (str_len(name) < 128 - 24) {
        char buf[128];
        snprintf(buf, sizeof buf, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
        return str_init(self->arena, buf);
    } else {
        size_t len = str_len(name) + 24;
        char  *buf = alloc_malloc(self->transient, len);
        snprintf(buf, len, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
        str out = str_init(self->arena, buf);
        alloc_free(self->transient, buf);
        return out;
    }
}

static void cancel_last_instantiation(tl_infer *self) {
    self->next_instantiation--;
}

static tl_polytype *make_arrow_result_type(tl_infer *self, traverse_ctx *ctx, ast_node_sized args,
                                           tl_polytype *result_type, int is_parameters) {
    if (args.size == 0 || (args.size == 1 && ast_node_is_nil(args.v[0]))) {
        // always use a tuple on the left side of arrow, even if zero elements
        tl_monotype *lhs   = tl_monotype_create_tuple(self->arena, (tl_monotype_sized){0});
        tl_monotype *rhs   = result_type ? result_type->type : null;
        tl_monotype *arrow = tl_type_registry_create_arrow(self->registry, lhs, rhs);

        {
            str str = tl_monotype_to_string(self->transient, arrow);
            dbg(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_polytype_absorb_mono(self->arena, arrow);
    }

    else {
        tl_monotype_array args_types = {.alloc = self->arena};
        array_reserve(args_types, args.size);
        forall(i, args) {
            if (resolve_node(self, args.v[i], ctx,
                             is_parameters ? npos_formal_parameter : npos_function_argument))
                return null;

            tl_monotype *mono = args.v[i]->type->type;

            // make concrete if possible
            tl_monotype_substitute(self->arena, mono, self->subs, null);
            array_push(args_types, mono);
        }

        tl_monotype *left = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(args_types));
        tl_monotype *right = null;
        if (result_type) {
            right = result_type->type;
            // tl_monotype_substitute(self->arena, right, self->subs, null);
        } else {
            right = tl_type_registry_nil(self->registry);
        }

        tl_monotype *out = tl_type_registry_create_arrow(self->registry, left, right);

        {
            str str = tl_monotype_to_string(self->transient, out);
            dbg(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }

        return tl_polytype_absorb_mono(self->arena, out);
    }
}

static tl_polytype *make_arrow(tl_infer *self, traverse_ctx *ctx, ast_node_sized args, ast_node *result,
                               int is_parameters) {
    if (result) ensure_tv(self, &result->type);
    return make_arrow_result_type(self, ctx, args, result ? result->type : null, is_parameters);
}

static tl_polytype *make_arrow_with(tl_infer *self, traverse_ctx *ctx, ast_node *node, tl_polytype *type) {
    ast_arguments_iter iter = ast_node_arguments_iter(node); // not used for iter, just for args
    tl_polytype       *out  = make_arrow(self, ctx, iter.nodes, node, 0);

    if (!out) return null;
    if (tl_monotype_is_list(out->type) && tl_monotype_is_list(type->type)) {
        (out->type)->list.fvs = type->type->list.fvs;
#if DEBUG_RECURSIVE_TYPES
        {
            str out_str = tl_monotype_to_string(self->transient, out->type);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] make_arrow_with: arrow=%s fvs_count=%u\n",
                    str_cstr(&out_str), type->type->list.fvs.size);
            forall(i, type->type->list.fvs) {
                fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   fv[%u] = '%s'\n", i,
                        str_cstr(&type->type->list.fvs.v[i]));
            }
        }
#endif
    }
    return out;
}

static tl_polytype *make_binary_predicate_arrow(tl_infer *self, traverse_ctx *ctx, ast_node *lhs,
                                                ast_node *rhs) {

    ast_node_array args = {.alloc = self->arena};
    array_push(args, lhs);
    array_push(args, rhs);
    tl_monotype *bool_type = tl_type_registry_bool(self->registry);

    tl_polytype *bool_poly = tl_polytype_absorb_mono(self->arena, bool_type);
    tl_polytype *pred_arrow =
      make_arrow_result_type(self, ctx, (ast_node_sized)array_sized(args), bool_poly, 0);

    return pred_arrow;
}

typedef struct {
    str_array fvs;
} collect_free_variables_ctx;

static int can_be_free_variable(tl_infer *self, traverse_ctx *traverse_ctx, ast_node const *node) {
    if (!ast_node_is_symbol(node) || traverse_ctx->is_field_name) return 0;

    str name = ast_node_str(node);

    // don't collect symbols which are nullary type literals
    if (tl_type_registry_is_nullary_type(self->registry, name)) return 0;

    // don't collect symbols that start with c_
    if (0 == str_cmp_nc(name, "c_", 2)) return 0;

    // don't collect symbols that are already in lexical scope (e.g., union case bindings)
    if (str_hset_contains(traverse_ctx->lexical_names, name)) return 0;

    return 1;
}

static int collect_free_variables_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (ast_node_is_binary_op(node)) node = node->binary_op.left;
    if (!can_be_free_variable(self, traverse_ctx, node)) return 0;

    str name = ast_node_str(node);
    if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) return 1;

    collect_free_variables_ctx *ctx      = traverse_ctx->user;

    tl_polytype                *type     = tl_type_env_lookup(self->env, name);
    int                         is_arrow = type && tl_monotype_is_arrow(type->type);

    // Note: arrow types in the environment are global functions and are not free variables. Note that
    // even local let-in-lambda functions are also in the environment, but their names will never clash
    // with function names.
    if (is_arrow || traverse_ctx_is_param(traverse_ctx, name)) {
        // FIXME: arrow type may exist because of a forward declaration, but function definition may be
        // missing. Need to report an error.
        ;
    } else {
        // a free variable
        dbg(self, "collect_free_variables_cb: add '%s'", str_cstr(&name));
        str_array_set_insert(&ctx->fvs, name);
    }

    // if symbol has a type which carries fvs, we also collect those.
    if (is_arrow && !tl_polytype_is_scheme(type)) {
        str_sized type_fvs = tl_monotype_fvs(type->type);
        forall(i, type_fvs) {
            if (!traverse_ctx_is_param(traverse_ctx, type_fvs.v[i]))
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

    array_shrink(ctx.fvs);
    dbg(self, "-- free variables: %u --", ctx.fvs.size);
    forall(i, ctx.fvs) {
        dbg(self, "%.*s", str_ilen(ctx.fvs.v[i]), str_buf(&ctx.fvs.v[i]));
    }

    // find any sublists with free variables and bring them to the top
    promote_free_variables(&ctx.fvs, arrow->type);

    // add free variables to arrow type
    if (ctx.fvs.size) {
        tl_monotype_absorb_fvs(arrow->type, (str_sized)sized_all(ctx.fvs));

        // sort free variables
        tl_monotype_sort_fvs(arrow->type);
    }
}

static int generic_declaration(tl_infer *self, str name, ast_node const *name_node, ast_node *node) {
    // no function body, so let's treat this as a type declaration
    if (!name_node->symbol.annotation_type) {
        expected_type(self, node);
        return 1;
    }

    // must quantify arrow types
    if (tl_monotype_is_arrow(node->symbol.annotation_type->type)) {
        tl_polytype_generalize(node->symbol.annotation_type, self->env, self->subs);
    }
    tl_type_env_insert(self->env, name, node->symbol.annotation_type);
    tl_infer_set_attributes(self, node);
    return 0;
}

static int infer_one(tl_infer *self, ast_node *infer_target, tl_polytype *arrow) {
    // arrow is non-null only for let nodes
    if (arrow && !ast_node_is_let(infer_target) && !ast_node_is_lambda_function(infer_target))
        fatal("logic error");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    if (traverse_ast(self, traverse, infer_target, infer_traverse_cb)) return 1;

    // constrain arrow result type and infer target's type
    if (arrow) {
        if (tl_polytype_is_scheme(arrow)) fatal("logic error");
        ast_node *body = null;
        if (ast_node_is_let(infer_target)) body = infer_target->let.body;
        else if (ast_node_is_lambda_function(infer_target)) body = infer_target->lambda_function.body;
        if (!body) fatal("logic error");

        if (constrain_pm(self, body->type, tl_monotype_arrow_result(arrow->type), body)) return 1;
    }
    return 0;
}

static int add_generic(tl_infer *self, ast_node *node) {
    if (!node) return 0;

    // Handle body nodes early - they contain multiple definitions (e.g., from tagged union desugaring)
    if (ast_node_is_body(node)) {
        forall(i, node->body.expressions) {
            if (add_generic(self, node->body.expressions.v[i])) return 1;
        }
        return 0;
    }

    ast_node    *infer_target = get_infer_target(node);
    ast_node    *name_node    = toplevel_name_node(node);
    tl_polytype *provisional  = name_node->symbol.annotation_type;
    str          name         = name_node->symbol.name;
    str          orig_name    = name_node->symbol.original;

    tl_infer_set_attributes(self, name_node);

    // calculate provisional type, for recursive functions
    if (ast_node_is_let(node)) {
        if (!provisional) {
            // Note: special case: force main() to have a CInt result type
            if (str_eq(name, S("main"))) {
                provisional = make_arrow_result_type(self, null, ast_node_sized_from_ast_array(node),
                                                     tl_type_registry_get(self->registry, S("CInt")), 1);
            }

            else {
                provisional =
                  make_arrow(self, null, ast_node_sized_from_ast_array(node), node->let.body, 1);
            }
        }
    } else if (ast_node_is_let_in_lambda(node)) {
        if (!provisional)
            provisional = make_arrow(self, null, ast_node_sized_from_ast_array(infer_target),
                                     node->let_in.value->lambda_function.body, 1);
    } else if (ast_node_is_symbol(node)) {
        // toplevel symbol node, e.g. for declaration of intrinsics, or forward type annotations. They will
        // take precedence to any later declarations, so let's be careful
    } else if (ast_node_is_utd(node)) {
        // already loaded from load_toplevel
        return 0;
    } else if (ast_node_is_let_in(node)) {
        if (infer_one(self, infer_target, null)) {
            dbg(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
                str_ilen(orig_name), str_buf(&orig_name));
        }

        assert(node->let_in.value->type);
        tl_type_env_insert(self->env, name, node->let_in.value->type);
        ast_node_type_set(node->let_in.name, node->let_in.value->type);
        return 0;

    } else {
        fatal("logic error");
    }

    dbg(self, "-- add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    if (!infer_target) {
        // no function body, so let's treat this as a type declaration
        return generic_declaration(self, name, name_node, node);
    }

    // ensure provisional type is not quantified. If it is, instantiate it
    if (tl_polytype_is_scheme(provisional)) {
        provisional = tl_polytype_absorb_mono(
          self->arena, tl_polytype_instantiate(self->arena, provisional, self->subs));
    }

    // add provisional type to environment (for polymorphic recursion)
    if (provisional) {
        // Note: ensure this is not quantified until after inference
        tl_type_env_insert(self->env, name, provisional);
    }

    // run inference
    if (infer_one(self, infer_target, provisional)) {
        dbg(self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
            str_ilen(orig_name), str_buf(&orig_name));
        return 1;
    }

    // Must apply subs before quantifying, because we want to replace any tvs (that would otherwise be
    // quantified) with primitives if possible, or the same root of an equivalence class
    tl_type_subs_apply(self->subs, self->env);

    // get the arrow type from the annotation, or else from the result of inference
    tl_polytype *arrow = null;

    // get the arrow type from inference, or else from the annotation, if any
    arrow = tl_type_env_lookup(self->env, name);
    if (!arrow) arrow = name_node->symbol.annotation_type;
    // tl_polytype_substitute(self->arena, arrow, self->subs);
    if (!arrow) fatal("runtime error");

    tl_polytype_generalize(arrow, self->env, self->subs);

    // collect free variables from infer target and add to the generic's arrow type

    add_free_variables_to_arrow(self, infer_target, arrow);
    tl_type_env_insert(self->env, name, arrow);

    {
        str tmp = tl_polytype_to_string(self->transient, arrow);
        dbg(self, "-- done add_generic: %.*s (%.*s): type : %s --", str_ilen(name), str_buf(&name),
            str_ilen(orig_name), str_buf(&orig_name), str_cstr(&tmp));
        str_deinit(self->transient, &tmp);
    }

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
        dbg(self, "remove_generic_toplevels: removing '%s'", str_cstr(&names.v[i]));
        toplevel_del(self, names.v[i]);
    }
    array_free(names);
}

void tree_shake_toplevels(tl_infer *self, ast_node const *start) {
    hashmap  *used   = tree_shake(self, start);

    str_array remove = {.alloc = self->transient};

    // Add all toplevel let-in names (globals) and type names because we now use this process to determine
    // all symbols used in the program. This helps us identify nonexistent free variables.
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_let_in(node)) {
            str name = ast_node_str(node->let_in.name);
            str_hset_insert(&used, name);

            // recurse into toplevel let-in nodes
            hashmap *recur = tree_shake(self, node);
            map_merge(&used, recur);
            map_destroy(&recur);

        } else if (ast_node_is_utd(node)) {
            str name = ast_node_str(node->user_type_def.name);
            str_hset_insert(&used, name);
        }

        // Note: special case: preserve module init functions.
        else if (ast_node_is_let(node) && is_module_init(ast_node_str(node->let.name))) {
            str_hset_insert(&used, node->let.name->symbol.name);

            // recurse into toplevel module init functions
            hashmap *recur = tree_shake(self, node);
            map_merge(&used, recur);
            map_destroy(&recur);
        }
    }

    iter = (hashmap_iterator){0};
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;

        // preserve value let-ins, but not unused let-in-lambdas (including the latter causes a test
        // failure)
        if (ast_node_is_let_in(node) && !ast_node_is_let_in_lambda(node)) continue;

        str name = toplevel_name(node);
        if (!str_hset_contains(used, name)) array_push(remove, name);
    }

    forall(i, remove) {
        dbg(self, "tree_shake_toplevels: removing '%s'", str_cstr(&remove.v[i]));
        toplevel_del(self, remove.v[i]);
    }
    array_free(remove);

    // Note: also remove any unused name in the environment
    tl_type_env_remove_unknown_symbols(self->env, used);

    map_destroy(&used);
}

static int check_main_function(tl_infer *self, ast_node *main) {
    // instantiate and infer main
    assert(ast_node_is_let(main));
    tl_polytype *type = tl_type_env_lookup(self->env, S("main"));
    if (!type) fatal("main function with no type");

    // set is_specialized
    ast_node_set_is_specialized(main);

    tl_polytype *body_type = main->let.body->type;
    if (!body_type || tl_polytype_is_scheme(body_type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_main_function_bad_type, .node = main}));
        return 1;
    }

    // remove free variables from main type if they are toplevel (e.g. lambda functions)
    str_sized *fvs = &type->type->list.fvs;
    if (tl_monotype_is_arrow(type->type)) {
        for (u32 i = 0; i < fvs->size;) {
            str fv = fvs->v[i];
            if (toplevel_get(self, fv)) array_sized_erase(*fvs, i);
            else ++i;
        }
    }

    // report errors: main must have no free variables
    int error = 0;
    forall(i, *fvs) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_unknown_symbol_in_main, .message = fvs->v[i]}));
        ++error;
    }
    return error;
}

tl_monotype *tl_infer_update_specialized_type_(tl_infer *self, tl_monotype *mono, hashmap **in_progress) {

    // Note: this function pretty definitely breaks the isolation between tl_infer and the transpiler so
    // that makes me a little bit sad. But it makes sizeof(TypeConstructor) work.

    switch (mono->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:
    case tl_weak:        break;

    case tl_cons_inst:   {

        int did_replace  = !tl_monotype_is_inst_specialized(mono);
        str generic_name = mono->cons_inst->def->generic_name;

        // check args recursively
        str_hset_insert(in_progress, generic_name);
        forall(i, mono->cons_inst->args) {
            tl_monotype *arg = mono->cons_inst->args.v[i];
            if (!tl_monotype_is_inst(arg) ||
                !str_hset_contains(*in_progress, arg->cons_inst->def->generic_name)) {

                tl_monotype *replace = tl_infer_update_specialized_type_(self, arg, in_progress);

                if (replace) {
                    mono->cons_inst->args.v[i] = replace;
                    did_replace                = 1;
                }
            }
        }
        str_hset_remove(*in_progress, generic_name);

        tl_polytype *replace = null;
        (void)specialize_type_constructor(self, mono->cons_inst->def->generic_name, mono->cons_inst->args,
                                          &replace);

#if DEBUG_EXPLICIT_TYPE_ARGS
        {
            str gn = mono->cons_inst->def->generic_name;
            fprintf(stderr, "[DEBUG UPDATE_SPECIALIZED] specialize_type_constructor('%s'):\n",
                    str_cstr(&gn));
            fprintf(stderr, "  replace = %p\n", (void *)replace);
            if (replace) {
                str ts = tl_monotype_to_string(self->transient, replace->type);
                fprintf(stderr, "  replace->type = %s\n", str_cstr(&ts));
                fprintf(stderr, "  is_specialized = %d\n", tl_monotype_is_inst_specialized(replace->type));
            }
        }
#endif

        if (replace && !tl_monotype_is_inst_specialized(replace->type)) fatal("unreachable");

        if (replace && did_replace) {
            return replace->type;
        } else {
            return null;
        }

    } break;

    case tl_arrow:
    case tl_tuple: {
        int did_replace = 0;
        forall(i, mono->list.xs) {
            tl_monotype *replace = tl_infer_update_specialized_type_(self, mono->list.xs.v[i], in_progress);
            if (replace) {
                mono->list.xs.v[i] = replace;
                did_replace        = 1;
            }
        }
        if (did_replace) return mono;

    } break;
    }

    return null;
}

tl_monotype *tl_infer_update_specialized_type(tl_infer *self, tl_monotype *mono) {
    switch (mono->tag) {
    case tl_var:         tl_monotype_substitute(self->arena, mono, self->subs, null); break;

    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_weak:
    case tl_placeholder: return null;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:       {
        hashmap     *in_progress = hset_create(self->transient, 64);
        tl_monotype *out         = tl_infer_update_specialized_type_(self, mono, &in_progress);
        return out;
    }
    }
    return null;
}

typedef struct {
    hashmap *in_progress;
} update_types_ctx;

static void update_types_one_type(tl_infer *self, update_types_ctx *ctx, tl_polytype **poly) {
    if (!poly || !*poly) return; // not all ast nodes will have types

    // Don't try to specialize type schemes
    if (tl_polytype_is_scheme(*poly)) return;

    switch ((*poly)->type->tag) {
    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_var:
    case tl_weak:
    case tl_placeholder: return;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:       {
        // For recursive types, bounce until no changes. update_specialized_type returns null if there is no
        // need to replace the type being tested.
        int tries = 3;
        while (tries--) {
            int did_replace = 1;

            hset_reset(ctx->in_progress);
            tl_monotype *replace =
              tl_infer_update_specialized_type_(self, (*poly)->type, &ctx->in_progress);

            if (replace) *poly = tl_polytype_absorb_mono(self->arena, replace);
            else did_replace = 0;

            if (!did_replace) break;
        }
        if (-1 == tries) fatal("loop exhausted");
    }
    }
}

static void fixup_arrow_name(tl_infer *self, ast_node *ident) {
    if (ast_node_is_symbol(ident)) {
        tl_monotype *type = ident->type->type;
        if (!tl_monotype_is_arrow(type)) return;
        str name = ast_node_str(ident);

        // TODO: function pointers with type arguments
        str *inst_name = instance_lookup_arrow(self, name, type, (ast_node_sized){0}, null);
        if (inst_name) ast_node_name_replace(ident, *inst_name);
    }
}

static void update_types_arrow(tl_infer *self, ast_node *node) {
    if (ast_node_is_let_in(node)) {
        ast_node *ident = node->let_in.value;
        fixup_arrow_name(self, ident);
    }
}

static int update_types_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    update_types_ctx *ctx = traverse_ctx->user;
    update_types_one_type(self, ctx, &node->type);
    update_types_arrow(self, node);

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

    // propagate the types back up the ast, especially for type constructors
    switch (node->tag) {
    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:      ast_node_type_set(node, node->assignment.value->type); break;

    case ast_body:            {
        u32 n = node->body.expressions.size;
        if (n) ast_node_type_set(node, node->body.expressions.v[n - 1]->type);
    } break;

    case ast_let_in:
        if (node->let_in.body) ast_node_type_set(node, node->let_in.body->type);

        // Note: ensure name's type in the environment matches a specialized type constructor on the rhs
        {
            tl_monotype *value_type = node->let_in.value->type->type;

            if (tl_monotype_is_inst_specialized(value_type)) {
                tl_polytype *new_type = tl_polytype_absorb_mono(self->arena, value_type);
                ast_node_type_set(node->let_in.name, new_type);
                tl_type_env_insert(self->env, ast_node_str(node->let_in.name), new_type);
            }
        }
        break;

    default: break;
    }

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
    return 0;
}

static void update_specialized_types(tl_infer *self) {
    update_types_ctx ctx = {.in_progress = hset_create(self->transient, 64)};

    // Snapshot the env keys before iterating. update_types_one_type may trigger
    // specialize_type_constructor_ which inserts new entries into the env. Robin Hood
    // hashing can relocate existing entries on insertion, invalidating any data pointers
    // obtained from a prior map_iter call.
    str_array env_keys = str_map_keys(self->transient, self->env->map);
    forall(ki, env_keys) {
        tl_polytype *poly = tl_type_env_lookup(self->env, env_keys.v[ki]);
        if (!poly) continue;
        tl_polytype *orig = poly;
        update_types_one_type(self, &ctx, &poly);
        if (poly != orig) tl_type_env_insert(self->env, env_keys.v[ki], poly);
    }
    array_free(env_keys);

    // NOTE: this is an expensive traverse
    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    traverse->user         = &ctx;
    hashmap_iterator iter  = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        traverse_ast(self, traverse, node, update_types_cb);
        // Note: traverse_ast does not traverse let nodes directly (just their sub-parts)
    }
    arena_reset(self->transient);
}

static int check_unresolved_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (traverse_ctx->is_field_name) return 0;

    if (ast_node_is_let_in(node) && !tl_monotype_is_arrow(node->let_in.value->type->type)) {
        if (!tl_polytype_is_concrete(node->let_in.name->type)) {
            type_error(self, node->let_in.name);
            type_error(self, node);
        }
    } else if (ast_node_is_reassignment(node) && !tl_polytype_is_concrete(node->type)) {
        unresolved_type_error(self, node);
    }

    return 0;
}

static void check_unresolved_types(tl_infer *self) {
    // checks if any nodes in ast are still type variables
    traverse_ctx    *traverse = traverse_ctx_create(self->transient);
    hashmap_iterator iter     = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        traverse_ast(self, traverse, node, check_unresolved_cb);
    }
    arena_reset(self->transient);
}

// -- invariant checking --

#if DEBUG_INVARIANTS

static void report_invariant_failure(tl_infer *self, char const *phase, char const *invariant,
                                     char const *detail, ast_node const *node) {
    fprintf(stderr, "\nINVARIANT VIOLATION [%s]\n", phase);
    fprintf(stderr, "  Invariant: %s\n", invariant);
    fprintf(stderr, "  Detail:    %s\n", detail);
    if (node) {
        fprintf(stderr, "  Location:  %s:%u\n", node->file, node->line);
        str s = ast_node_to_short_string(self->transient, node);
        fprintf(stderr, "  Node:      %.*s\n", str_ilen(s), str_buf(&s));
        str_deinit(self->transient, &s);
    }
    fprintf(stderr, "\n");
}

static void check_types_null_cb(void *ctx_ptr, ast_node *node) {
    struct check_types_null_ctx *ctx = ctx_ptr;
    if (node->type != null) {
        char detail[256];
        snprintf(detail, sizeof detail, "Node has non-null type at %s:%u", node->file, node->line);
        report_invariant_failure(ctx->self, ctx->phase, "All AST node types must be null", detail, node);
        ctx->failures++;
    }
}

static int check_all_types_null(tl_infer *self, ast_node_sized nodes, char const *phase) {
    struct check_types_null_ctx ctx = {.self = self, .phase = phase, .failures = 0};
    forall(i, nodes) {
        ast_node_dfs(&ctx, nodes.v[i], check_types_null_cb);
    }
    return ctx.failures;
}

static void check_type_arg_types_null_one(struct check_types_null_ctx *ctx, ast_node *node) {
    // Check type parameters on let nodes
    if (ast_node_is_let(node)) {
        struct ast_let *let = &node->let;
        for (u8 i = 0; i < let->n_type_parameters; ++i) {
            if (let->type_parameters[i] && let->type_parameters[i]->type != null) {
                char detail[256];
                snprintf(detail, sizeof detail, "Type parameter %u has non-null type", i);
                report_invariant_failure(ctx->self, ctx->phase, "Type parameter types must be null", detail,
                                         let->type_parameters[i]);
                ctx->failures++;
            }
        }
    }
    // Check type arguments on named function applications
    else if (ast_node_is_nfa(node)) {
        struct ast_named_application *nfa = &node->named_application;
        for (u8 i = 0; i < nfa->n_type_arguments; ++i) {
            if (nfa->type_arguments[i] && nfa->type_arguments[i]->type != null) {
                char detail[256];
                snprintf(detail, sizeof detail, "Type argument %u has non-null type", i);
                report_invariant_failure(ctx->self, ctx->phase, "Type argument types must be null", detail,
                                         nfa->type_arguments[i]);
                ctx->failures++;
            }
        }
    }
    // Check type arguments on user type definitions
    else if (ast_node_is_utd(node)) {
        struct ast_user_type_def *utd = &node->user_type_def;
        for (u8 i = 0; i < utd->n_type_arguments; ++i) {
            if (utd->type_arguments[i] && utd->type_arguments[i]->type != null) {
                char detail[256];
                snprintf(detail, sizeof detail, "UTD type argument %u has non-null type", i);
                report_invariant_failure(ctx->self, ctx->phase, "Type argument types must be null", detail,
                                         utd->type_arguments[i]);
                ctx->failures++;
            }
        }
    }
}

static void check_type_arg_types_null_cb(void *ctx_ptr, ast_node *node) {
    check_type_arg_types_null_one(ctx_ptr, node);
}

static int check_type_arg_types_null(tl_infer *self, ast_node_sized nodes, char const *phase) {
    struct check_types_null_ctx ctx = {.self = self, .phase = phase, .failures = 0};
    forall(i, nodes) {
        ast_node_dfs(&ctx, nodes.v[i], check_type_arg_types_null_cb);
    }
    return ctx.failures;
}

static int check_no_generic_toplevels(tl_infer *self, char const *phase) {
    int              failures = 0;
    hashmap_iterator iter     = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (!ast_node_is_let(node)) continue;
        if (ast_node_is_specialized(node)) continue; // specialized is OK

        // Check if this function has type parameters (generic)
        struct ast_let *let = &node->let;
        if (let->n_type_parameters > 0) {
            // Generic function - check that it has been fully specialized
            str          name = ast_node_str(let->name);
            tl_polytype *poly = tl_type_env_lookup(self->env, name);
            if (poly && !tl_polytype_is_concrete(poly)) {
                str  tmp = tl_polytype_to_string(self->transient, poly);
                char detail[512];
                snprintf(detail, sizeof detail, "Generic function '%s' still has type variables: %s",
                         str_cstr(&name), str_cstr(&tmp));
                report_invariant_failure(self, phase, "No generic functions should remain", detail, node);
                failures++;
            }
        }
    }
    return failures;
}

// Check that specialized NFA type arguments have concrete types
static void check_specialized_nfa_type_args_cb(void *ctx_ptr, ast_node *node) {
    struct check_types_null_ctx *ctx = ctx_ptr;
    if (!ast_node_is_nfa(node)) return;
    if (!node->named_application.is_specialized) return;

    for (u8 i = 0; i < node->named_application.n_type_arguments; i++) {
        ast_node *ta = node->named_application.type_arguments[i];
        if (ta && ta->type && !tl_polytype_is_concrete(ta->type)) {
            char detail[256];
            str  type_str = tl_polytype_to_string(ctx->self->transient, ta->type);
            snprintf(detail, sizeof detail, "Type argument %u has non-concrete type: %s", i,
                     str_cstr(&type_str));
            report_invariant_failure(ctx->self, ctx->phase,
                                     "Specialized NFA type arguments must be concrete", detail, ta);
            ctx->failures++;
        }
    }
}

static int check_specialized_nfa_type_args(tl_infer *self, ast_node *node, char const *phase) {
    struct check_types_null_ctx ctx = {.self = self, .phase = phase, .failures = 0};
    ast_node_dfs(&ctx, node, check_specialized_nfa_type_args_cb);
    return ctx.failures;
}

#endif // DEBUG_INVARIANTS

int tl_infer_run(tl_infer *self, ast_node_sized nodes, tl_infer_result *out_result) {
    dbg(self, "-- start inference --");

    // Phase 1: Alpha-conversion - ensure unique variable names
    // Performs alpha-conversion on the AST to ensure all bound variables have globally unique names
    // while preserving lexical scope. This simplifies later passes by removing name collision concerns.
    {
        rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};
        // rename toplevel let-in symbols and keep them in global lexical scope
        forall(i, nodes) rename_let_in(self, nodes.v[i], &ctx);

        // rename the rest
        ctx = (rename_variables_ctx){.lex = ctx.lex};
        forall(i, nodes) rename_variables(self, nodes.v[i], &ctx, 0);
        arena_reset(self->transient);
    }

#if DEBUG_INVARIANTS
    // Invariant: After alpha conversion, all AST types must still be null
    if (check_all_types_null(self, nodes, "Phase 1: Alpha Conversion")) return 1;
    if (check_type_arg_types_null(self, nodes, "Phase 1: Alpha Conversion")) return 1;
    arena_reset(self->transient);
#endif

    // Phase 2: Load top-level definitions
    // Load all top level forms.
    self->toplevels = ast_node_str_map_create(self->arena, 1024);
    load_toplevel(self, nodes);
    arena_reset(self->transient);
    if (self->errors.size) return 1;

    dbg(self, "-- toplevels");
    log_toplevels(self);

    // Phase 3: Generic function type inference
    // now go through the toplevel let nodes and create generic functions: don't call add_generic from
    // inside the iteration because infer will add lambda functions to the toplevel.
    forall(i, nodes) {
        if (ast_node_is_hash_command(nodes.v[i])) continue;
        if (ast_node_is_type_alias(nodes.v[i])) continue;
        add_generic(self, nodes.v[i]);
    }
    arena_reset(self->transient);

    if (self->errors.size) return 1;

#if DEBUG_INVARIANTS
    // Invariant: After generic inference, all let-bound functions should have polytypes in env
    {
        int failures = 0;
        forall(i, nodes) {
            ast_node *node = nodes.v[i];
            if (!ast_node_is_let(node)) continue;
            str          name = ast_node_str(node->let.name);
            tl_polytype *poly = tl_type_env_lookup(self->env, name);
            if (!poly) {
                char detail[256];
                snprintf(detail, sizeof detail, "Function '%.*s' not found in type environment",
                         str_ilen(name), str_buf(&name));
                report_invariant_failure(self, "Phase 3: Generic Inference",
                                         "All functions must have polytypes in env", detail, node);
                failures++;
            }
        }
        if (failures) return 1;
        arena_reset(self->transient);
    }
#endif

    // Phase 4: Check free variables
    // check if free variables are present
    if (check_missing_free_variables(self)) return 1;
    if (self->errors.size) return 1;
    arena_reset(self->transient);

    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    dbg(self, "-- inference complete --");
    dbg(self, "");
    dbg(self, "-- toplevels");
    log_toplevels(self);
    if (1) {
        dbg(self, "-- subs");
        log_subs(self);
    }
    dbg(self, "-- env");
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

    // Phase 5: Generic function specialization
    // Final phase: communiate type information top-down by following applications. This contrasts with
    // the bottom-up inference we just completed. At this point the program is well-typed and we are
    // setting up for the transpiler.
    dbg(self, "-- specialize phase");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);

    if (main) {
        traverse_ast(self, traverse, main, specialize_applications_cb);
    } else {
        assert(self->opts.is_library);
        ast_node     *node         = null;
        traverse_ctx *traverse_ctx = traverse_ctx_create(self->transient);
        forall(i, nodes) {
            node = nodes.v[i];

            if (ast_node_is_let(node)) {
                ast_node *name     = toplevel_name_node(node);
                str       fun_name = ast_node_str(name);
                if (str_eq(fun_name, S("main"))) continue;
                tl_polytype *type = tl_type_env_lookup(self->env, fun_name);
                if (tl_polytype_is_concrete(type)) {
                    dbg(self, "library: exporting '%s'", str_cstr(&fun_name));
                    tl_polytype *callsite = make_arrow_with(self, traverse_ctx, node, type);
                    if (!callsite) {
                        dbg(self, "library: exporting '%s' failed: arrow", str_cstr(&fun_name));
                        continue;
                    }

                    str inst_name =
                      specialize_arrow(self, traverse_ctx, fun_name, callsite->type, (ast_node_sized){0});
                    // FIXME: ignores specialize_arrow error
                    dbg(self, "library: exporting '%s' => '%s'", str_cstr(&fun_name), str_cstr(&inst_name));
                }
            }
        }
    }

    // specialize toplevel nodes e.g. global values that may refer to functions by name
    forall(i, nodes) {
        ast_node *node = nodes.v[i];

        if (ast_node_is_let_in(node)) {
            traverse_ast(self, traverse, node, specialize_applications_cb);
        }
    }

    // specialize module init functions
    {
        // () -> Void
        tl_polytype *callsite = make_arrow_result_type(self, traverse, (ast_node_sized){0},
                                                       tl_polytype_nil(self->arena, self->registry), 0);
        forall(i, nodes) {
            ast_node *node = nodes.v[i];
            if (ast_node_is_let(node)) {
                str name = ast_node_str(node->let.name);
                if (is_module_init(name)) {
                    // These two things must be done in order for the transpiler to emit the function: It
                    // must be specialized and it must not have a generic type.
                    ast_node_set_is_specialized(node);
                    tl_type_env_insert(self->env, name, callsite);
                    tl_infer_set_attributes(self, node->let.name);
                    // recurse through init body as if we had specialized it
                    post_specialize(self, traverse, node, callsite->type);
                }
            }
        }
    }

    arena_reset(self->transient);

    // apply subs to global environment
    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    arena_reset(self->transient);

    // ensure main function has the correct type
    if (main) {
        if (check_main_function(self, main)) return 1;
        arena_reset(self->transient);
    }

    remove_generic_toplevels(self);
    arena_reset(self->transient);

#if DEBUG_INVARIANTS
    // Invariant: After specialization, no generic toplevels should remain
    if (check_no_generic_toplevels(self, "Phase 5: Specialization")) return 1;
    arena_reset(self->transient);
#endif

    // Phase 6: Tree shaking
    // tree shake
    if (main) {
        tree_shake_toplevels(self, main);
        arena_reset(self->transient);

        // after tree shake, extraneous symbols will have been removed from environment
        if (check_missing_free_variables(self)) return 1;
        if (self->errors.size) return 1;
    }

    // Phase 7: Type specialization updates
    // update type specialisations: replace generic constructors with specialised constructors.
    update_specialized_types(self);
    arena_reset(self->transient);

    check_unresolved_types(self);
    arena_reset(self->transient);

    if (1) {
        dbg(self, "-- final subs");
        log_subs(self);
    }
    dbg(self, "-- final env --");
    log_env(self);
    arena_reset(self->transient);
    dbg(self, "-- final toplevels");
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

#if DEBUG_INSTANCE_CACHE
    fprintf(stderr, "\n[INSTANCE CACHE SUMMARY]\n");
    fprintf(stderr, "  Total specializations: %zu\n", map_size(self->instances));
    fprintf(stderr, "  Unique instance names: %zu\n", hset_size(self->instance_names));
#endif

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
                if (node->file && *node->file)
                    fprintf(stderr, "%s:%u: %s: %.*s: %.*s\n", node->file, node->line,
                            tl_error_tag_to_string(err->tag), str_ilen(message), str_buf(&message),
                            str_ilen(node_str), str_buf(&node_str));
                else
                    fprintf(stderr, "%s: %.*s: %.*s\n", tl_error_tag_to_string(err->tag), str_ilen(message),
                            str_buf(&message), str_ilen(node_str), str_buf(&node_str));
            }

            else
                fprintf(stderr, "error: %s: %.*s\n", tl_error_tag_to_string(err->tag), str_ilen(message),
                        str_buf(&message));
        }
    }
}

//

static void dbg(tl_infer const *self, char const *restrict fmt, ...) {
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
    if (!self->verbose) return;
    str_array sorted = str_map_sorted_keys(self->transient, self->toplevels);
    forall(i, sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, sorted.v[i]);
        str       str;
        if (self->verbose_ast) str = v2_ast_node_to_string(self->transient, node);
        else str = ast_node_to_short_string(self->transient, node);
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
        tl_polytype_substitute(self->arena, node->type, self->subs);
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

int is_module_init(str name) {
    return str_ends_with(name, S("____init__0"));
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
    dbg(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

__attribute__((unused)) static void log_constraint_mono(tl_infer *self, tl_monotype *left,
                                                        tl_monotype *right, ast_node const *node) {
    if (!self->verbose) return;
    str left_str  = tl_monotype_to_string(self->transient, left);
    str right_str = tl_monotype_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    dbg(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

static void log_type_error(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    // Note: always print err to stderr
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);

    fprintf(stderr, "%s:%i: error: conflicting types: %s versus %s\n", node->file, node->line,
            str_cstr(&left_str), str_cstr(&right_str));
}
static void log_type_error_mm(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    tl_polytype l = tl_polytype_wrap((tl_monotype *)left), r = tl_polytype_wrap((tl_monotype *)right);
    log_type_error(self, &l, &r, node);
}

static void log_subs(tl_infer *self) {
    if (self->verbose) tl_type_subs_log(self->subs);
}
