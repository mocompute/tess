#ifndef TESS_SYNTAX_H
#define TESS_SYNTAX_H

#include "ast.h"

#include "alloc.h"

typedef struct syntax_checker syntax_checker;

// -- allocation and deallocation --

nodiscard syntax_checker *syntax_checker_create(allocator *) mallocfun;
void                      syntax_checker_destroy(syntax_checker **);

// -- operation --

nodiscard int syntax_checker_run(syntax_checker *, ast_node **, size_t);

#endif
