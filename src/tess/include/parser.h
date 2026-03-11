#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "nodiscard.h"
#include "str.h"
#include "tokenizer.h"

#include "alloc.h"
#include "type.h"
#include "type_registry.h"

typedef struct parser parser;

typedef struct parser_error {
    tokenizer_error *tokenizer;
    token           *token;
    tl_error_tag     tag;
    char const      *file;
    u32              line;
    u32              col;
} parser_error;

typedef struct {
    tl_type_registry *registry;
    str_sized         files;
    char const       *prelude;        // optional: TL source string parsed before files
    str_array         defines;        // -D symbols for conditional compilation
    hashmap          *known_modules;  // optional: pre-scanned module names (str map) for nested module validation
    hashmap          *module_pkg_prefixes; // optional: module name → "pkg__ver" prefix (str→str map)
    hashmap          *file_pkg_prefixes;  // optional: file_path → hashmap* (module→prefix per file)
    char const       *preloaded_path; // optional: path (e.g. "<stdin>") for pre-loaded file data
    char const       *preloaded_data; // optional: pre-loaded file contents (used instead of file_read)
    u32               preloaded_size; // size of preloaded_data
} parser_opts;

// -- allocation and deallocation --

nodiscard parser *parser_create(allocator *, parser_opts const *) mallocfun;
void              parser_destroy(parser **);

// -- module symbols pass --

hashmap *parser_take_module_symbols(parser *);
void     parser_set_module_symbols(parser *, hashmap *);

// -- parser --

int  parser_next(parser *);
void parser_result(parser *, ast_node **);

int  parser_parse_all(parser *, ast_node_array *out);
int  parser_parse_all_symbols(parser *);
int  parser_parse_all_verbose(parser *, ast_node_array *out);
int  parser_parse_all_toplevel_funcalls(parser *, ast_node_array *out);
void parser_report_errors(parser *);
void parser_set_verbose(parser *, int);

// -- utilities --

int is_arithmetic_operator(char const *);
int is_relational_operator(char const *);
int is_logical_operator(char const *);
int is_index_operator(char const *);
int is_struct_access_operator(char const *);
int is_dot_operator(char const *);
int is_bitwise_operator(char const *);

str mangle_str_for_arity(allocator *, str, u8);

// -- stats --

void parser_get_arena_stats(parser *, arena_stats *ast, arena_stats *tokens);

#endif
