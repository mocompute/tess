#ifndef TESS_SYNTAX_H
#define TESS_SYNTAX_H

#include "ast.h"

#include "alloc.h"

int syntax_rename_variables(allocator *, ast_pool *, ast_node_h *, size_t);

#endif
