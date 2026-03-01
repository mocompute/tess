#ifndef TESS_INFER_H_V2
#define TESS_INFER_H_V2

#include "alloc.h"
#include "ast.h"
#include "str.h"
#include "type.h"
#include "type_registry.h"

typedef struct tl_infer tl_infer;

typedef struct {
    tl_infer         *infer;
    tl_type_registry *registry;
    tl_type_env      *env;
    tl_type_subs     *subs;
    hashmap          *toplevels;         // str => ast_node*
    ast_node_sized    nodes;             // full ast (to get utds)
    ast_node_sized    synthesized_nodes; // nodes added by compiler
    str_sized         hash_includes;
} tl_infer_result;

typedef struct {
    int is_library; // build a library, not an executable
} tl_infer_opts;

nodiscard tl_infer *tl_infer_create(allocator *, tl_infer_opts const *) mallocfun;
void                tl_infer_destroy(allocator *, tl_infer **);
void                tl_infer_set_verbose(tl_infer *, int);
void                tl_infer_set_verbose_ast(tl_infer *, int);
tl_type_registry   *tl_infer_get_registry(tl_infer *);

int                 tl_infer_run(tl_infer *, ast_node_sized, tl_infer_result *);
void                tl_infer_report_errors(tl_infer *);

str                 toplevel_name(ast_node const *);
ast_node           *toplevel_name_node(ast_node *);

tl_monotype        *tl_infer_update_specialized_type(tl_infer *, tl_monotype *mono);

int                 is_c_symbol(str);
int                 is_c_struct_symbol(str);
int                 is_intrinsic(str);
int                 is_module_init(str);
int                 is_main_function(str);

// -- stats --

void tl_infer_get_arena_stats(tl_infer *, arena_stats *out);

typedef struct {
    double alpha_ms;
    double load_toplevels_ms;
    double generic_inference_ms;
    double free_vars_ms;
    double specialize_ms;
    double tree_shake_ms;
    double update_types_ms;
} tl_infer_phase_stats;

typedef struct {
    u32    traverse_infer_calls;
    u32    traverse_specialize_calls;
    u32    traverse_update_types_calls;
    u64    traverse_nodes_visited;

    u32    specialize_created;
    u32    specialize_cache_hits;
    u32    specialize_already;

    u32    subs_apply_calls;
    u64    subs_nodes_visited;

    u32    unify_calls;
    double unify_ms; // accumulated time in constrain()

    // #1: Generic inference vs tree-shake survival
    u32 toplevels_inferred;         // toplevels entering Phase 3
    u32 toplevels_after_specialize; // toplevels after Phase 5 (remove_generic)
    u32 toplevels_after_tree_shake; // toplevels surviving Phase 6

    // #2: Specialization inner loop breakdown (accumulated ms)
    double specialize_clone_ms;   // clone_generic_for_arrow
    double specialize_infer_ms;   // re-inference in post_specialize
    double specialize_subs_ms;    // apply_subs_to_ast_node in post_specialize
    double specialize_recurse_ms; // recursive specialize_applications_cb in post_specialize

    // #3: Type Updates breakdown
    double update_types_env_ms;            // env iteration pass
    double update_types_ast_ms;            // AST traverse pass
    u32    update_types_env_count;         // env entries processed
    u32    update_types_type_cons_calls;   // specialize_type_constructor_ calls during Phase 7
    u32    update_types_type_cons_skipped; // skipped (already specialized, no args changed)
} tl_infer_counters;

void                        tl_infer_set_report_stats(tl_infer *, int);
tl_infer_phase_stats const *tl_infer_get_phase_stats(tl_infer const *);
tl_infer_counters const    *tl_infer_get_counters(tl_infer const *);

#endif
