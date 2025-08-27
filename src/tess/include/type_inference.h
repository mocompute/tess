#ifndef TESS_TYPE_INFERENCE_H
#define TESS_TYPE_INFERENCE_H

#include "alloc.h"
#include "ast.h"
#include "types.h"

typedef struct ti_inferer ti_inferer;

// -- allocation and deallocation --

nodiscard ti_inferer *ti_inferer_create(allocator *, ast_node **, u32);
void                  ti_inferer_destroy(allocator *, ti_inferer **);

// -- operation --

void ti_inferer_run(ti_inferer *);

void ti_inferer_dbg_constraints(ti_inferer const *);

#endif
