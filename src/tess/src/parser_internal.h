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

// Info about a variadic function, stored in parser's variadic_symbols map.
// Key is the base (unmangled, unarity'd) function name.
typedef struct {
    u8  n_fixed_params; // number of non-variadic parameters
    str mangled_name;   // arity-mangled name (e.g. "print__2")
    str trait_name;     // trait bound name (e.g. "ToString")
    str module;         // module where the variadic function is defined
} variadic_symbol_info;

// Info about a function alias created by `name = Module.func` at the top level.
// Key is the alias name (e.g. "print"). Call sites using the alias are rewritten
// at parse time to call the target function directly.
typedef struct {
    str module;    // target module name (e.g. "Print")
    str base_name; // target function's base name (e.g. "print")
} function_alias_info;

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
    hashmap            *modules_version_seen;         // str hset: "prefix::module" for version-aware dedup
    hashmap            *nested_type_parents;          // str hset: types that have nested types
    hashmap            *tagged_union_variant_parents; // str hset: type names that are tagged union parents
    hashmap            *module_aliases;               // map str -> str: alias name -> original module name
    hashmap            *nullary_variant_parents; // map str -> str: mangled variant name -> TU parent name
    hashmap            *module_pkg_prefixes;     // optional: module name -> "pkg__ver" prefix (str->str)
    hashmap            *file_pkg_prefixes;       // optional: file_path -> hashmap* (per-file prefix map)

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

    hashmap    *variadic_symbols; // map str -> variadic_symbol_info: base name -> variadic info
    hashmap    *function_aliases; // map str -> function_alias_info: alias name -> target info
};

// ============================================================================
// Internal API: parser.c
// ============================================================================

typedef int (*parse_fun_s)(parser *, char const *);
typedef int (*parse_fun_int)(parser *, int);

// Top-level definitions

int toplevel_defun(parser *);
int toplevel_enum(parser *self);
int toplevel_function_alias(parser *self);
int toplevel_struct(parser *self);
int toplevel_trait(parser *self);
int toplevel_type_alias(parser *self);
int toplevel_union(parser *self);
int toplevel(parser *);

// Expressions and operators

int       a_binary_operator(parser *self, int min_prec);
int       a_expression(parser *);
int       a_unary_operator(parser *self, int min_prec);
int       a_value(parser *);
int       operator_precedence(char const *op, int is_prefix);
ast_node *parse_expression(parser *, int min_preced);
ast_node *parse_if_expr(parser *);
ast_node *parse_lvalue(parser *);

// Statements and bodies

int       a_assignment(parser *);
int       a_assignment_by_operator(parser *, int);
int       a_body_element(parser *);
int       a_defer_statement(parser *);
int       a_field_assignment(parser *);
int       a_reassignment(parser *);
int       a_statement(parser *);
ast_node *create_body(parser *self, ast_node_array exprs, ast_node_array defers);
ast_node *create_body_fallback(parser *self, ast_node_array exprs, ast_node_array defers, ast_node *);

ast_node *parse_body(parser *);

// Functions and lambdas

int a_funcall(parser *);
int a_lambda_function(parser *);
int a_lambda_funcall(parser *);
int too_many_arguments(parser *);

// Types and type annotations

int a_type_arrow(parser *self);
int a_type_annotation(parser *self);
int a_type_constructor(parser *);
int maybe_type_arguments(parser *self, ast_node_array *type_args);
int a_type_identifier(parser *);
int maybe_trait_bound(parser *);
int maybe_type_parameters(parser *, ast_node_array *out);
int is_reserved_type_name(ast_node const *);

// Type definition helpers
ast_node *create_utd(parser *, ast_node *name, u8 n_type_args, ast_node **type_args, ast_node_array fields,
                     int is_union);
ast_node *create_enum_utd(parser *, ast_node *name, ast_node_array idents);
u8        collect_used_type_params(parser *, u8 n_type_args, ast_node **type_args, ast_node_array fields,
                                   ast_node ***out_used_type_args);
int       parse_param_list(parser *, ast_node_array *, int);

// tagged union helpers

int       toplevel_tagged_union(parser *);
ast_node *build_tagged_union_wrapping(parser *, str tu_name, str var_name, str module,
                                      ast_node *inner_call);
ast_node *maybe_auto_invoke_nullary_variant(parser *, ast_node *symbol, str original_name,
                                            str target_module);
