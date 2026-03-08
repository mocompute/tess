// parser_internal.h — Shared internal header for parser_*.c compilation units.
// NOT a public header.  Only included by src/tess/src/parser*.c files.

#ifndef TESS_PARSER_INTERNAL_H
#define TESS_PARSER_INTERNAL_H

#include "parser.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "hashmap.h"
#include "str.h"
#include "token.h"
#include "tokenizer.h"
#include "types.h"

#include <assert.h>

// ============================================================================
// Internal constants
// ============================================================================

#define ERROR_STOP 2 // signal to stop parsing rather than backtrack

// ============================================================================
// Internal types
// ============================================================================

typedef int (*parse_fun)(parser *);

typedef enum {
    mode_none,            // initialisation
    mode_symbols,         // first pass, formerly is_symbol_pass
    mode_source,          // second pass, after symbols
    mode_toplevel_funcall // for parsing package.tl
} parser_mode;

struct parser {
    allocator          *parent_alloc;
    allocator          *file_arena;
    allocator          *tokens_arena; // for tokens only
    allocator          *ast_arena;    // for ast nodes and related
    allocator          *transient;    // reset after each call to parser_next
    allocator          *speculative;  // for speculative AST allocations in toplevel()

    parser_opts         opts;

    tokenizer          *tokenizer;

    str_sized           files;
    u32                 files_index;
    char_csized         current_file_data;
    hashmap            *modules_seen;                 // str hset
    hashmap            *module_preludes_seen;         // str hset: modules declared with #module_prelude
    hashmap            *nested_type_parents;          // str hset: types that have nested types
    hashmap            *tagged_union_variant_parents; // str hset: type names that are tagged union parents
    hashmap            *module_aliases;               // map str -> str: alias name -> original module name
    hashmap            *nullary_variant_parents; // map str -> str: mangled variant name -> TU parent name

    ast_node           *result;
    token_array         tokens;

    struct parser_error error;
    struct tokenizer_error tokenizer_error;
    struct token           token;

    str                    current_module;
    hashmap               *current_module_symbols; // hset str
    hashmap               *builtin_module_symbols; // hset str
    hashmap               *module_symbols;         // map str (mod name) -> hashmap* of hset str

    u32                    next_var_name;
    int                    verbose;
    int                    indent_level;
    int                    in_function_application; // enable greedy parsing
    int                    skip_module;             // skip parsing until next module or file
    int                    prelude_consumed;        // prelude string has been parsed
    int         expect_module; // expect a module immediately after a #unity_file before any terms
    parser_mode mode;
};

// ============================================================================
// Internal API: parser.c (shared utilities used by parser_tagged_union.c)
// ============================================================================

// Result helpers
int result_ast_node(parser *, ast_node *);

// Token parsers
nodiscard int a_try(parser *, parse_fun);
int           a_identifier(parser *);
int           a_colon(parser *);
int           a_vertical_bar(parser *);
int           a_open_curly(parser *);
int           a_close_curly(parser *);
int           a_comma(parser *);
int           a_dot(parser *);
int           a_param(parser *);
int           a_type_identifier(parser *);

// Node helpers
void set_node_file(parser *, ast_node *);
int  set_node_parameters(parser *, ast_node *, ast_node_array *);

// Name mangling
void mangle_name(parser *, ast_node *);
void mangle_name_for_module(parser *, ast_node *, str module);
void unmangle_name(parser *, ast_node *);
str  mangle_str_for_module(parser *, str name, str module);

// Module symbols
void add_module_symbol(parser *, ast_node *);

// Type definition helpers
int       is_reserved_type_name(ast_node const *);
ast_node *create_utd(parser *, ast_node *name, u8 n_type_args, ast_node **type_args, ast_node_array fields,
                     int is_union);
ast_node *create_enum_utd(parser *, ast_node *name, ast_node_array idents);
u8        collect_used_type_params(parser *, u8 n_type_args, ast_node **type_args, ast_node_array fields,
                                   ast_node ***out_used_type_args);

// ============================================================================
// Internal API: parser_tagged_union.c
// ============================================================================

int       toplevel_tagged_union(parser *);
ast_node *build_tagged_union_wrapping(parser *, str tu_name, str var_name, str module,
                                      ast_node *inner_call);

#endif // TESS_PARSER_INTERNAL_H
