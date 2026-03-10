// infer_internal.h — Shared internal header for infer_*.c compilation units.
// NOT a public header.  Only included by src/tess/src/infer*.c files.

#ifndef TESS_INFER_INTERNAL_H
#define TESS_INFER_INTERNAL_H

#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "error.h"
#include "hash.h"
#include "infer.h"
#include "parser.h"
#include "platform.h"
#include "str.h"
#include "type.h"

#include "ast.h"
#include "hashmap.h"

#include "type_registry.h"
#include "types.h"

#include <stdarg.h>
#include <stdio.h>

// ============================================================================
// Debug flags
// ============================================================================

#define DEBUG_RESOLVE            0
#define DEBUG_RENAME             0
#define DEBUG_CONSTRAIN          0
#define DEBUG_EXPLICIT_TYPE_ARGS 0
#define DEBUG_INVARIANTS         0 // Enable invariant checking at phase boundaries
#define DEBUG_INSTANCE_CACHE     0 // Log instance cache hits/misses and key components
#define DEBUG_RECURSIVE_TYPES    0 // Trace mutually recursive type specialization
#define DEBUG_TYPE_ALIAS         0 // Trace type alias registration and resolution

// ============================================================================
// Invariant checking forward declarations
// ============================================================================

#if DEBUG_INVARIANTS
struct check_types_null_ctx {
    struct tl_infer *self;
    char const      *phase;
    int              failures;
};

// Helper: check if a name is alpha-converted (contains "_v" followed by digits)
static inline int is_alpha_converted_name(str name) {
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

// ============================================================================
// Internal types
// ============================================================================

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
    hashmap             *traits;         // str => tl_trait_def* (trait registry)
    hashmap             *instances;      // u64 hash => str specialised name in env
    hashmap             *instance_names; // str set
    hashmap             *attributes;     // str => ast_node* attribute_set (possibly null)
    str_array            hash_includes;
    tl_infer_error_array errors;

    // Context for single-pass parsing of user type definitions
    tl_type_registry_parse_type_ctx type_parse_ctx;

    // Pre-allocated context for hot-path parse_type calls (reused via reinit).
    // REENTRANCY: this is shared mutable state.  Every callsite must reinit immediately before
    // use and capture the result into a local before calling anything that might re-enter
    // (e.g. specialize_type_constructor -> make_instance_key).  See hot_parse_ctx_reinit().
    tl_type_registry_parse_type_ctx hot_parse_ctx;
    hashmap                        *hot_parse_ctx_own_ta; // saved own type_arguments ptr
    int                             hot_parse_ctx_guard;  // reentrancy detector: 1 while in use

    hashmap *load_type_arguments; // Phase 2: type params for current toplevel annotation parse

    u32      next_var_name;
    u32      next_instantiation;

    int      verbose;
    int      verbose_ast;
    int      indent_level;

    int      is_constrain_ignore_error; // non-zero if no error should be reported during unification

    int      report_stats;

    tl_infer_phase_stats phase_stats;
    tl_infer_counters    counters;
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
    int           skip_alloc_expr; // skip alloc_expr traversal in lambda (for FV collection)
} traverse_ctx;

typedef int (*traverse_cb)(tl_infer *, traverse_ctx *, ast_node *);

typedef struct {
    hashmap *lex;
    int      is_field;
} rename_variables_ctx;

typedef struct {
    str_array fvs;
} collect_free_variables_ctx;

typedef struct {
    tl_monotype *parsed;
    hashmap     *type_arguments;
} annotation_parse_result;

typedef struct {
    int check_type_arg_self; // Check if name is in type_arguments (for formal params)
} annotation_opts;

// ============================================================================
// Internal API: infer.c (orchestration, public API, shared utilities)
// ============================================================================