ast_node *maybe_wrap_variant_in_tagged_union(parser *, str parent_name, str child_name, str module,
                                             ast_node *right);

// token parsers (atomic terminals)

int a_identifier(parser *);
int a_colon(parser *);
int a_colon_equal(parser *p);
int a_semicolon(parser *);
int a_vertical_bar(parser *);
int a_open_curly(parser *);
int a_close_curly(parser *);
int a_comma(parser *);
int a_dot(parser *);
int a_param(parser *);
int a_attribute_set(parser *);

int a_ampersand(parser *);
int a_arrow(parser *);
int a_bool(parser *);
int a_close_round(parser *);
int a_close_square(parser *);
int a_double_open_square(parser *);
int a_double_close_square(parser *);
int a_char(parser *);
int a_ellipsis(parser *);
int a_equal_sign(parser *);
int a_identifier_optional_arity(parser *);
int a_attributed_identifier(parser *);
int a_nil(parser *);
int a_null(parser *);
int a_number(parser *);
int a_open_round(parser *);
int a_open_square(parser *);
int a_star(parser *);
int a_string(parser *);
int the_symbol(parser *, char const *const);

// Result helpers

int result_ast(parser *, ast_tag);
int result_ast_bool(parser *, int);
int result_ast_f64(parser *, f64);
int result_ast_i64(parser *, i64);
int result_ast_i64_z(parser *, i64);
int result_ast_str(parser *, ast_tag, char const *s);
int result_ast_u64(parser *, u64);
int result_ast_u64_zu(parser *, u64);
int result_ast_node(parser *, ast_node *);

// Node helpers
void set_node_file(parser *, ast_node *);
int  set_node_parameters(parser *, ast_node *, ast_node_array *);

// Name mangling
void mangle_name(parser *, ast_node *);
void mangle_name_for_module(parser *, ast_node *, str module);
void mangle_name_for_arity(parser *self, ast_node *name, u8 arity, int is_definition);
str  mangle_str_for_module(parser *, str name, str module);
void unmangle_name(parser *, ast_node *);

// Module symbols
void     add_module_symbol(parser *, ast_node *);
hashmap *resolve_module_symbols(parser *, str module_name);
void     maybe_mangle_implicit_submodule(parser *, ast_node *name);

// Variadic detection and symbol registration
int  is_variadic_annotation(ast_node *ann);
str  variadic_trait_name(ast_node *ann);
void register_variadic_symbol(parser *, str base_name, str mangled, u8 n_fixed, str trait, str module);
int  detect_and_register_variadic(parser *, ast_node *name, ast_node_sized params, u8 *out_arity);

// parser infrastructure

int           is_eof(parser *);
int           is_unary_operator(char const *);
int           is_reserved(char const *);
int           is_ampersand(ast_node const *node);

nodiscard int a_try(parser *, parse_fun);
nodiscard int a_try_s(parser *, parse_fun_s, char const *);
nodiscard int a_try_int(parser *p, parse_fun_int fun, int arg);

int           eat_comments(parser *);
int           next_token(parser *);

int           string_to_number(parser *, char const *const);
str           next_var_name(parser *);

void          tokens_push_back(struct parser *, struct token *);
void          tokens_shrink(struct parser *, u32);

// ============================================================================
// Receiver blocks
// ============================================================================

// A single parameter in the receiver block header: "name : TypeExpr"
typedef struct {
    ast_node *name;      // symbol node (from a_identifier)
    ast_node *type_expr; // type expression (from a_type_identifier)
} receiver_param;

defarray(receiver_param_array, receiver_param);

// A single function entry inside the receiver block.
// Forward declaration (body == null) or full definition (body != null).
typedef struct {
    ast_node      *name;       // attributed identifier
    ast_node_array type_params; // function-level [U] if present
    ast_node_array params;      // explicit params only (no receiver)
    ast_node      *return_type; // type after ->, or null
    ast_node      *body;        // parsed body, or null for forward decls
} receiver_entry;

defarray(receiver_entry_array, receiver_entry);

// Complete parsed receiver block (before desugaring).
typedef struct {
    receiver_param_array params;  // 1 or more receiver parameters
    receiver_entry_array entries; // function entries inside { ... }
} receiver_block_info;

int toplevel_receiver_block(parser *);

#ifndef MOS_WINDOWS
void parser_dbg(struct parser *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));
#else
void parser_dbg(struct parser *, char const *restrict fmt, ...);
#endif

#endif // TESS_PARSER_INTERNAL_H
