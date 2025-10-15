#ifndef TESS_INFER_H_V2
#define TESS_INFER_H_V2

#include "alloc.h"
#include "ast.h"
#include "v2_type.h"

typedef struct tl_infer tl_infer;

typedef struct {
    tl_type_registry *registry;
    tl_type_env      *env;
    hashmap          *toplevels; // str => ast_node*
    ast_node_sized    nodes;     // full ast (to get utds)
} tl_infer_result;

nodiscard tl_infer *tl_infer_create(allocator *) mallocfun;
void                tl_infer_destroy(allocator *, tl_infer **);
void                tl_infer_set_verbose(tl_infer *, int);

int                 tl_infer_run(tl_infer *, ast_node_sized, tl_infer_result *);
void                tl_infer_report_errors(tl_infer *);

str                 toplevel_name(ast_node const *);
ast_node           *toplevel_name_node(ast_node *);

tl_monotype const  *tl_type_constructor_from_user_type(tl_infer *, ast_node const *node);

#endif
