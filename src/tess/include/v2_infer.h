#ifndef TESS_INFER_H_V2
#define TESS_INFER_H_V2

#include "alloc.h"
#include "ast.h"
#include "v2_type.h"

typedef struct tl_infer tl_infer;

typedef struct {
    tl_type_env *env;
    hashmap     *toplevels; // str => ast_node*
} tl_infer_result;

nodiscard tl_infer *tl_infer_create(allocator *) mallocfun;
void                tl_infer_destroy(allocator *, tl_infer **);
void                tl_infer_set_verbose(tl_infer *, int);

int                 tl_infer_run(tl_infer *, ast_node_sized, tl_infer_result *);
void                tl_infer_report_errors(tl_infer *);

#endif
