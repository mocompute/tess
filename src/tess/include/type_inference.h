#ifndef TESS_TYPE_INFERENCE_H
#define TESS_TYPE_INFERENCE_H

#include "alloc.h"
#include "ast.h"
#include "types.h"

typedef struct ti_inferer ti_inferer;

// -- allocation and deallocation --

nodiscard ti_inferer *ti_inferer_create(allocator *, ast_node_array *);
void                  ti_inferer_destroy(allocator *, ti_inferer **);

// -- operation --

nodiscard int ti_inferer_run(ti_inferer *);
void          ti_inferer_report_errors(ti_inferer *);

void          ti_inferer_set_verbose(ti_inferer *, bool);
void          ti_inferer_dbg_constraints(ti_inferer const *);
void          ti_inferer_dbg_substitutions(ti_inferer const *);

#endif
