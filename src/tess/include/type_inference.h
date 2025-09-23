#ifndef TESS_TYPE_INFERENCE_H
#define TESS_TYPE_INFERENCE_H

#include "alloc.h"
#include "ast.h"
#include "hashmap.h"
#include "type_registry.h"

typedef struct ti_inferer ti_inferer;

typedef struct {
    char const *name;
    tl_type    *type;
    ast_node   *node;   // let, symbol, lambda_function
    ast_node   *source; // the node from which we derived this specialisation requirement
} ti_function_record;

// -- allocation and deallocation --

nodiscard ti_inferer *ti_inferer_create(allocator *, ast_node_array *, type_registry *);
void                  ti_inferer_destroy(allocator *, ti_inferer **);

// -- operation --

nodiscard int  ti_inferer_run(ti_inferer *);
void           ti_inferer_report_errors(ti_inferer *);
ast_node_sized ti_inferer_get_program(ti_inferer *); // owned by ti

void           ti_inferer_set_verbose(ti_inferer *, int);
void           ti_inferer_dbg_constraints(ti_inferer const *);
void           ti_inferer_dbg_substitutions(ti_inferer const *);

typedef void (*ti_traverse_lexical_fun)(void *, ast_node *, hashmap **);
void                   ti_traverse_lexical(allocator *, void *, ast_node *, ti_traverse_lexical_fun);

tl_free_variable_sized ti_free_variables_in(allocator *, ast_node const *);

ti_function_record    *ti_lookup_function(ti_inferer *, char const *);

int                    ti_is_generated_variable_name(char const *);

void                   ti_trace_symbol_add(ti_inferer *, char const *);
void                   ti_trace_symbol_remove(ti_inferer *, char const *);

#endif