void      toplevel_add(tl_infer *, str, ast_node *);
void      toplevel_del(tl_infer *, str);
ast_node *toplevel_get(tl_infer *, str);
ast_node *toplevel_iter(tl_infer *, hashmap_iterator *);
void      tl_infer_dbg(tl_infer const *, char const *restrict, ...);
#define dbg tl_infer_dbg
void         log_toplevels(tl_infer const *);
void         log_env(tl_infer const *);
void         log_subs(tl_infer *);
void         log_constraint(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
void         log_type_error(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *);
void         log_type_error_mm(tl_infer *, tl_monotype *, tl_monotype *, ast_node const *);

void         tl_infer_set_attributes(tl_infer *, ast_node const *);
void         hot_parse_ctx_reinit(tl_infer *, hashmap *);
tl_monotype *parse_type_arg(tl_infer *, hashmap *, ast_node *);
void         apply_subs_to_ast(tl_infer *);
str          next_variable_name(tl_infer *, str);
str          next_instantiation(tl_infer *, str);
void         cancel_last_instantiation(tl_infer *);
void         do_apply_subs(void *, ast_node *);
void         apply_subs_to_ast_node(tl_infer *, ast_node *);
void         rewrite_operator_overloads_all(tl_infer *);

// ============================================================================
// Internal API: infer_constraint.c (Phases 2-4)
// ============================================================================

int          resolve_node(tl_infer *, ast_node *, traverse_ctx *, node_position);
int          constrain(tl_infer *, tl_polytype *, tl_polytype *, ast_node const *, tl_unify_direction);
int          constrain_mono(tl_infer *, tl_monotype *, tl_monotype *, ast_node const *, tl_unify_direction);
int          constrain_pm(tl_infer *, tl_polytype *, tl_monotype *, ast_node const *, tl_unify_direction);
int          constrain_or_set(tl_infer *, ast_node *, tl_polytype *);
void         ensure_tv(tl_infer *, tl_polytype **);
int          type_error(tl_infer *, ast_node const *);
int          unresolved_type_error(tl_infer *, ast_node const *);
void         expected_type(tl_infer *, ast_node const *);
void         expected_tagged_union(tl_infer *, ast_node const *);
void         wrong_number_of_arguments(tl_infer *, ast_node const *);
void         tagged_union_case_syntax_error(tl_infer *, ast_node const *);
int          expected_symbol(tl_infer *, ast_node const *);
int          traverse_ast(tl_infer *, traverse_ctx *, ast_node *, traverse_cb);
int          traverse_ast_case(tl_infer *, traverse_ctx *, ast_node *, traverse_cb);
int          traverse_ast_node_params(tl_infer *, traverse_ctx *, ast_node *, traverse_cb);
void         traverse_ctx_load_type_arguments(tl_infer *, traverse_ctx *, ast_node const *);
int          traverse_ctx_assign_type_arguments(tl_infer *, traverse_ctx *, ast_node const *);
int          traverse_ctx_is_param(traverse_ctx *, str);
int          env_insert_constrain(tl_infer *, str, tl_polytype *, ast_node const *);
int          process_annotation(tl_infer *, traverse_ctx *, ast_node *, annotation_opts);
int          infer_traverse_cb(tl_infer *, traverse_ctx *, ast_node *);
int          add_generic(tl_infer *, ast_node *);
int          check_missing_free_variables(tl_infer *);
void         sync_with_env(tl_infer *, traverse_ctx *, ast_node *, int);
void         ensure_symbol_type_from_env(tl_infer *, ast_node *);
int          check_is_pointer(tl_infer *, tl_polytype *, ast_node *);
void         load_toplevel(tl_infer *, ast_node_sized);
int          check_type_predicate(tl_infer *, traverse_ctx *, ast_node *);
tl_monotype *tagged_union_find_variant(tl_monotype *, str, int *);
int          is_cast_annotation(ast_node *);

traverse_ctx           *traverse_ctx_create(allocator *);
annotation_parse_result parse_type_annotation(tl_infer *, traverse_ctx *, ast_node *);

// ============================================================================
// Internal API: infer_alpha.c (Phase 1)
// ============================================================================

void rename_variables(tl_infer *, ast_node *, rename_variables_ctx *, int);
void rename_let_in(tl_infer *, ast_node *, rename_variables_ctx *);
void add_free_variables_to_arrow(tl_infer *, ast_node *, tl_polytype *);
void concretize_params(tl_infer *, ast_node *, tl_monotype *, hashmap *, tl_monotype_sized);
int  collect_free_variables_cb(tl_infer *, traverse_ctx *, ast_node *);
int  can_be_free_variable(tl_infer *, traverse_ctx *, ast_node const *);
void promote_free_variables(str_array *, tl_monotype *);

// ============================================================================
// Internal API: infer_specialize.c (Phase 5)
// ============================================================================

str          *instance_lookup_arrow(tl_infer *, str, tl_monotype *, tl_monotype_sized);
str           specialize_type_constructor(tl_infer *, str, tl_monotype_sized, tl_polytype **);
str           specialize_arrow(tl_infer *, traverse_ctx *, str, tl_monotype *, tl_monotype_sized);
int           specialize_applications_cb(tl_infer *, traverse_ctx *, ast_node *);
ast_node     *clone_generic_for_arrow(tl_infer *, ast_node const *, tl_monotype *, str, hashmap *,
                                      tl_monotype_sized);
tl_polytype  *make_arrow(tl_infer *, traverse_ctx *, ast_node_sized, ast_node *, int);
tl_polytype  *make_arrow_result_type(tl_infer *, traverse_ctx *, ast_node_sized, tl_polytype *, int);
tl_polytype  *make_arrow_with(tl_infer *, traverse_ctx *, ast_node *, tl_polytype *);
tl_polytype  *make_binary_predicate_arrow(tl_infer *, traverse_ctx *, ast_node *, ast_node *);
int           post_specialize(tl_infer *, traverse_ctx *, ast_node *, tl_monotype *);
void          specialized_add_to_env(tl_infer *, str, tl_monotype *);
void          remove_generic_toplevels(tl_infer *);
int           check_main_function(tl_infer *, ast_node *);
int           instance_name_exists(tl_infer *, str);
int           is_union_struct(tl_infer *, str);
int           is_type_literal(tl_infer *, traverse_ctx const *, ast_node const *);
int           type_literal_specialize(tl_infer *, ast_node *, hashmap *type_arguments);
void          specialize_type_alias(tl_infer *, ast_node *);
ast_node     *get_infer_target(ast_node *);
void          toplevel_name_replace(ast_node *, str);
name_and_type make_instance_key(tl_infer *, str, tl_monotype *, tl_monotype_sized);
str          *instance_lookup(tl_infer *, name_and_type *);

// ============================================================================
// Internal API: infer_update.c (Phases 6-7)
// ============================================================================

hashmap     *tree_shake(tl_infer *, ast_node const *);
void         tree_shake_toplevels(tl_infer *, ast_node const *);
void         update_specialized_types(tl_infer *);
void         check_unresolved_types(tl_infer *);
void         check_closure_escape(tl_infer *);
void         check_closure_alloc_capture(tl_infer *);
tl_monotype *tl_infer_update_specialized_type_(tl_infer *, tl_monotype *, hashmap **);

// Check whether a lambda node has [[alloc]] in its attributes.
static inline int lambda_has_alloc(tl_infer *self, ast_node *lambda) {
    if (!lambda || lambda->tag != ast_lambda_function) return 0;
    if (!lambda->lambda_function.attributes) return 0;
    lambda_closure_attrs attrs = lambda_get_closure_attrs(self->transient, lambda->lambda_function.attributes);
    return attrs.has_alloc;
}

#if DEBUG_INVARIANTS
void report_invariant_failure(tl_infer *, char const *phase, char const *invariant, char const *detail,
                              ast_node const *);
void check_types_null_cb(void *, ast_node *);
int  check_all_types_null(tl_infer *, ast_node_sized, char const *);
void check_type_arg_types_null_one(struct check_types_null_ctx *, ast_node *);
void check_type_arg_types_null_cb(void *, ast_node *);
int  check_type_arg_types_null(tl_infer *, ast_node_sized, char const *);
int  check_no_generic_toplevels(tl_infer *, char const *);
void check_specialized_nfa_type_args_cb(void *, ast_node *);
int  check_specialized_nfa_type_args(tl_infer *, ast_node *, char const *);
#endif

#endif // TESS_INFER_INTERNAL_H
