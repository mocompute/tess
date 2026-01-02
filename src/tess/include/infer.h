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

#endif
