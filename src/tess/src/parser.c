#include "parser.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "file.h"
#include "hashmap.h"
#include "infer.h"
#include "platform.h"
#include "str.h"
#include "token.h"
#include "tokenizer.h"
#include "types.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSER_ARENA_SIZE 1024

// All non-zero returns from a_try parsing functions signal a backtrack, except for this:
#define ERROR_STOP        2 // signal to stop parsing rather than backtrack

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
    hashmap            *nullary_variant_parents;       // map str -> str: mangled variant name -> TU parent name

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

typedef int (*parse_fun)(parser *);
typedef int (*parse_fun_s)(parser *, char const *);
typedef int (*parse_fun_int)(parser *, int);

static int annotation_uses_type_param(ast_node *node, str param_name);

// -- overview --

static int           toplevel(parser *);

static int           a_param(parser *);
static int           a_assignment(parser *);
static int           a_defer_statement(parser *);
static int           a_field_assignment(parser *);
static int           a_body_element(parser *);
static int           a_expression(parser *);
static int           a_funcall(parser *);
static int           a_type_constructor(parser *);
static int           a_lambda_function(parser *);
static int           a_reassignment(parser *);
static int           a_statement(parser *);
static int           a_value(parser *);
static ast_node     *create_body(parser *self, ast_node_array exprs, ast_node_array defers);
static ast_node     *create_body_fallback(parser *self, ast_node_array exprs, ast_node_array defers,
                                          ast_node *);
static int           maybe_type_arguments(parser *self, ast_node_array *type_args);
static int           operator_precedence(char const *op, int is_prefix);
static ast_node     *parse_base_expression(parser *);
static ast_node     *parse_expression(parser *, int min_preced);
static ast_node     *parse_if_expr(parser *);
static ast_node     *parse_lvalue(parser *);
static int           toplevel_defun(parser *);

static ast_node     *build_tagged_union_wrapping(parser *, str tu_name, str var_name, str module,
                                                 ast_node *inner_call);
static int           result_ast(parser *, ast_tag);
static int           result_ast_bool(parser *, int);
static int           result_ast_f64(parser *, f64);
static int           result_ast_i64(parser *, i64);
static int           result_ast_i64_z(parser *, i64);
static int           result_ast_node(parser *, ast_node *);
static int           result_ast_str(parser *, ast_tag, char const *s);
static int           result_ast_u64(parser *, u64);
static int           result_ast_u64_zu(parser *, u64);

static int           is_eof(parser *);
static int           is_unary_operator(char const *);
static int           is_reserved(char const *);

nodiscard static int a_try(parser *, parse_fun);
nodiscard static int a_try_s(parser *, parse_fun_s, char const *);

static int           eat_comments(parser *);
static int           next_token(parser *);

static int           a_ampersand(parser *);
static int           a_arrow(parser *);
static int           a_bool(parser *);
static int           a_close_round(parser *);
static int           a_close_square(parser *);
static int           a_colon(parser *);
static int           a_double_open_square(parser *);
static int           a_double_close_square(parser *);
static int           a_vertical_bar(parser *);
static int           a_char(parser *);
static int           a_comma(parser *);
static int           a_ellipsis(parser *);
static int           a_equal_sign(parser *);
static int           a_identifier(parser *);
static int           a_identifier_optional_arity(parser *);
static int           a_attributed_identifier(parser *);
static int           a_nil(parser *);
static int           a_null(parser *);
static int           a_number(parser *);
static int           a_open_round(parser *);
static int           a_open_square(parser *);
static int           a_star(parser *);
static int           a_string(parser *);
static int           the_symbol(parser *, char const *const);

static int           string_to_number(parser *, char const *const);
static void          mangle_name_for_module(parser *, ast_node *, str);
static void          mangle_name(parser *, ast_node *);
static void          mangle_name_for_arity(parser *self, ast_node *name, u8 arity, int is_definition);
static void          unmangle_name(parser *, ast_node *);
static str           next_var_name(parser *);

static void          tokens_push_back(struct parser *, struct token *);
static void          tokens_shrink(struct parser *, u32);
static int           too_many_arguments(parser *);

#ifndef MOS_WINDOWS
static void dbg(struct parser *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));
#else
static void dbg(struct parser *, char const *restrict fmt, ...);
#endif

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, parser_opts const *opts) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));
    alloc_zero(self);
    self->opts                         = *opts;
    self->parent_alloc                 = alloc;
    self->file_arena                   = arena_create(alloc, 64 * 1024);
    self->tokens_arena                 = arena_create(alloc, PARSER_ARENA_SIZE);
    self->ast_arena                    = arena_create(alloc, PARSER_ARENA_SIZE);
    self->transient                    = arena_create(alloc, PARSER_ARENA_SIZE);
    self->speculative                  = arena_create(alloc, PARSER_ARENA_SIZE);
    self->tokenizer                    = null;
    self->files                        = opts->files;
    self->files_index                  = 0;
    self->current_file_data.v          = null;
    self->current_file_data.size       = 0;
    self->modules_seen                 = hset_create(self->parent_alloc, 32);
    self->module_preludes_seen         = hset_create(self->parent_alloc, 32);
    self->nested_type_parents          = hset_create(self->parent_alloc, 1024);
    self->tagged_union_variant_parents = hset_create(self->parent_alloc, 256);
    self->module_aliases               = map_new(self->parent_alloc, str, str, 32);
    self->nullary_variant_parents       = map_new(self->parent_alloc, str, str, 16);
    self->result                       = null;
    self->tokens                       = (token_array){0};
    self->error                        = (struct parser_error){0};
    self->tokenizer_error              = (struct tokenizer_error){0};
    self->token                        = (struct token){0};
    self->current_module               = str_empty();
    self->current_module_symbols       = hset_create(self->parent_alloc, 512);
    self->builtin_module_symbols       = hset_create(self->parent_alloc, 512);
    self->module_symbols               = map_create_ptr(self->parent_alloc, 64); // str -> hashmap*
    self->next_var_name                = 0;
    self->verbose                      = 0;
    self->indent_level                 = 0;
    self->in_function_application      = 0;
    self->skip_module                  = 0;
    self->prelude_consumed             = 0;
    self->expect_module                = 0;
    self->mode                         = mode_none;

    self->tokenizer                    = null;
    self->tokens                       = (token_array){.alloc = self->tokens_arena};

    token_init(&self->token, tok_invalid);
    self->error.token     = &self->token;
    self->error.tokenizer = &self->tokenizer_error;

    return self;
}

void parser_destroy(parser **self) {
    // error token: arena
    // tokens: arena

    // tokenizer
    if ((*self)->tokenizer) tokenizer_destroy(&(*self)->tokenizer);

    // arena
    allocator *alloc = (*self)->parent_alloc;
    if ((*self)->module_symbols) hset_destroy(&(*self)->module_symbols);
    hset_destroy(&(*self)->builtin_module_symbols);
    hset_destroy(&(*self)->current_module_symbols);
    hset_destroy(&(*self)->nested_type_parents);
    hset_destroy(&(*self)->tagged_union_variant_parents);
    map_destroy(&(*self)->nullary_variant_parents);
    hset_destroy(&(*self)->module_preludes_seen);
    hset_destroy(&(*self)->modules_seen);
    arena_destroy(&(*self)->transient);
    arena_destroy(&(*self)->speculative);
    arena_destroy(&(*self)->ast_arena);
    arena_destroy(&(*self)->tokens_arena);
    arena_destroy(&(*self)->file_arena);
    alloc_free(alloc, *self);
    *self = null;
}

hashmap *parser_take_module_symbols(parser *self) {
    hashmap *out         = self->module_symbols;
    self->module_symbols = null;
    return out;
}

void parser_set_module_symbols(parser *self, hashmap *mod_syms) {
    map_destroy(&self->module_symbols);
    self->module_symbols = mod_syms;
}

void parser_get_arena_stats(parser *self, arena_stats *ast, arena_stats *tokens) {
    arena_get_stats(self->ast_arena, ast);
    arena_get_stats(self->tokens_arena, tokens);
}

// -- module --

static void add_module_symbol(parser *self, ast_node *name) {
    // Keeps names before they are mangled with the module name prefix, but after they are mangled with
    // arity.
    if (ast_node_is_symbol(name)) {
        // For safety, don't add symbols which have already been mangled.
        if (name->symbol.is_mangled) return;

        str name_str = ast_node_str(name);

        // don't add names which are builtin names
        if (str_hset_contains(self->builtin_module_symbols, name_str)) return;

        str_hset_insert(&self->current_module_symbols, name_str);

        // Also copy builtin names to another hset
        if (str_eq(self->current_module, S("builtin")))
            str_hset_insert(&self->builtin_module_symbols, name_str);

    } else if (ast_node_is_nfa(name)) {
        add_module_symbol(self, name->named_application.name);
    }
}

// -- parser --

static void set_node_file(parser *self, ast_node *node) {
    node->file = self->token.file;
    node->line = self->token.line;
    node->col  = self->token.col;
}

static void set_result_file(parser *self) {
    set_node_file(self, self->result);
}

static int result_ast(parser *p, ast_tag tag) {
    p->result = ast_node_create(p->ast_arena, tag);
    set_result_file(p);
    return 0;
}

static int result_ast_i64(parser *p, i64 val) {
    p->result = ast_node_create_i64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

static int result_ast_u64(parser *p, u64 val) {
    p->result = ast_node_create_u64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

static int result_ast_i64_z(parser *p, i64 val) {
    p->result = ast_node_create_i64_z(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

static int result_ast_u64_zu(parser *p, u64 val) {
    p->result = ast_node_create_u64_zu(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

static int result_ast_f64(parser *p, f64 val) {
    p->result = ast_node_create_f64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

static int result_ast_bool(parser *p, int val) {
    p->result = ast_node_create_bool(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

static int result_ast_str(parser *p, ast_tag tag, char const *s) {
    p->result      = ast_node_create_sym_c(p->ast_arena, s);
    p->result->tag = tag;
    set_result_file(p);
    return 0;
}

static int result_ast_str_(parser *p, ast_tag tag, str s) {
    p->result      = ast_node_create_sym(p->ast_arena, s);
    p->result->tag = tag;
    set_result_file(p);
    return 0;
}

static int result_ast_node(parser *p, ast_node *node) {
    p->result = node;
    set_result_file(p);
    return 0;
}

static int is_reserved(char const *s) {
    static char const *strings[] = {
      "break",  "case", "continue", "defer", "else", "false", "if",    "in", "null",
      "return", "then", "true",     "try",   "void", "when",  "while", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

static int is_reserved_type_keyword(char const *s) {
    static char const *strings[] = {
      "any",
      "void",
      null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

static int is_reserved_type_name(ast_node const *name) {
    // Check for reserved type keywords to disallow
    if (!ast_node_is_symbol(name)) return 0;
    str word = ast_node_str(name);
    if (is_reserved_type_keyword(str_cstr(&word))) return 1;
    return 0;
}

int is_arithmetic_operator(char const *s) {
    static char const *strings[] = {
      "+", "-", "*", "/", "%", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_assignment_by_operator(char const *s) {
    static char const *strings[] = {
      "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_relational_operator(char const *s) {
    static char const *strings[] = {
      "<", "<=", "==", "!=", ">=", ">", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_logical_operator(char const *s) {
    static char const *strings[] = {"&&", "||", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_bitwise_operator(char const *s) {
    static char const *strings[] = {"&", "|", "^", "<<", ">>", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_index_operator(char const *s) {
    return 0 == strcmp(s, ".[");
}

int is_dot_operator(char const *s) {
    return 0 == strcmp(s, ".");
}

int is_struct_access_operator(char const *s) {
    static char const *strings[] = {".", "->", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

static int is_unary_operator(char const *s) {
    static char const *strings[] = {"!", "~", "+", "-", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

static int is_eof(parser *p) {
    return p->error.tag == tl_err_eof || p->tokenizer_error.tag == tl_err_eof;
}

static int eat_comments(parser *p) {
    while (1) {
        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            dbg(p, "tokenizer error: %s", tl_error_tag_to_string(p->tokenizer_error.tag));
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.col  = p->tokenizer_error.col;
            p->error.tag  = tl_err_tokenizer_error;
            return 1;
        }

        token_tag const tag = p->token.tag;
        if (tok_comment == tag) {
            continue;
        } else {
            tokenizer_put_back(p->tokenizer, &p->token, 1);
            return 0;
        }
    }
}

static int next_token(parser *p) {
    if (eat_comments(p)) return 1;

    while (1) {

        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.col  = p->tokenizer_error.col;
            p->error.tag  = tl_err_tokenizer_error;
            return 1;
        }

        // always update file/line
        p->error.file = p->token.file;
        p->error.line = p->token.line;
        p->error.col  = p->token.col;

        if (tok_comment == p->token.tag) continue;

        tokens_push_back(p, &p->token);
        return 0;
    }
}

nodiscard static int a_try(parser *p, parse_fun fun) {
    int       result    = 0;
    u32 const save_toks = p->tokens.size;

    if ((result = fun(p))) {
        assert(p->tokens.size >= save_toks);
        if (p->tokens.size > save_toks) {
            if (0) {
                char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
                // log(p, "a_try: put back %i tokens starting with %s", p->tokens.size - save_toks, str);
                alloc_free(p->transient, str);
            }
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }
        goto cleanup;
    }

cleanup:
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return result;
}

nodiscard static int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
    int       result    = 0;
    u32 const save_toks = p->tokens.size;
    if ((result = fun(p, arg))) {
        if (p->tokens.size > save_toks) {
            if (0) {
                char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
                alloc_free(p->transient, str);
            }
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }

        return result;
    }
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return 0;
}

nodiscard static int a_try_int(parser *p, parse_fun_int fun, int arg) {
    int       result    = 0;
    u32 const save_toks = p->tokens.size;
    if ((result = fun(p, arg))) {
        if (p->tokens.size > save_toks) {
            if (0) {
                char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
                alloc_free(p->transient, str);
            }
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }

        return result;
    }
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return 0;
}

static int a_char(parser *p) {
    if (next_token(p)) return 1;
    if (tok_char == p->token.tag) return result_ast_str(p, ast_char, p->token.s);
    p->error.tag = tl_err_expected_comma;
    return 1;
}

static int a_comma(parser *p) {
    if (next_token(p)) return 1;
    if (tok_comma == p->token.tag) return result_ast_str(p, ast_symbol, ",");
    p->error.tag = tl_err_expected_comma;
    return 1;
}

static int a_dot(parser *p) {
    if (next_token(p)) return 1;
    if (tok_dot == p->token.tag) return result_ast_str(p, ast_symbol, ".");
    p->error.tag = tl_err_expected_dot;
    return 1;
}

static int a_ellipsis(parser *p) {
    if (next_token(p)) return 1;
    if (tok_ellipsis == p->token.tag) return result_ast_str(p, ast_symbol, "...");
    p->error.tag = tl_err_expected_ellipsis;
    return 1;
}

static int a_vertical_bar(parser *p) {
    if (next_token(p)) return 1;
    if (tok_bar == p->token.tag) return result_ast_str(p, ast_symbol, "|");
    p->error.tag = tl_err_expected_vertical_bar;
    return 1;
}

static int a_hash_command(parser *p) {
    if (next_token(p)) return 1;
    if (tok_hash_command == p->token.tag) {
        int res                            = result_ast_str(p, ast_hash_command, p->token.s);
        p->result->hash_command.words      = (str_sized){0};
        p->result->hash_command.is_c_block = 0;
        return res;
    }
    p->error.tag = tl_err_expected_hash_command;
    return 1;
}

static int a_c_block(parser *p) {
    if (next_token(p)) return 1;
    if (tok_c_block == p->token.tag) {
        int res                            = result_ast_str(p, ast_hash_command, p->token.s);
        p->result->hash_command.words      = (str_sized){0};
        p->result->hash_command.is_c_block = 1;
        return res;
    }
    p->error.tag = tl_err_expected_hash_command;
    return 1;
}

static int a_open_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_round == p->token.tag) return result_ast_str(p, ast_symbol, "(");
    p->error.tag = tl_err_expected_open_round;
    return 1;
}

static int a_open_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_square == p->token.tag) return result_ast_str(p, ast_symbol, "[");
    p->error.tag = tl_err_expected_open_square;
    return 1;
}

static int a_close_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_round == p->token.tag) return result_ast_str(p, ast_symbol, ")");
    p->error.tag = tl_err_expected_close_round;
    return 1;
}

static int a_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]");
    p->error.tag = tl_err_expected_close_square;
    return 1;
}

static int a_open_curly(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_curly == p->token.tag) return result_ast_str(p, ast_symbol, "{");
    p->error.tag = tl_err_expected_open_curly;
    return 1;
}

static int a_close_curly(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_curly == p->token.tag) return result_ast_str(p, ast_symbol, "}");
    p->error.tag = tl_err_expected_close_curly;
    return 1;
}

static int a_assignment_by_operator(parser *self, int min_prec) {
    if (next_token(self)) return 1;

    char const *op = null;
    switch (self->token.tag) {
    case tok_symbol:
        if (is_assignment_by_operator(self->token.s)) {
            op = self->token.s;
        } else return 1;
        break;

    case tok_bang_equal:
    case tok_star:
    case tok_dot:
    case tok_equal_equal:
    case tok_logical_and:
    case tok_logical_or:
    case tok_arrow:
    case tok_ampersand:
    case tok_plus:
    case tok_minus:
    case tok_bar:
    case tok_bang:
    case tok_comma:
    case tok_c_block:
    case tok_colon:
    case tok_colon_equal:
    case tok_double_colon:
    case tok_semicolon:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_open_square:
    case tok_close_square:
    case tok_double_open_square:
    case tok_double_close_square:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_c_string:
    case tok_char:
    case tok_comment:
    case tok_hash_command:        return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 0);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

static int a_binary_operator(parser *self, int min_prec) {
    if (next_token(self)) return 1;

    char const *op = null;
    switch (self->token.tag) {
    case tok_symbol:
        if (is_arithmetic_operator(self->token.s) || is_logical_operator(self->token.s) ||
            is_relational_operator(self->token.s) || is_bitwise_operator(self->token.s)) {
            op = self->token.s;
        } else return 1;
        break;

    case tok_bang_equal:          op = "!="; break;
    case tok_star:                op = "*"; break;
    case tok_dot:                 op = "."; break;
    case tok_equal_equal:         op = "=="; break;
    case tok_logical_and:         op = "&&"; break;
    case tok_logical_or:          op = "||"; break;
    case tok_arrow:               op = "->"; break;
    case tok_ampersand:           op = "&"; break;
    case tok_open_square:         op = "["; break;
    case tok_plus:                op = "+"; break;
    case tok_minus:               op = "-"; break;
    case tok_bar:                 op = "|"; break;
    case tok_double_colon:        op = "::"; break;

    case tok_bang:
    case tok_comma:
    case tok_c_block:
    case tok_colon:
    case tok_colon_equal:
    case tok_semicolon:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_close_square:
    case tok_double_open_square:
    case tok_double_close_square:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_c_string:
    case tok_char:
    case tok_comment:
    case tok_hash_command:        return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 0);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

static int a_unary_operator(parser *self, int min_prec) {
    if (next_token(self)) return 1;

    char const *op = null;
    switch (self->token.tag) {

    case tok_bang:  op = "!"; break;
    case tok_plus:  op = "+"; break;
    case tok_minus: op = "-"; break;

    case tok_symbol:
        if (is_unary_operator(self->token.s)) op = self->token.s;
        else return 1;
        break;

    case tok_star:
    case tok_ampersand:
    case tok_bang_equal:
    case tok_bar:
    case tok_comma:
    case tok_dot:
    case tok_c_block:
    case tok_colon:
    case tok_colon_equal:
    case tok_double_colon:
    case tok_semicolon:
    case tok_logical_and:
    case tok_logical_or:
    case tok_arrow:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_open_square:
    case tok_close_square:
    case tok_double_open_square:
    case tok_double_close_square:
    case tok_equal_sign:
    case tok_equal_equal:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_c_string:
    case tok_char:
    case tok_comment:
    case tok_hash_command:        return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 1);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

static int unmangle_arity(str name);
static str unmangle_arity_qualified_name(allocator *alloc, str name);

static int a_attribute_set(parser *self) {
    if (a_try(self, a_double_open_square)) return 1;

    ast_node_array items = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_double_close_square)) goto done;

    if (0 == a_try(self, a_funcall) || 0 == a_try(self, a_identifier)) {
        array_push(items, self->result);
    }

    while (1) {
        if (0 == a_try(self, a_double_close_square)) break;
        if (a_try(self, a_comma)) return 1;
        if (0 == a_try(self, a_identifier) || 0 == a_try(self, a_funcall)) {
            array_push(items, self->result);
        }
    }

done:;

    ast_node *out = ast_node_create_attribute_set(self->ast_arena, (ast_node_sized)sized_all(items));
    return result_ast_node(self, out);
}

static int identifier_base(parser *p, str *name) {
    if (next_token(p)) return 1;

    if (tok_symbol != p->token.tag || 0 == strlen(p->token.s) || is_reserved(p->token.s)) goto error;

    char const c = p->token.s[0];
    if (('_' == c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {

        // first character good, check remaining characters
        char const *pc = &p->token.s[1];
        while (*pc) {
            if ('_' == *pc || ('a' <= *pc && *pc <= 'z') || ('A' <= *pc && *pc <= 'Z') ||
                ('0' <= *pc && *pc <= '9') || '/' == *pc) {
                pc++; // still good
            } else {
                goto error;
            }
        }

        // reject identifiers containing __ to avoid collisions with mangled names
        // exceptions: __init (compiler-recognized) and c__* (C interop prefix)
        if (str_contains(str_init_static(p->token.s), S("__")) && 0 != strcmp(p->token.s, "__init") &&
            0 != strncmp(p->token.s, "c__", 3)) {
            p->error.tag = tl_err_double_underscore_in_identifier;
            return ERROR_STOP;
        }

        // check for reserved words, which are not allowed as identifiers
        if (is_reserved(p->token.s)) goto error;

        *name = str_init(p->ast_arena, p->token.s);
        goto success;
    }

error:
    p->error.tag = tl_err_expected_identifier;
    return 1;

success:
    return 0;
}

static int a_identifier(parser *p) {
    int res = 0;
    str name;
    if ((res = identifier_base(p, &name))) return res;

    result_ast_str_(p, ast_symbol, name);
    return 0;
}

static int a_identifier_optional_arity(parser *p) {
    int res = 0;
    str name;
    if ((res = identifier_base(p, &name))) return res;

    // check for arity-qualified name
    int arity = unmangle_arity(name);
    if (arity != -1) {
        str base = unmangle_arity_qualified_name(p->ast_arena, name);
        assert(!str_is_empty(base));
        str mangled = mangle_str_for_arity(p->ast_arena, base, (u8)arity);
        return result_ast_str_(p, ast_symbol, mangled);
    }

    return result_ast_str_(p, ast_symbol, name);
}

static int a_attributed_identifier(parser *self) {
    // All identifiers may now have an attribute set, denoted by a [[...]] expression immediately preceding
    // the identifer. This will be tokenized as `[[, <zero or more tokens>, ]]`

    // This is pathed through a_value, so it must also support arity-qualified identifiers, e.g. foo/1

    ast_node *attributes = null;
    if (0 == a_try(self, a_attribute_set)) {
        attributes = self->result;
    }
    if (0 == a_try(self, a_identifier_optional_arity)) {
        ast_node_set_attributes(self->result, attributes);
        return 0;
    }
    return 1;
}

static int the_symbol(parser *p, char const *const want) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp(want, p->token.s)) return result_ast_str(p, ast_symbol, p->token.s);
    }

    p->error.tag = tl_err_expected_specific_symbol;
    return 1;
}

static int a_string(parser *p) {
    if (next_token(p)) return 1;

    if (tok_string == p->token.tag || tok_c_string == p->token.tag)
        return result_ast_str(p, ast_string, p->token.s);

    p->error.tag = tl_err_expected_string;
    return 1;
}

static int string_to_number(parser *parser, char const *const in) {
    i64 i;
    u64 u;
    f64 d;
    switch (str_parse_cnum(in, &i, &u, &d)) {
    case 1:  return result_ast_i64(parser, i);
    case 2:  return result_ast_u64(parser, u);
    case 3:  return result_ast_f64(parser, d);
    case 4:  return result_ast_i64_z(parser, i);
    case 5:  return result_ast_u64_zu(parser, u);
    default: return 1;
    }
}

static int a_number(parser *self) {
    if (next_token(self)) return 1;

    if (tok_number == self->token.tag) {

        if (string_to_number(self, self->token.s)) goto error;
        // sets parser result

        return 0;
    }

error:
    self->error.tag = tl_err_expected_number;
    return 1;
}

static int a_bool(parser *p) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp("true", p->token.s)) return result_ast_bool(p, 1);
        if (0 == strcmp("false", p->token.s)) return result_ast_bool(p, 0);
    }

    p->error.tag = tl_err_expected_bool;
    return 1;
}

static int a_equal_sign(parser *p) {
    if (next_token(p)) return 1;

    if (tok_equal_sign == p->token.tag) return result_ast_str(p, ast_symbol, "=");

    p->error.tag = tl_err_expected_equal_sign;
    return 1;
}

static int a_colon(parser *p) {
    if (next_token(p)) return 1;
    if (tok_colon == p->token.tag) return result_ast_str(p, ast_symbol, ":");
    p->error.tag = tl_err_expected_colon;
    return 1;
}

static int a_double_open_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_double_open_square == p->token.tag) return result_ast_str(p, ast_symbol, "[[");
    p->error.tag = tl_err_expected_double_open_square;
    return 1;
}

static int a_double_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_double_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]]");
    p->error.tag = tl_err_expected_double_close_square;
    return 1;
}

static int a_star(parser *p) {
    if (next_token(p)) return 1;
    if (tok_star == p->token.tag) return result_ast_str(p, ast_symbol, "*");
    p->error.tag = tl_err_expected_star;
    return 1;
}

static int is_ampersand(ast_node const *node) {
    return (ast_node_is_symbol(node) && str_eq(ast_node_str(node), S("&")));
}

static int a_ampersand(parser *p) {
    if (next_token(p)) return 1;
    if (tok_ampersand == p->token.tag) return result_ast_str(p, ast_symbol, "&");
    p->error.tag = tl_err_expected_ampersand;
    return 1;
}

static int a_colon_equal(parser *p) {
    if (next_token(p)) return 1;

    if (tok_colon_equal == p->token.tag) return result_ast_str(p, ast_symbol, ":=");

    p->error.tag = tl_err_expected_colon_equal;
    return 1;
}

static int a_arrow(parser *p) {
    if (next_token(p)) return 1;

    if (tok_arrow == p->token.tag) return result_ast_str(p, ast_symbol, "->");

    p->error.tag = tl_err_expected_arrow;
    return 1;
}

static int a_nil(parser *self) {

    if (0 == a_try_s(self, the_symbol, "void")) return result_ast(self, ast_void);
    if ((0 == a_open_round(self)) && (0 == a_close_round(self))) return result_ast(self, ast_void);

    self->error.tag = tl_err_expected_nil;
    return 1;
}

static int a_null(parser *self) {
    if (0 == the_symbol(self, "null")) return result_ast(self, ast_nil);
    self->error.tag = tl_err_expected_nil;
    return 1;
}

static int set_node_parameters(parser *self, ast_node *node, ast_node_array *parameters) {
    // given parsed parameters for a function or lambda, initialize
    // the node array properly

    array_shrink(*parameters);
    node->array.nodes = parameters->v;
    if (parameters->size > 0xff) return too_many_arguments(self);

    node->array.n = (u8)parameters->size;
    return 0;
}

static int a_type_identifier_base(parser *self);
static int a_type_identifier(parser *self);

// Parse a comma-separated parameter list between ( and ).
// Returns 0 on success, error_code on mid-list failure, 1 if ( is missing.
// Shared by a_type_arrow and toplevel_defun.
static int parse_param_list(parser *self, ast_node_array *out_params, int error_code) {
    if (a_try(self, a_open_round)) return 1;
    if (0 == a_try(self, a_close_round)) return 0;
    if (a_try(self, a_param)) return 1;
    array_push(*out_params, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) return 0;
        if (a_try(self, a_comma)) return error_code;
        if (a_try(self, a_param)) return error_code;
        array_push(*out_params, self->result);
    }
}

static int a_type_arrow(parser *self) {
    ast_node_array params = {.alloc = self->ast_arena};
    int            res    = parse_param_list(self, &params, ERROR_STOP);
    if (res) return res;

    if (a_try(self, a_arrow)) return 1;

    if (a_try(self, a_type_identifier)) return 1;
    ast_node *rhs = self->result;

    // make tuple
    ast_node *tup = ast_node_create_tuple(self->ast_arena, (ast_node_sized)array_sized(params));
    set_node_file(self, tup);

    // make arrow: type arguments will be parsed separately
    ast_node *arrow = ast_node_create_arrow(self->ast_arena, tup, rhs, (ast_node_sized){0});
    set_node_file(self, arrow);

    return result_ast_node(self, arrow);
}

static int maybe_mangle_binop(parser *self, ast_node *op, ast_node **inout, ast_node *right);

static int a_type_identifier_base(parser *self) {
    // Callers expect name to be mangled.
    if (0 == a_try(self, a_type_arrow)) return 0;

    if (0 == a_try(self, a_attributed_identifier)) {
        ast_node *ident = self->result;

        // Look for module-qualified identifier
        if (0 == a_try(self, a_dot)) {
            ast_node *op = self->result;

            if (a_try(self, a_type_identifier_base)) return 1;
            ast_node *right = self->result;

            if (maybe_mangle_binop(self, op, &ident, right)) {
                self->result = ident;
                return 0;
            } else {
                mangle_name(self, self->result);
                return 0;
            }
        } else {
            ast_node_array type_args;
            if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

            mangle_name(self, ident);

            if (type_args.size) {
                ast_node *r = ast_node_create_nfa(
                  self->ast_arena, ident, (ast_node_sized)sized_all(type_args), (ast_node_sized){0});
                return result_ast_node(self, r);
            } else {
                return result_ast_node(self, ident);
            }
        }
    }

    return 1;
}

static int a_type_identifier(parser *self) {
    if (0 == a_try(self, a_ellipsis)) return 0;
    return a_type_identifier_base(self);
}

static int a_type_annotation(parser *self) {
    if (0 == a_try(self, a_colon)) {
        int res = a_try(self, a_type_identifier);
        return res;
    }

    self->error.tag = tl_err_expected_type;
    return 1;
}

static int a_param(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *ident = self->result;

    // If followed by type arguments (e.g., Ptr[K] in a function pointer type),
    // parse as a generic type application rather than a named parameter.
    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

    if (type_args.size) {
        mangle_name(self, ident);
        ast_node *r = ast_node_create_nfa(
          self->ast_arena, ident, (ast_node_sized)sized_all(type_args), (ast_node_sized){0});
        return result_ast_node(self, r);
    }

    ast_node *ann = null;
    if (0 == a_try(self, a_type_annotation)) {
        ann = self->result;
    }

    assert(ast_node_is_symbol(ident));
    ident->symbol.annotation = ann;
    return result_ast_node(self, ident);
}

static ast_node *maybe_wrap_lambda_function_in_let_in(parser *self, ast_node *node) {
    // Note: special case to handle anonymous lambdas as function arguments: the transpiler requires every
    // lambda to be named, because the function is hoisted to a toplevel function. So here we wrap a lambda
    // function in a simple `let gen_name = f in f` form.
    if (!ast_node_is_lambda_function(node)) return node;

    ast_node *lval = ast_node_create_sym(self->ast_arena, next_var_name(self));
    ast_node *val  = node;

    // body of let: just the symbol referring to the lambda's name
    ast_node_array exprs = {.alloc = self->ast_arena};
    array_push(exprs, lval);
    ast_node *body = create_body(self, exprs, (ast_node_array){0});

    ast_node *a    = ast_node_create_let_in(self->ast_arena, lval, val, body);
    return a;
}

static int maybe_type_argument_element(parser *self) {
    if (0 == a_try(self, a_type_identifier)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    return 1;
}

static int maybe_type_arguments(parser *self, ast_node_array *type_args) {
    *type_args = (ast_node_array){.alloc = self->ast_arena};

    if (0 == a_try(self, a_open_square)) {
        if (0 == a_try(self, a_close_square)) goto type_args_done;
        if (maybe_type_argument_element(self)) return ERROR_STOP;
        array_push(*type_args, self->result);

        while (1) {
            if (0 == a_try(self, a_close_square)) goto type_args_done;
            if (a_try(self, a_comma)) return ERROR_STOP;
            if (maybe_type_argument_element(self)) return ERROR_STOP;
            array_push(*type_args, self->result);
        }
    }

type_args_done:
    return 0;
}

static int a_funcall(parser *self) {

    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

    if (a_try(self, a_open_round)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_round)) goto done;
    if (0 == a_try(self, a_expression)) {
        ast_node *_t = maybe_wrap_lambda_function_in_let_in(self, self->result);
        array_push(args, _t);
    }

    while (1) {
        if (0 == a_try(self, a_close_round)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_expression)) return 1;
        {
            ast_node *_t = maybe_wrap_lambda_function_in_let_in(self, self->result);
            array_push(args, _t);
        }
    }

done:
    array_shrink(args);

    // IMPORTANT: arity-mangle FIRST, then module-mangle.
    // symbol_is_module_function checks for the arity-mangled name (e.g., "foo__0") in
    // current_module_symbols. If we module-mangle first, we'd be checking for the wrong name.
    mangle_name_for_arity(self, name, args.size, 0); // 0 = function call, not definition
    mangle_name(self, name);

    ast_node *node = ast_node_create_nfa(self->ast_arena, name, (ast_node_sized)sized_all(type_args),
                                         (ast_node_sized)sized_all(args));
    return result_ast_node(self, node);
}

static int a_type_constructor(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;
    if (a_try(self, a_open_round)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_round)) goto done;
    if (0 == a_try(self, a_field_assignment)) array_push(args, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_field_assignment)) return 1;
        array_push(args, self->result);
    }

done:
    array_shrink(args);
    mangle_name(self, name);
    ast_node *node = ast_node_create_nfa_tc(self->ast_arena, name, (ast_node_sized)sized_all(type_args),
                                            (ast_node_sized)sized_all(args));
    return result_ast_node(self, node);
}

static int a_lambda_function(parser *self) {
    ast_node_array params = {.alloc = self->ast_arena};

    if (a_try(self, a_open_round)) return 1;
    if (0 == a_try(self, a_close_round)) goto decl_done;
    if (0 == a_try(self, a_param)) array_push(params, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto decl_done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_param)) return 1;
        array_push(params, self->result);
    }

decl_done:

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array exprs = {.alloc = self->ast_arena};

    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_body_element)) return 1;
        array_push(exprs, self->result);
    }

    array_shrink(exprs);
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)sized_all(exprs));
    set_node_file(self, body);

    ast_node *l = ast_node_create(self->ast_arena, ast_lambda_function);
    set_node_parameters(self, l, &params);
    l->lambda_function.body = body;
    return result_ast_node(self, l);
}

static int a_lambda_funcall(parser *self) {
    if (a_try(self, a_lambda_function)) return 1;
    ast_node *lambda = self->result;

    if (a_try(self, a_open_round)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_round)) goto done;
    if (0 == a_try(self, a_expression)) array_push(args, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_expression)) return 1;
        array_push(args, self->result);
    }

done:;

    ast_node *node = ast_node_create_lfa(self->ast_arena, lambda, (ast_node_sized)array_sized(args));
    return result_ast_node(self, node);
}

// Parse arity qualifier: /N where N is a non-negative integer.
// Used for function references with explicit type args: name[TypeArgs]/N
static int a_arity_qualifier(parser *self) {
    if (next_token(self)) return 1;
    if (tok_symbol != self->token.tag || 0 != strcmp("/", self->token.s)) return 1;

    if (next_token(self)) return 1;
    if (tok_number != self->token.tag) return 1;

    // Arities are plain non-negative integers only (no suffixes, no floats)
    char *end;
    errno = 0;
    long n = strtol(self->token.s, &end, 10);
    if (errno || end == self->token.s || *end != '\0' || n < 0 || n > 255) return 1;
    return result_ast_i64(self, (i64)n);
}

static int a_value(parser *self) {
    // Put funcall before type constructor, due to arity mangling
    if (0 == a_try(self, a_funcall)) return 0;
    if (0 == a_try(self, a_type_constructor)) return 0;
    if (0 == a_try(self, a_lambda_function)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    if (0 == a_try(self, a_string)) return 0;
    if (0 == a_try(self, a_char)) return 0;
    if (0 == a_try(self, a_bool)) return 0;
    if (0 == a_try(self, a_nil)) return 0;
    if (0 == a_try(self, a_null)) return 0;
    if (0 == a_try(self, a_attributed_identifier)) {

        ast_node *ident = self->result;
        assert(ast_node_is_symbol(ident));
        int is_none = str_eq(ident->symbol.name, S("None"));
        mangle_name(self, ident);

        // an identifier with type arguments in a value position can be used as
        // an operand of type predicates (e.g. opt :: Option[Int])
        ast_node_array type_args;
        if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;
        if (type_args.size) {
            // Check for arity qualifier: name[TypeArgs]/N (function reference with explicit type args)
            int is_fn_ref = 0;
            if (0 == a_try(self, a_arity_qualifier)) {
                u8 arity = (u8)self->result->i64.val;
                ast_node_name_replace(ident, mangle_str_for_arity(self->ast_arena, ident->symbol.name, arity));
                is_fn_ref = 1;
            }
            mangle_name(self, ident);
            ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                              (ast_node_sized){0});
            r->named_application.is_function_reference = is_fn_ref;
            return result_ast_node(self, r);
        } else {
            // Syntax sugar: promote naked None or nullary tagged union variant to zero-arg call
            if (is_none || str_map_contains(self->nullary_variant_parents, ident->symbol.name)) {
                ast_node *r =
                  ast_node_create_nfa_tc(self->ast_arena, ident, (ast_node_sized){0}, (ast_node_sized){0});
                return result_ast_node(self, r);

            } else {
                return result_ast_node(self, ident);
            }
        }
    }
    // standalone attribute set is used as an operand of a type predicates
    if (0 == a_try(self, a_attribute_set)) return 0;

    self->error.tag = tl_err_expected_value;
    return 1;
}

static int operator_precedence(char const *op, int is_prefix) {

    struct item {
        char const *op;
        int         p;
    };
    static struct item const infix[] = {

      {"=", 5},   {"+=", 5},   {"-=", 5},  {"*=", 5}, {"/=", 5}, {"%=", 5},

      {"<<=", 5}, {">>=", 5},  {"&=", 5},  {"^=", 5}, {"|=", 5},

      {"||", 10}, {"&&", 20},  {"|", 30},  {"&", 40},

      {"::", 50}, {"==", 50},  {"!=", 50},

      {"<", 60},  {"<=", 60},  {">=", 60}, {">", 60},

      {"<<", 70}, {">>", 70},

      {"+", 80},  {"-", 80},

      {"*", 90},  {"/", 90},   {"%", 90},

      {".", 110}, {"->", 110}, {"[", 110},

      {null, 0},
    };

    static struct item const prefix[] = {
      {"-", 100},
      {"+", 100},
      {"!", 100},
      {"~", 100},
      //
      {"*", 100},
      {"&", 100},
      //
      {null, 0},
    };

    for (struct item const *search = is_prefix ? prefix : infix; search->op; ++search) {
        if (0 == strcmp(op, search->op)) return search->p;
    }
    return INT_MIN;
}

// -- if expression --

static ast_node *parse_if_continue(parser *self) {
    // the "if" token has been seen
    ast_node *cond = parse_expression(self, INT_MIN);
    if (!cond) return null;
    if (a_try(self, a_open_curly)) {
        self->error.tag = tl_err_expected_if_then_arm;
        return null;
    }

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return null;
        else array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    ast_node *yes = create_body(self, exprs, defers);
    exprs         = (ast_node_array){.alloc = self->ast_arena};
    defers        = (ast_node_array){.alloc = self->ast_arena};

    ast_node *no  = null;

    if (0 == a_try_s(self, the_symbol, "else")) {
        if (0 == a_try_s(self, the_symbol, "if")) {
            no = parse_if_continue(self);
        } else {
            if (a_try(self, a_open_curly)) return null;
            while (1) {
                if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
                else if (a_try(self, a_body_element)) return null;
                else array_push(exprs, self->result);
                if (0 == a_try(self, a_close_curly)) break;
            }
            no = create_body(self, exprs, defers);
        }
    }
    if (!no) no = null; // ok to have no case null

    ast_node *n = ast_node_create_if_then_else(self->ast_arena, cond, yes, no);
    set_node_file(self, n);
    return n;
}

static ast_node *parse_if_expr(parser *self) {
    if (a_try_s(self, the_symbol, "if")) {
        self->error.tag = tl_err_ok;
        return null;
    }
    return parse_if_continue(self);
}

// -- cond expression

static ast_node *parse_body(parser *self) {
    if (a_try(self, a_open_curly)) return null;

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};

    // Check for empty body `{ }` as sugar for `{ void }`
    if (0 == a_try(self, a_close_curly)) {
        (void)result_ast(self, ast_void);
        array_push(exprs, self->result);
        return create_body(self, exprs, defers);
    }

    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return null;
        else array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    return create_body(self, exprs, defers);
}

//

static ast_node *parse_case_expr(parser *self) {
    if (a_try_s(self, the_symbol, "case")) {
        self->error.tag = tl_err_ok;
        return null;
    }

    ast_node *expr = parse_expression(self, INT_MIN);
    if (!expr) return null;

    int is_pointer = 0;
    if (ast_node_is_unary_op(expr)) {
        if (is_ampersand(expr->unary_op.op)) is_pointer = 1;
        else return null; // no other unary op is valid

        // reset variable to the actual expression
        expr = expr->unary_op.operand;
    }

    // look for optional predicate
    ast_node *bin_pred = null;
    if (0 == a_try(self, a_comma)) {
        bin_pred = parse_expression(self, INT_MIN);
        if (!bin_pred) return null;
        if (!ast_node_is_symbol(bin_pred) && !ast_node_is_lambda_function(bin_pred)) return null;
    }

    // look for optional union type for destructure expression. Exclusive with predicate, however.
    ast_node *union_type   = null;
    str       union_module = str_empty();
    if (!bin_pred) {
        if (0 == a_try(self, a_type_annotation)) {
            // case s: Foo.Shape { c: Circle { ... } ... }
            // `Foo.Shape` will be mangled to symbol Foo_Shape by a_type_annotation

            union_type = self->result;
            // Must be a symbol: other type annotations e.g. arrows and nfas are not permitted.
            if (!ast_node_is_symbol(union_type)) goto begin_body;
            union_module = union_type->symbol.module;
        }
    }

begin_body:
    if (a_try(self, a_open_curly)) return null;

    ast_node_array conditions = {.alloc = self->ast_arena};
    ast_node_array arms       = {.alloc = self->ast_arena};
    ast_node      *else_arm   = null;
    while (1) {
        // check for else condition
        if (0 == a_try_s(self, the_symbol, "else")) {
            if (else_arm) {
                self->error.tag = tl_err_unexpected_else;
                return null;
            }
            else_arm = parse_body(self);
            if (!else_arm) return null;
            continue;
        }
        if (0 == a_try(self, a_close_curly)) break;

        ast_node *cond = null;
        if (union_type) {
            // a union case expression: condition must be an annotated symbol: a symbol to be bound, and the
            // desired variant type to be matched. The variant type must be mangled to the union_module.
            // E.g. `c: Circle` is the symbol `c` with the annotation `Circle`, which must be mangled to
            // `Foo.Circle`.
            if (a_try(self, a_param)) return null;
            cond = self->result;
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation ||
                !ast_node_is_symbol(cond->symbol.annotation))
                return null;

            // mangle symbol for the union's module
            mangle_name_for_module(self, cond->symbol.annotation, union_module);
        } else {
            // standard case expression
            cond = parse_expression(self, INT_MIN);
        }

        if (!cond) return null;
        ast_node *body = parse_body(self);
        if (!body) return null;

        array_push(conditions, cond);
        array_push(arms, body);
    }

    if (else_arm) {
        // else arm always goes at the end
        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_arm);
    }

    int union_flag = 0;
    if (is_pointer) union_flag = AST_TAGGED_UNION_MUTABLE;
    else if (union_type) union_flag = AST_TAGGED_UNION_VALUE;

    ast_node *node =
      ast_node_create_case(self->ast_arena, expr, (ast_node_sized)array_sized(conditions),
                           (ast_node_sized)array_sized(arms), bin_pred, union_type, union_flag);
    set_node_file(self, node);
    return node;
}

static ast_node *parse_when_expr(parser *self) {
    if (a_try_s(self, the_symbol, "when")) {
        self->error.tag = tl_err_ok;
        return null;
    }

    ast_node *expr = parse_expression(self, INT_MIN);
    if (!expr) return null;

    int is_pointer = 0;
    if (ast_node_is_unary_op(expr)) {
        if (is_ampersand(expr->unary_op.op)) is_pointer = 1;
        else return null; // no other unary op is valid

        // reset variable to the actual expression
        expr = expr->unary_op.operand;
    }

    if (a_try(self, a_open_curly)) return null;

    ast_node_array conditions = {.alloc = self->ast_arena};
    ast_node_array arms       = {.alloc = self->ast_arena};
    ast_node      *else_arm   = null;
    while (1) {
        // check for else condition
        if (0 == a_try_s(self, the_symbol, "else")) {
            if (else_arm) {
                self->error.tag = tl_err_unexpected_else;
                return null;
            }
            else_arm = parse_body(self);
            if (!else_arm) return null;
            continue;
        }
        if (0 == a_try(self, a_close_curly)) break;

        ast_node *cond = null;

        // a union case expression: condition must be an annotated symbol: a symbol to be bound, and the
        // desired variant type to be matched. The variant type must be mangled to the union_module.
        // E.g. `c: Circle` is the symbol `c` with the annotation `Circle`, which must be mangled to
        // `Foo.Circle`.
        if (a_try(self, a_param)) return null;
        cond = self->result;
        if (!ast_node_is_symbol(cond) || !cond->symbol.annotation ||
            !ast_node_is_symbol(cond->symbol.annotation))
            return null;

        // Note: Do not module-mangle annotation symbol: inference will use the unmangled name within the
        // module scope of the tagged union.

        if (!cond) return null;
        ast_node *body = parse_body(self);
        if (!body) return null;

        array_push(conditions, cond);
        array_push(arms, body);
    }

    if (else_arm) {
        // else arm always goes at the end
        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_arm);
    }

    int union_flag = 0;
    if (is_pointer) union_flag = AST_TAGGED_UNION_MUTABLE;
    else union_flag = AST_TAGGED_UNION_VALUE;

    ast_node *node = ast_node_create_case(self->ast_arena, expr, (ast_node_sized)array_sized(conditions),
                                          (ast_node_sized)array_sized(arms), null, null, union_flag);
    set_node_file(self, node);
    return node;
}

//

static int maybe_mangle_binop(parser *self, ast_node *op, ast_node **inout, ast_node *right) {
    // Module alias resolution: replace leftmost alias with original module name
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout)) {
        str *original = str_map_get(self->module_aliases, (*inout)->symbol.name);
        if (original)
            (*inout)->symbol.name = *original;
    }

    // Nested module resolution: if left is a module and "left.right" is also a module,
    // synthesize a combined module reference (e.g., Foo.Bar) for the next dot iteration.
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout) &&
        str_hset_contains(self->modules_seen, (*inout)->symbol.name) && ast_node_is_symbol(right)) {
        str combined = str_cat_3(self->ast_arena, (*inout)->symbol.name, S("."), right->symbol.name);
        if (str_hset_contains(self->modules_seen, combined)) {
            right->symbol.name = combined;
            *inout             = right;
            return 1;
        }
    }
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout) &&
        str_hset_contains(self->modules_seen, (*inout)->symbol.name)) {
        ast_node *to_mangle = null;
        u8        arity     = 0;
        if (ast_node_is_symbol(right)) to_mangle = right;
        else if (ast_node_is_nfa(right)) {
            to_mangle = right->named_application.name;
            arity     = right->named_application.n_arguments;
        }
        if (to_mangle) {
            unmangle_name(self, to_mangle);
            // When mangling a cross-module function call like ModuleOne.f(-1, 1),
            // we need to also mangle for arity. The arity mangling in a_funcall
            // doesn't know about the target module, so we do it here.
            str      target_module = (*inout)->symbol.name;
            str      original_name = to_mangle->symbol.name;
            str      mangled_name  = mangle_str_for_arity(self->ast_arena, original_name, arity);
            hashmap *module_syms   = str_map_get_ptr(self->module_symbols, target_module);
            if (module_syms && str_hset_contains(module_syms, mangled_name)) {
                to_mangle->symbol.name = mangled_name;
            }
            mangle_name_for_module(self, to_mangle, target_module);

            // Auto-invoke nullary tagged union variants: Opt.Empty → value, not fn ref
            if (to_mangle == right) { // bare symbol, not NFA
                str *parent = str_map_get(self->nullary_variant_parents, to_mangle->symbol.name);
                if (parent) {
                    // Build scoped variant struct name: parent__variant (e.g., T__Empty)
                    str       scoped = str_cat_3(self->ast_arena, *parent, S("__"), original_name);
                    ast_node *var_sym = ast_node_create_sym(self->ast_arena, scoped);
                    mangle_name_for_module(self, var_sym, target_module);
                    ast_node *inner_call = ast_node_create_nfa_tc(
                      self->ast_arena, var_sym, (ast_node_sized){0}, (ast_node_sized){0});
                    set_node_file(self, inner_call);
                    *inout = build_tagged_union_wrapping(self, *parent, original_name,
                                                         target_module, inner_call);
                    return 1;
                }
            }

            *inout = right;
            return 1;
        }
    }
    // UFCS cross-module: (x.Mod).foo(a, b) → binary_op(".", x, nfa("Mod__foo", [a, b]))
    if ((0 == str_cmp_c(op->symbol.name, ".")) &&
        ast_node_is_binary_op_struct_access(*inout) && ast_node_is_nfa(right) &&
        ast_node_is_symbol((*inout)->binary_op.right) &&
        str_hset_contains(self->modules_seen, (*inout)->binary_op.right->symbol.name)) {
        unmangle_name(self, right->named_application.name);
        mangle_name_for_module(self, right->named_application.name,
                               (*inout)->binary_op.right->symbol.name);
        *inout = ast_node_create_binary_op(self->ast_arena, op, (*inout)->binary_op.left, right);
        set_node_file(self, *inout);
        return 1;
    }
    // Check if left side is a type with nested types (dot syntax for nested structs / tagged union
    // variants)
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout)) {
        str parent_name = (*inout)->symbol.name;
        str module      = (*inout)->symbol.module;

        // Cross-module case: use original (unmangled) name
        if ((*inout)->symbol.is_mangled && !str_is_empty((*inout)->symbol.original))
            parent_name = (*inout)->symbol.original;

        if (str_hset_contains(self->nested_type_parents, parent_name)) {
            ast_node *to_mangle = null;
            if (ast_node_is_symbol(right)) to_mangle = right;
            else if (ast_node_is_nfa(right)) to_mangle = right->named_application.name;

            if (to_mangle) {
                // Build candidate child name and verify it exists in the correct module's symbols
                str child_name = to_mangle->symbol.is_mangled && !str_is_empty(to_mangle->symbol.original)
                                   ? to_mangle->symbol.original
                                   : to_mangle->symbol.name;
                str candidate_name = str_cat_3(self->ast_arena, parent_name, S("__"), child_name);

                // Look up in the appropriate module's symbol table
                hashmap *syms = null;
                if (!str_is_empty(module)) syms = str_map_get_ptr(self->module_symbols, module);
                else if (!str_is_empty(self->current_module))
                    syms = str_map_get_ptr(self->module_symbols, self->current_module);

                int found = 0;
                if (syms) found = str_hset_contains(syms, candidate_name);
                // Same-module in main (no module prefix): check current_module_symbols
                if (!found && str_is_empty(module))
                    found = str_hset_contains(self->current_module_symbols, candidate_name);

                if (found) {
                    unmangle_name(self, to_mangle);
                    to_mangle->symbol.name = candidate_name;
                    if (!str_is_empty(module)) mangle_name_for_module(self, to_mangle, module);
                    else mangle_name(self, to_mangle);

                    // Auto-wrap: if parent is a tagged union, wrap the result so it
                    // returns the tagged union instead of the bare variant struct.
                    if (str_hset_contains(self->tagged_union_variant_parents, parent_name)) {
                        if (ast_node_is_nfa(right)) {
                            // NFA case: Circle(2.0) or Circle(radius = 2.0) — already a call
                            *inout =
                              build_tagged_union_wrapping(self, parent_name, child_name, module, right);
                        } else {
                            // Bare symbol case: Op.A (zero-field variant, no parentheses)
                            // Promote to zero-arg NFA_TC then wrap (same pattern as None sugar)
                            ast_node *inner_call = ast_node_create_nfa_tc(
                              self->ast_arena, right, (ast_node_sized){0}, (ast_node_sized){0});
                            set_node_file(self, inner_call);
                            *inout = build_tagged_union_wrapping(self, parent_name, child_name, module,
                                                                 inner_call);
                        }
                    } else {
                        *inout = right;
                    }
                    return 1;
                }
            }
        }
    }

    return 0;
}

static ast_node *parse_base_expression(parser *self) {

    if (0 == a_try_s(self, the_symbol, "try")) {
        ast_node *operand = parse_expression(self, INT_MIN);
        if (!operand) return null;
        ast_node *n = ast_node_create_try(self->ast_arena, operand);
        set_node_file(self, n);
        return n;
    }

    if (0 == a_try_int(self, a_unary_operator, INT_MIN)) {
        ast_node *op   = self->result;
        int       prec = operator_precedence(str_cstr(&op->symbol.name), 1);
        ast_node *expr = parse_expression(self, prec);
        if (!expr) return null;
        ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, expr);
        set_node_file(self, unary);
        return unary;
    }

    // lambda function is identified by open round, so we need to parse it before nil and grouped
    // expressions.
    if (0 == a_try(self, a_lambda_funcall)) return self->result;
    if (0 == a_try(self, a_lambda_function)) return self->result;
    if (0 == a_try(self, a_nil)) return self->result; // parse () before (...)

    if (0 == a_try(self, a_open_round)) {

        ast_node *expr = null;

        // check for let-in expression before recursing
        if (0 == a_try(self, a_assignment)) expr = self->result;

        // if not let-in expression, check for body ({ ... }) before recursing
        if (!expr) expr = parse_body(self);

        if (!expr) expr = parse_expression(self, INT_MIN);
        if (a_try(self, a_close_round)) return null;
        return expr;
    }

    ast_node *node;
    node = parse_if_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;

    node = parse_case_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;

    node = parse_when_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;

    if (0 == a_try(self, a_value)) return self->result;
    return null;
}

static ast_node *parse_expression(parser *self, int min_prec) {

    ast_node *left = parse_base_expression(self);
    if (!left) return null;

    int assoc = 1; // 1: left assoc, 0: right assoc

    while (1) {
        if (0 == a_try_int(self, a_binary_operator, min_prec)) {
            ast_node *op = self->result;

            // Note: special case: .* and .& are converted to unary_op for legacy reasons
            // Note: special case: .[ index ] is pointer/CArray indexing
            if (0 == str_cmp_c(op->symbol.name, ".")) {
                if (0 == a_try(self, a_open_square)) {
                    ast_node *index_expr = parse_expression(self, INT_MIN);
                    if (!index_expr) return null;
                    if (a_try(self, a_close_square)) return null;
                    ast_node *index_op = ast_node_create_sym_c(self->ast_arena, ".[");
                    ast_node *binop =
                      ast_node_create_binary_op(self->ast_arena, index_op, left, index_expr);
                    set_node_file(self, binop);
                    left = binop;
                    continue;
                } else if (0 == a_try(self, a_star)) {
                    op              = self->result;
                    ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, left);
                    set_node_file(self, unary);
                    left = unary;
                    continue;
                } else if (0 == a_try(self, a_ampersand)) {
                    op              = self->result;
                    ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, left);
                    set_node_file(self, unary);
                    left = unary;
                    continue;
                }
            }

            int prec = operator_precedence(str_cstr(&op->symbol.name), 0);
            assert(prec >= min_prec);

            // if opening an index expression, reset min prec to minimum
            if (0 == str_cmp_c(op->symbol.name, "[")) prec = INT_MIN;

            ast_node *right = parse_expression(self, prec + assoc);
            if (!right) return null;

            // Note: special case: mangle Module.foo and Module.bar() to simple expressions
            if (maybe_mangle_binop(self, op, &left, right)) continue;

            // Note: special case: unmangle right hand side symbol following struct access operator
            char const *op_c = str_cstr(&op->symbol.name);
            if (is_struct_access_operator(op_c)) {
                unmangle_name(self, right);
            }

            // Note: special case: [ as binary operator, need to close it with ] token
            if (0 == str_cmp_c(op->symbol.name, "["))
                if (a_try(self, a_close_square)) return null;

            // Note: special case: detect type predicate with binop ::
            ast_node *binop = null;
            if (0 == str_cmp_c(op->symbol.name, "::")) {
                binop = ast_node_create_type_predicate(self->ast_arena, left, right);
            } else {
                binop = ast_node_create_binary_op(self->ast_arena, op, left, right);
            }
            set_node_file(self, binop);
            left = binop;

        } else break;
    }

    return left;
}

static int a_expression(parser *self) {
    ast_node *res = parse_expression(self, INT_MIN);
    if (!res) return 1;
    return result_ast_node(self, res);
}

static int unmangle_arity(str name) {
    // Returns -1 if name is not mangled with the format `foo/X` where X is an integer in the range [0,
    // 255]. Otherwise, returns X.
    char const *s = str_cstr(&name);
    char const *p = strrchr(s, '/');
    if (!p || p == s) return -1;
    if ((size_t)(p - s) + 1 >= str_len(name)) return -1;
    p++;
    // check for '0' directly because strtol returns 0 upon error
    if (*p == '0') return 0;

    long num = strtol(p, null, 10);
    if (num > 255 || num < 1) return -1;
    return (int)num;
}

static str unmangle_arity_qualified_name(allocator *alloc, str name) {
    // Returns str_empty if name is not arity-qualified (e.g. `foo/X`). Otherwise returns the name portion
    // as a newly allocated str.
    char const *s = str_cstr(&name);
    char const *p = strrchr(s, '/');
    if (!p || p == s) return str_empty();
    if ((size_t)(p - s) + 1 >= str_len(name)) return str_empty();
    return str_init_n(alloc, s, p - s);
}

static void unmangle_name(parser *self, ast_node *name) {
    (void)self;
    if (ast_node_is_nfa(name)) {
        unmangle_name(self, name->named_application.name);
        return;
    }
    if (!ast_node_is_symbol(name)) return;
    if (!name->symbol.is_mangled) return;
    if (str_is_empty(name->symbol.original)) return;
    name->symbol.name       = name->symbol.original;
    name->symbol.is_mangled = 0;
    str_deinit(self->ast_arena, &name->symbol.module);
}

// Check if a symbol is a known module-level function (not a type).
// Returns true only for symbols that exist in module_symbols (with arity mangling) and are not types.
// Used for arity mangling: only mangle calls to known module functions, not local variables.
static int symbol_is_module_function(parser *self, ast_node *name, u8 arity) {
    if (!ast_node_is_symbol(name)) return 0;

    // Get the original (unmangled) name for type check
    str original_name = name->symbol.original;
    if (str_is_empty(original_name)) original_name = name->symbol.name;

    // Types should not be mangled for arity
    if (tl_type_registry_get(self->opts.registry, original_name)) return 0;

    // Generate the arity-mangled name to search for
    str mangled_name = mangle_str_for_arity(self->ast_arena, original_name, arity);

    // Check builtin symbols first (e.g., sizeof, alignof)
    if (str_hset_contains(self->builtin_module_symbols, mangled_name)) return 1;

    // Get module name
    str module_name = name->symbol.module;

    // For symbols in the current module (empty module name), check current_module_symbols directly.
    // This handles the case where we're parsing a call within the same module where the function is
    // defined.
    if (str_is_empty(module_name)) {
        int found = str_hset_contains(self->current_module_symbols, mangled_name);
        return found;
    }

    // For cross-module references, look up the module's symbol table
    hashmap *module_syms = str_map_get_ptr(self->module_symbols, module_name);
    if (!module_syms) return 0;

    // Check if arity-mangled symbol exists in that module
    return str_hset_contains(module_syms, mangled_name);
}

static str mangle_str_for_module(parser *self, str name, str module) {
    str safe_module = str_replace_char_str(self->ast_arena, module, '.', S("__"));
    return str_cat_3(self->ast_arena, safe_module, S("__"), name);
}

str mangle_str_for_arity(allocator *alloc, str name, u8 arity) {
    return str_fmt(alloc, "%s__%i", str_cstr(&name), (int)arity);
}

static void mangle_name_for_module(parser *self, ast_node *name, str module) {
    if (ast_node_is_symbol(name) && !str_is_empty(module)) {
        ast_node_name_replace(name, mangle_str_for_module(self, name->symbol.name, module));
        name->symbol.is_mangled = 1;
        name->symbol.module     = str_copy(self->ast_arena, module);
        if (0) {
            fprintf(stderr, "parser: mangle '%s' to '%s'\n", str_cstr(&name->symbol.original),
                    str_cstr(&name->symbol.name));
        }
    }
}

static void mangle_name_for_arity(parser *self, ast_node *name, u8 arity, int is_definition) {
    if (!ast_node_is_symbol(name)) return;
    str name_str = name->symbol.name;
    if (is_main_function(name_str)) return;
    if (is_c_symbol(name_str)) return;

    // For function calls (not definitions), only mangle if the symbol is a known module-level function.
    // Do NOT mangle unknown symbols - they might be local variables (parameters, let bindings).
    // This is the key fix: previously we mangled unknown symbols, which broke calls like f(x)
    // where f is a parameter.
    // For function definitions, always mangle - the definition establishes the mangled name.
    if (!is_definition && !symbol_is_module_function(self, name, arity)) {
        return;
    }

    ast_node_name_replace(name, mangle_str_for_arity(self->ast_arena, name_str, arity));
    name_str = name->symbol.name;

    // Don't set is_mangled: that flag is to indicate module mangling, not arity mangling.
    dbg(self, "arity mangle '%s' to '%s'\n", str_cstr(&name->symbol.original), str_cstr(&name_str));
}

static void mangle_name(parser *self, ast_node *name) {
    // Note: module `main` set current_module to empty, so names are not mangled at all.
    if (str_is_empty(self->current_module)) return;
    if (ast_node_is_nfa(name)) {
        mangle_name(self, name->named_application.name);
        return;
    }
    if (!ast_node_is_symbol(name)) return;
    if (name->symbol.is_mangled) return;
    if (is_intrinsic(name->symbol.name)) return;

    // Don't mangle names of known types
    str name_str = ast_node_str(name);
    if (tl_type_registry_get(self->opts.registry, name_str)) return;

    // Don't mangle c_ names
    if (is_c_symbol(name_str)) return;

    // Don't mangle names in 'builtin' module
    if (str_eq(self->current_module, S("builtin"))) return;
    if (str_hset_contains(self->builtin_module_symbols, name_str)) return;

    // Don't mangle names that haven't been defined in the current module
    if (!str_hset_contains(self->current_module_symbols, name_str)) return;

    mangle_name_for_module(self, name, self->current_module);
}

static ast_node *parse_lvalue(parser *self) {
    ast_node *ident = null;

    ast_node *expr  = parse_expression(self, INT_MIN);
    if (!expr) return null;

    if (ast_node_is_symbol(expr)) ident = expr;
    else if (ast_binary_op == expr->tag) {
        char const *op = str_cstr(&expr->binary_op.op->symbol.name);
        if (is_struct_access_operator(op) || is_index_operator(op)) ident = expr;
    } else if (ast_unary_op == expr->tag) {
        if (str_eq(ast_node_str(expr->unary_op.op), S("*"))) ident = expr;
    }
    if (!ident) return null;

    ast_node *ann = null;
    if (0 == a_try(self, a_type_annotation)) {
        ann                = self->result;

        ast_node *leftmost = ident;
        while (!ast_node_is_symbol(leftmost)) {
            if (ast_binary_op == leftmost->tag) leftmost = leftmost->binary_op.left;
            else fatal("logic error");
        }

        leftmost->symbol.annotation = ann;
    }

    return ident;
}

static int a_reassignment(parser *self) {
    // x = newval
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    ast_node *op = null;
    if (0 == a_try(self, a_equal_sign)) {
        // op stays null
    } else if (0 == a_try_int(self, a_assignment_by_operator, INT_MIN)) {
        op = self->result;
    } else {
        return 1;
    }

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a = op ? ast_node_create_reassignment_op(self->ast_arena, lval, val, op)
                     : ast_node_create_reassignment(self->ast_arena, lval, val);
    return result_ast_node(self, a);
}

static int a_field_assignment(parser *self) {
    // x = val (for type literals)
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *name = self->result;

    ast_node *ann  = null;
    if (0 == a_try(self, a_type_annotation)) ann = self->result;

    if (a_try(self, a_equal_sign)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a                           = ast_node_create_assignment(self->ast_arena, name, val);
    a->assignment.is_field_name           = 1;
    a->assignment.name->symbol.annotation = ann;
    return result_ast_node(self, a);
}

static int ast_body_is_diverging(ast_node const *node);

static int ast_node_is_diverging(ast_node const *node) {
    if (!node) return 0;
    if (ast_return == node->tag) return 1; // return and break (is_break_statement)
    if (ast_continue == node->tag) return 1;
    if (ast_case == node->tag) {
        // A case/when node diverges if all arms diverge (e.g. nested let-else inside an else block)
        if (0 == node->case_.arms.size) return 0;
        for (u32 i = 0; i < node->case_.arms.size; i++) {
            if (!ast_body_is_diverging(node->case_.arms.v[i])) return 0;
        }
        return 1;
    }
    return 0;
}

static int ast_body_is_diverging(ast_node const *node) {
    if (!node) return 0;
    if (ast_body != node->tag) return ast_node_is_diverging(node);
    if (0 == node->body.expressions.size) return 0;
    return ast_node_is_diverging(node->body.expressions.v[node->body.expressions.size - 1]);
}

static int a_assignment(parser *self) {
    // Note: this is a let-in expression
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    if (a_try(self, a_colon_equal)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    // Bail form: s: MySome := val else { diverge }
    // Desugars to: when val { s: MySome { <remaining body> } else { <diverge> } }
    if (ast_node_is_symbol(lval) && lval->symbol.annotation && 0 == a_try_s(self, the_symbol, "else")) {
        ast_node *else_body = parse_body(self);
        if (!else_body) return 1;

        if (!ast_body_is_diverging(else_body)) {
            self->error.tag = tl_err_tagged_union_bail_else_must_diverge;
            return 1;
        }

        // Parse remaining body expressions (the continuation after the bail)
        ast_node_array exprs  = {.alloc = self->ast_arena};
        ast_node_array defers = {.alloc = self->ast_arena};
        while (1) {
            if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
            else if (a_try(self, a_body_element)) break;
            else array_push(exprs, self->result);
        }
        ast_node *success_body = create_body_fallback(self, exprs, defers, val);

        // Build case node: condition is the annotated lval, else arm is the bail body
        ast_node_array conditions = {.alloc = self->ast_arena};
        ast_node_array arms       = {.alloc = self->ast_arena};
        array_push(conditions, lval);
        array_push(arms, success_body);

        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_body);

        ast_node *node =
          ast_node_create_case(self->ast_arena, val, (ast_node_sized)array_sized(conditions),
                               (ast_node_sized)array_sized(arms), null, null, AST_TAGGED_UNION_VALUE);
        set_node_file(self, node);
        return result_ast_node(self, node);
    }

    // Normal let-in path
    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) break;
        else array_push(exprs, self->result);
    }

    ast_node *body = create_body_fallback(self, exprs, defers, val);

    ast_node *a    = ast_node_create_let_in(self->ast_arena, lval, val, body);
    return result_ast_node(self, a);
}

static int a_return_statement(parser *self) {
    if (a_try_s(self, the_symbol, "return")) return 1;

    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) {
        // allow `return` without an argument to mean `return void`
        result_ast(self, ast_void);
        value = self->result;
    }

    ast_node *r = ast_node_create_return(self->ast_arena, value, 0);
    return result_ast_node(self, r);
}

static int a_break_statement(parser *self) {
    if (a_try_s(self, the_symbol, "break")) return 1;

    ast_node *r = ast_node_create_return(self->ast_arena, null, 1);
    return result_ast_node(self, r);
}

static int a_continue_statement(parser *self) {
    if (a_try_s(self, the_symbol, "continue")) return 1;

    ast_node *r = ast_node_create_continue(self->ast_arena);
    return result_ast_node(self, r);
}

static int a_while_statement(parser *);
static int a_for_statement(parser *);
static int a_defer_eligible_statement(parser *self) {
    if (0 == a_try(self, a_assignment)) return 0;
    if (0 == a_try(self, a_reassignment)) return 0;
    if (0 == a_try(self, a_while_statement)) return 0;
    if (0 == a_try(self, a_for_statement)) return 0;

    return 1;
}

static int a_defer_eligible_expression(parser *self) {
    ast_node *res = parse_expression(self, INT_MIN);
    if (!res) return 1;
    return result_ast_node(self, res);
}

static int a_defer_eligible_body_element(parser *self) {
    // Note: statement before expression, because assignment and ident are ambiguous. Commas can be ignored,
    // so they can be used between body elements for readability.
    int ignore = a_try(self, a_comma);
    (void)ignore; // for GCC
    if (0 == a_try(self, a_defer_eligible_statement) || 0 == a_try(self, a_defer_eligible_expression))
        return 0;
    else return 1;
}

static int a_defer_statement(parser *self) {
    if (a_try_s(self, the_symbol, "defer")) return 1;

    // single element
    if (0 == a_try(self, a_defer_eligible_body_element)) return 0;

    // block element
    if (a_try(self, a_open_curly)) return 1;
    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_defer_eligible_body_element)) return ERROR_STOP; // stop parsing
        array_push(exprs, self->result);
    }
    // cannot nest defer inside defer
    ast_node *body = create_body(self, exprs, (ast_node_array){0});
    return result_ast_node(self, body);
}

static int a_while_statement(parser *self) {
    if (a_try_s(self, the_symbol, "while")) return 1;

    ast_node *condition = parse_expression(self, INT_MIN);
    if (!condition) return 1;

    // optional update expression: a command, then expression, before the open curly
    ast_node *update = null;
    if (0 == a_try(self, a_comma)) {
        if (a_try(self, a_statement)) return 1;
        update = self->result;
    }

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return 1;
        else array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs, defers);

    ast_node *r    = ast_node_create_while(self->ast_arena, condition, update, body);
    return result_ast_node(self, r);
}

static int a_for_statement(parser *self) {
    // The `for` statement is sugar over a while statement which uses a defined iterator interface. It has
    // four forms: with or without a Module specifier, and with or without a pointer specifier. If the
    // module specifier is omitted, Array is used as the default. The pointer specifier (using the &
    // operator) indicates the loop variable is a pointer which provides access to the underlying storage.
    //
    //     for x in xs { ... }
    //     for x.& in xs { ... }
    //     for x in Module xs { ... }
    //     for x.& in Module xs { ... }
    //
    // The iterator interface requires modules to implement the following functions:
    //
    //     iter_init    (iterable: Ptr(T)) -> Iter // T is the iterable, and Iter is any type.
    //     iter_value   (Ptr(Iter))        -> TValue
    //     iter_ptr     (Ptr(Iter))        -> Ptr(TValue)
    //     iter_cond    (Ptr(Iter))        -> Bool
    //     iter_update  (Ptr(Iter))        -> Void
    //     iter_deinit  (Ptr(Iter))        -> Void
    //
    // See <Array.tl> for a sample implementation
    //
    // The statement `for x in Module xs { ... }` desugars into:
    //
    //     iter := Module.iter_init(xs.&)
    //     while Module.iter_cond(iter.&), Module.iter_update(iter.&) {
    //       x := Module.iter_value(iter.&)
    //       ...
    //     }
    //     Module.iter_deinit(iter.&)
    //
    // The statement `for x.& in Module xs { ... }` desugars into:
    //
    //     iter := Module.iter_init(xs.&)
    //     while Module.iter_cond(iter.&), Module.iter_update(iter.&) {
    //       x := Module.iter_ptr(iter.&)
    //       ...
    //     }
    //     Module.iter_deinit(iter.&)
    //

    if (a_try_s(self, the_symbol, "for")) return 1;
    ast_node *variable = parse_expression(self, INT_MIN);
    if (!variable || (!ast_node_is_symbol(variable) && !ast_node_is_unary_op(variable))) return 1;

    int is_pointer = 0;
    if (ast_node_is_unary_op(variable)) {
        if (is_ampersand(variable->unary_op.op)) is_pointer = 1;
        else return 1; // no other unary op is valid

        // reset variable to the actual symbol
        variable = variable->unary_op.operand;
        if (!ast_node_is_symbol(variable)) return 1;
    }

    if (a_try_s(self, the_symbol, "in")) return 1;

    // one or two expressions can follow before the open brace

    ast_node *first     = parse_expression(self, INT_MIN);
    ast_node *second    = null;

    int       saw_curly = 0;
    if (a_try(self, a_open_curly)) second = parse_expression(self, INT_MIN);
    else saw_curly = 1;

    ast_node *module   = null;
    ast_node *iterable = null;
    if (!second) {
        // either second failed to parse, or we saw a curly
        if (!saw_curly) return 1;
        iterable = first;
    } else {
        // we saw two expressions, make sure they meet our requirements

        // first expression must be a symbol because it's a module name
        if (!ast_node_is_symbol(first)) return 1;
        module = first;

        // second can be anything
        iterable = second;
    }

    if (module) {
        assert(!saw_curly);
        // Now we need the open curly
        if (a_try(self, a_open_curly)) return 1;
    }

    // Read body of for loop
    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return 1;
        else array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }
    ast_node *user_body = create_body(self, exprs, defers);

    // Construct enclosing let-in(s) for the body of the loop. For value iteration, we need two let-ins:
    // first to grab the iterator pointer, and second to set the value variable. For pointer iteration, we
    // just need one let-in: to grab the iterator pointer and set the value variable.

    // First, we need a name for the hidden iterator variable
    ast_node *iterator = ast_node_create_sym_c(self->ast_arena, "gen_iter");

    // And we need an address-of operation for the iterator and the iterable
    ast_node *address_of       = ast_node_create_sym_c(self->ast_arena, "&");
    ast_node *iterator_address = ast_node_create_unary_op(self->ast_arena, address_of, iterator);
    ast_node *iterable_address = ast_node_create_unary_op(self->ast_arena, address_of, iterable);

    // Do we use default Array module, or a user-provided module?
    str module_name;
    if (module) module_name = ast_node_str(module);
    else module_name = S("Array");

    // Create the nfa for iter_init
    ast_node *call_iter_init = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterable_address;
        ast_node *iter_init      = ast_node_create_sym_c(self->ast_arena, "iter_init__1");
        mangle_name_for_module(self, iter_init, module_name);
        call_iter_init = ast_node_create_nfa(self->ast_arena, iter_init, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_value
    ast_node *call_iter_value = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_value     = ast_node_create_sym_c(self->ast_arena, "iter_value__1");
        mangle_name_for_module(self, iter_value, module_name);
        call_iter_value = ast_node_create_nfa(self->ast_arena, iter_value, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_ptr
    ast_node *call_iter_ptr = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_ptr       = ast_node_create_sym_c(self->ast_arena, "iter_ptr__1");
        mangle_name_for_module(self, iter_ptr, module_name);
        call_iter_ptr = ast_node_create_nfa(self->ast_arena, iter_ptr, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_cond
    ast_node *call_iter_cond = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_cond      = ast_node_create_sym_c(self->ast_arena, "iter_cond__1");
        mangle_name_for_module(self, iter_cond, module_name);
        call_iter_cond = ast_node_create_nfa(self->ast_arena, iter_cond, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_update
    ast_node *call_iter_update = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_update    = ast_node_create_sym_c(self->ast_arena, "iter_update__1");
        mangle_name_for_module(self, iter_update, module_name);
        call_iter_update =
          ast_node_create_nfa(self->ast_arena, iter_update, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_deinit
    ast_node *call_iter_deinit = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_deinit    = ast_node_create_sym_c(self->ast_arena, "iter_deinit__1");
        mangle_name_for_module(self, iter_deinit, module_name);
        call_iter_deinit =
          ast_node_create_nfa(self->ast_arena, iter_deinit, (ast_node_sized){0}, iter_args);
    }

    ast_node *while_body = null;
    {
        ast_node *lhs      = variable;
        ast_node *rhs      = is_pointer ? call_iter_ptr : call_iter_value;
        ast_node *for_body = ast_node_create_let_in(self->ast_arena, lhs, rhs, user_body);
        while_body         = for_body;
    }

    // Now we need to construct the while statement. It will be enclosed in a let-in which initializes the
    // iterator. And it will be followed by a funcall to free the iterator. So we will have: let iter = init
    // in body (while, free).

    ast_node_array while_statement_exprs = {.alloc = self->ast_arena};

    ast_node      *condition             = call_iter_cond;
    ast_node      *update                = call_iter_update;
    ast_node      *while_statement = ast_node_create_while(self->ast_arena, condition, update, while_body);

    // The body of the let-in: the while statement followed by the iter_deinit
    array_push(while_statement_exprs, while_statement);
    array_push(while_statement_exprs, call_iter_deinit);
    ast_node *while_statement_exprs_body = create_body(self, while_statement_exprs, (ast_node_array){0});

    ast_node *lhs                        = iterator;
    ast_node *rhs                        = call_iter_init;
    ast_node *let_iter_init = ast_node_create_let_in(self->ast_arena, lhs, rhs, while_statement_exprs_body);

    // The resulting node is a let-in:
    return result_ast_node(self, let_iter_init);
}

static int a_statement(parser *self) {
    if (0 == a_try(self, a_assignment)) return 0;
    if (0 == a_try(self, a_reassignment)) return 0;
    if (0 == a_try(self, a_while_statement)) return 0;
    if (0 == a_try(self, a_for_statement)) return 0;
    if (0 == a_try(self, a_break_statement)) return 0;
    if (0 == a_try(self, a_continue_statement)) return 0;
    if (0 == a_try(self, a_return_statement)) return 0;

    return 1;
}

static int a_body_element(parser *self) {
    // Note: statement before expression, because assignment and ident are ambiguous. Commas can be ignored,
    // so they can be used between body elements for readability.
    int ignore = a_try(self, a_comma);
    (void)ignore; // for GCC
    if (0 == a_try(self, a_statement) || 0 == a_try(self, a_expression)) return 0;
    else return 1;
}

static ast_node *create_body(parser *self, ast_node_array exprs, ast_node_array defers) {
    array_shrink(exprs);
    ast_node *body    = ast_node_create_body(self->ast_arena, (ast_node_sized)sized_all(exprs));
    body->body.defers = (ast_node_sized)sized_all(defers);
    set_node_file(self, body);
    return body;
}

static ast_node *create_body_fallback(parser *self, ast_node_array exprs, ast_node_array defers,
                                      ast_node *fallback) {
    if (0 == exprs.size && fallback) array_push(exprs, fallback);
    return create_body(self, exprs, defers);
}

// Parse an optional trait bound on a type parameter: T: Trait
// Expects self->result to be the type parameter identifier.
// On success, self->result is still the type parameter (with annotation set if bound present).
static int maybe_trait_bound(parser *self) {
    ast_node *type_param = self->result;
    if (0 == a_try(self, a_colon)) {
        if (a_try(self, a_type_identifier)) return 1;
        type_param->symbol.annotation = self->result;
    }
    self->result = type_param;
    return 0;
}

static int toplevel_defun(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name        = self->result;
    ast_node_array type_params = {.alloc = self->ast_arena};
    ast_node_array params      = {.alloc = self->ast_arena};

    if (0 == a_try(self, a_open_square)) {
        if (0 == a_try(self, a_close_square)) goto type_params_done;
        if (a_try(self, a_identifier)) return 1;
        if (maybe_trait_bound(self)) return 1;
        array_push(type_params, self->result);

        while (1) {
            if (0 == a_try(self, a_close_square)) goto type_params_done;
            if (a_try(self, a_comma)) return 1;
            if (a_try(self, a_identifier)) return 1;
            if (maybe_trait_bound(self)) return 1;
            array_push(type_params, self->result);
        }
    }

type_params_done:;
    int res = parse_param_list(self, &params, 1);
    if (res) return res;

    // optional arrow: if it is present, we need to add an annotation to the defun name's symbol
    if (0 == a_try(self, a_arrow)) {

        if (a_try(self, a_type_identifier)) return 1;
        ast_node *ann = self->result;

        // make tuple
        ast_node *tup = ast_node_create_tuple(self->ast_arena, (ast_node_sized)array_sized(params));
        set_node_file(self, tup);

        // make arrow
        ast_node *arrow =
          ast_node_create_arrow(self->ast_arena, tup, ann, (ast_node_sized)sized_all(type_params));
        set_node_file(self, arrow);

        // attach to name
        name->symbol.annotation = arrow;
    }

    if (a_try(self, a_open_curly)) return 1;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};

    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return ERROR_STOP; // stop parsing
        else array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs, defers);

    // arity-mangle the name before recording it in module symbols
    mangle_name_for_arity(self, name, params.size, 1); // 1 = function definition
    add_module_symbol(self, name);
    mangle_name(self, name);

    ast_node *let = ast_node_create_let(self->ast_arena, name, (ast_node_sized)sized_all(type_params),
                                        (ast_node_sized)sized_all(params), body);
    set_node_parameters(self, let, &params);
    let->let.name = name;
    let->let.body = body;

    result_ast_node(self, let);

    return 0;
}

static int toplevel_assign(parser *self) {
    // cannot use parse_lvalue here
    if (a_try(self, a_param)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_colon_equal)) return 1;
    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return ERROR_STOP;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    ast_node *n = ast_node_create_let_in(self->ast_arena, name, value, null);

    add_module_symbol(self, name);
    mangle_name(self, name);
    return result_ast_node(self, n);
}

static int toplevel_forward(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

    if (a_try(self, a_type_arrow)) return 1;
    ast_node *arrow = self->result;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    // add type arguments
    array_shrink(type_args);
    arrow->arrow.n_type_parameters = type_args.size;
    arrow->arrow.type_parameters   = type_args.v;

    // attach to name
    name->symbol.annotation = arrow;

    // Get arity from the arrow's parameter tuple
    ast_node_sized params = ast_node_sized_from_ast_array_const(arrow->arrow.left);
    mangle_name_for_arity(self, name, params.size, 1); // 1 = forward declaration (definition)

    add_module_symbol(self, name);
    mangle_name(self, name);

    return result_ast_node(self, name);
}

static int toplevel_symbol_annotation(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *ident = self->result;

    if (a_try(self, a_type_annotation)) return ERROR_STOP;
    ast_node *ann = self->result;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(ident)) return ERROR_STOP;

    assert(ast_node_is_symbol(ident));
    ident->symbol.annotation = ann;

    if (ast_node_is_arrow(ident->symbol.annotation)) {
        ast_node      *arrow  = ident->symbol.annotation;
        ast_node_sized params = ast_node_sized_from_ast_array_const(arrow->arrow.left);
        mangle_name_for_arity(self, ident, params.size, 1); // 1 = symbol annotation (definition)
    }

    add_module_symbol(self, ident);
    mangle_name(self, ident);
    return result_ast_node(self, ident);
}

static int toplevel_c_chunk(parser *self) {
    if (a_try(self, a_c_block)) return 1;
    return 0;
}

static void save_current_module_symbols(parser *self) {
    if (self->mode != mode_symbols) return;
    str      module_name = str_is_empty(self->current_module) ? S("main") : self->current_module;
    hashmap *copy        = map_copy(self->current_module_symbols);
    str_map_set_ptr(&self->module_symbols, module_name, copy);
}

static void load_module_symbols(parser *self) {
    str module_name = str_is_empty(self->current_module) ? S("main") : self->current_module;
    if (str_map_contains(self->module_symbols, module_name)) {
        // Don't destroy current_module_symbols here - after the first load, it points to a hashmap
        // in module_symbols, and destroying it would corrupt module_symbols entries.
        // The memory is managed by module_symbols and will be freed when the parser is destroyed.
        self->current_module_symbols = str_map_get_ptr(self->module_symbols, module_name);
    }
}

static void toplevel_hash_unity_file(parser *self, str argument) {
    self->skip_module   = 0;
    self->expect_module = 1;
    tokenizer_set_file(self->tokenizer, argument);
    map_reset(self->module_aliases);
}

static int toplevel_hash_module(parser *self, str cmd, str module) {
    // Modules: the name's sole use is to prevent multiple evaluations of the same terms. If a
    // duplicate name is seen, parsing will stop returning terms it sees until a new #module or new
    // #unity_file directive is seen.
    int is_prelude      = str_eq(cmd, S("module_prelude"));
    self->skip_module   = 0;
    self->expect_module = 0;

    // reject module names containing __ to avoid collisions with mangled names
    if (str_contains(module, S("__"))) {
        self->error.tag = tl_err_double_underscore_in_identifier;
        return ERROR_STOP;
    }

    // Validate immediate parent module exists for nested modules (e.g., Foo.Bar must exist for
    // Foo.Bar.Baz)
    str parent = str_empty();
    if (str_rprefix_char(self->transient, module, '.', &parent)) {
        int parent_known = str_hset_contains(self->modules_seen, parent) ||
                           (self->opts.known_modules && str_map_contains(self->opts.known_modules, parent));
        if (!parent_known) {
            self->error.tag = tl_err_nested_module_parent_not_found;
            return ERROR_STOP;
        }
    }
    str_deinit(self->transient, &parent);

    if (str_hset_contains(self->modules_seen, module)) self->skip_module = 1;
    else {
        // save current module symbols, if any
        save_current_module_symbols(self);

        // Prelude: don't add to modules_seen
        if (!is_prelude) str_hset_insert(&self->modules_seen, module);
        if (is_main_function(module)) self->current_module = str_empty();
        else {
            // Note: do not use ast_arena, as it could be speculative and discarded
            self->current_module = str_copy(self->parent_alloc, module);
        }

        // Only reset during first pass. During second pass, current_module_symbols may point
        // to a hashmap in module_symbols (set by load_module_symbols), and resetting it would
        // corrupt module_symbols.

        if (self->mode == mode_symbols) {
            hset_reset(self->current_module_symbols);
        }

        // load module symbols, if any (for re-opened modules, this pre-populates with prelude
        // symbols)
        load_module_symbols(self);
    }

    if (is_prelude) {
        str_hset_insert(&self->module_preludes_seen, module);
    }
    return 0;
}

static int toplevel_hash_alias(parser *self, str_array words) {
    if (words.size != 3) {
        self->error.tag = tl_err_expected_hash_command;
        return ERROR_STOP;
    }
    str alias  = words.v[1];
    str source = words.v[2];

    // self-alias
    if (str_eq(source, alias)) {
        self->error.tag = tl_err_alias_self_alias;
        return ERROR_STOP;
    }
    // __ in alias name
    if (str_contains(alias, S("__"))) {
        self->error.tag = tl_err_double_underscore_in_identifier;
        return ERROR_STOP;
    }
    // reserved prefix on alias
    if (is_c_symbol(alias) || is_intrinsic(alias)) {
        self->error.tag = tl_err_alias_invalid_name;
        return ERROR_STOP;
    }
    // source is main
    if (is_main_function(source)) {
        self->error.tag = tl_err_alias_source_is_main;
        return ERROR_STOP;
    }
    // source is an existing alias name
    if (str_map_contains(self->module_aliases, source)) {
        self->error.tag = tl_err_alias_source_is_alias;
        return ERROR_STOP;
    }
    // source module must exist
    int source_known = str_hset_contains(self->modules_seen, source) ||
                       (self->opts.known_modules && str_map_contains(self->opts.known_modules, source));
    if (!source_known) {
        self->error.tag = tl_err_alias_source_not_found;
        return ERROR_STOP;
    }
    // alias name conflicts with real module
    int alias_is_module = str_hset_contains(self->modules_seen, alias) ||
                          (self->opts.known_modules && str_map_contains(self->opts.known_modules, alias));
    if (alias_is_module) {
        self->error.tag = tl_err_alias_conflicts_with_module;
        return ERROR_STOP;
    }
    // alias name already defined
    if (str_map_contains(self->module_aliases, alias)) {
        self->error.tag = tl_err_alias_already_defined;
        return ERROR_STOP;
    }

    str source_copy = str_copy(self->parent_alloc, source);
    str_map_set(&self->module_aliases, alias, &source_copy);
    return 0;
}

static int toplevel_hash_unalias(parser *self, str alias) {
    if (!str_map_contains(self->module_aliases, alias)) {
        self->error.tag = tl_err_unalias_not_found;
        return ERROR_STOP;
    }
    str_map_erase(self->module_aliases, alias);
    return 0;
}

static int toplevel_hash(parser *self) {
    if (a_try(self, a_hash_command)) return 1;
    ast_node *command = self->result;

    str_array words   = {.alloc = self->ast_arena};
    str_parse_words(command->hash_command.full, &words);

    if (words.size >= 2) {
        str cmd      = words.v[0];
        str argument = words.v[1];
        int res      = 0;
        dbg(self, "hash: %s %s", str_cstr(&cmd), str_cstr(&argument));

        if (str_eq(cmd, S("unity_file"))) toplevel_hash_unity_file(self, argument);
        else if (str_eq(cmd, S("module_prelude")) || str_eq(cmd, S("module")))
            res = toplevel_hash_module(self, cmd, argument);
        else if (str_eq(cmd, S("alias"))) res = toplevel_hash_alias(self, words);
        else if (str_eq(cmd, S("unalias"))) res = toplevel_hash_unalias(self, argument);

        if (res) return res;
    }

    array_shrink(words);
    command->hash_command.words = (str_sized)sized_all(words);

    return 0;
}

static int toplevel_type_alias(parser *self) {
    ast_node *name = null;
    if (0 == a_try(self, a_attributed_identifier)) name = self->result;
    else return 1;
    if (a_try(self, a_equal_sign)) return 1;

    ast_node *target = null;
    if (0 == a_try(self, a_type_identifier)) target = self->result;
    else return 1;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    add_module_symbol(self, name);
    mangle_name(self, name);
    ast_node *node = ast_node_create_type_alias(self->ast_arena, name, target);
    return result_ast_node(self, node);
}

static int toplevel_enum(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    ast_node_array idents = {.alloc = self->ast_arena};
    while (1) {
        int saw_comma = 0;
        if (0 == a_try(self, a_comma)) saw_comma = 1; // optional comma
        if (0 == a_try(self, a_close_curly)) break;
        if (!saw_comma && idents.size) {
            // require comma separators
            if (a_try(self, a_comma)) return 1;
        }
        if (a_try(self, a_attributed_identifier))
            return 1; // enum must be an identifier; not mangled because access is through the type name
        array_push(idents, self->result);
    }
    array_shrink(idents);
    if (!idents.size) {
        array_free(idents);
        return 1;
    }

    add_module_symbol(self, name);
    mangle_name(self, name);

    // an enum uses the ast_user_type_definition with no type_arguments and no field_annotations. The actual
    // enums are saved in field_names.
    ast_node *r                       = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union         = 0;
    r->user_type_def.name             = name;
    r->user_type_def.n_type_arguments = 0;
    r->user_type_def.type_arguments   = null;
    r->user_type_def.field_types      = null;

    // The utd struct separates names from annotations, while they are both in the same symbol ast
    // node variant. So we have to do this splitting just for it to recombine later.
    r->user_type_def.n_fields          = idents.size;
    r->user_type_def.field_names       = alloc_malloc(self->ast_arena, idents.size * sizeof(ast_node *));
    r->user_type_def.field_annotations = null;
    forall(i, idents) {
        r->user_type_def.field_names[i] = idents.v[i];
    }

    return result_ast_node(self, r);
}

// Forward declarations needed by parse_struct_fields
static u8        collect_used_type_params(parser *self, u8 n_type_args, ast_node **type_args,
                                          ast_node_array fields, ast_node ***out_used_type_args);
static ast_node *create_utd(parser *self, ast_node *name, u8 n_type_args, ast_node **type_args,
                            ast_node_array fields, int is_union);

// Nested struct name tracking for annotation rewriting
typedef struct {
    str        bare_name;     // e.g. "Bar"
    str        prefixed_name; // e.g. "Foo_Bar"
    u8         n_type_args;
    ast_node **type_args;
} nested_struct_info;

// Try to parse a nested struct header: identifier ':' '{'
// On success, sets self->result to the identifier node and returns 0.
// On failure, returns nonzero and a_try backtracks consumed tokens.
static int a_nested_struct_header(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *ident = self->result;
    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;
    self->result = ident;
    return 0;
}

// Parse struct fields between { and }, handling nested struct definitions recursively.
// parent_prefix: name prefix for nested structs (e.g. "Foo")
// parent_n_type_args/parent_type_args: type params inherited from parent
// out_fields: populated with the parent's fields (nested struct entries removed)
// out_nested_utds: collects desugared nested struct UTD nodes
// Returns 0 on success, nonzero on failure.
static int parse_struct_fields(parser *self, str parent_prefix, u8 parent_n_type_args,
                               ast_node **parent_type_args, ast_node_array *out_fields,
                               ast_node_array *out_nested_utds) {

    // Track nested struct names for annotation rewriting
    struct {
        nested_struct_info *v;
        allocator          *alloc;
        u32                 size;
        u32                 cap;
    } nested_infos = {.v = null, .alloc = self->ast_arena};

    while (1) {
        int saw_comma = 0;
        if (0 == a_try(self, a_comma)) saw_comma = 1;
        if (0 == a_try(self, a_close_curly)) break;
        if (!saw_comma && out_fields->size) {
            if (a_try(self, a_comma)) return 1;
        }

        // Try parsing a nested struct: identifier ':' '{'
        // a_try handles backtracking if the pattern doesn't match
        if (0 == a_try(self, a_nested_struct_header)) {
            ast_node *nested_ident = self->result;
            // a_nested_struct_header consumed identifier, ':', and '{'
            // Now parse the nested struct body (fields until '}')
            str nested_bare_name = nested_ident->symbol.name;
            str prefixed_name    = str_cat_3(self->ast_arena, parent_prefix, S("__"), nested_bare_name);

            // Recursively parse nested struct fields
            ast_node_array nested_fields = {.alloc = self->ast_arena};
            int res = parse_struct_fields(self, prefixed_name, parent_n_type_args, parent_type_args,
                                          &nested_fields, out_nested_utds);
            if (res) return res;
            array_shrink(nested_fields);

            // Determine which parent type params are used by this nested struct
            ast_node **used_type_args = null;
            u8 n_used_type_args       = collect_used_type_params(self, parent_n_type_args, parent_type_args,
                                                                 nested_fields, &used_type_args);

            // Create the nested struct UTD
            ast_node *nested_name = ast_node_create_sym(self->ast_arena, prefixed_name);
            ast_node *nested_utd =
              create_utd(self, nested_name, n_used_type_args, used_type_args, nested_fields, 0);
            add_module_symbol(self, nested_name);
            mangle_name(self, nested_name);
            str_hset_insert(&self->nested_type_parents, parent_prefix);
            array_push(*out_nested_utds, nested_utd);

            // Record info for annotation rewriting
            nested_struct_info info = {
              .bare_name     = nested_bare_name,
              .prefixed_name = prefixed_name,
              .n_type_args   = n_used_type_args,
              .type_args     = used_type_args,
            };
            array_push(nested_infos, info);

            continue; // don't add as a field
        }

        // Not a nested struct — parse as regular field
        if (a_try(self, a_param)) return saw_comma ? ERROR_STOP : 1;
        array_push(*out_fields, self->result);
    }
    array_shrink(*out_fields);

    // Rewrite field annotations: replace bare nested names with prefixed + parameterized versions
    if (nested_infos.size) {
        forall(fi, *out_fields) {
            ast_node *ann = out_fields->v[fi]->symbol.annotation;
            if (!ann) continue;

            for (u32 ni = 0; ni < nested_infos.size; ni++) {
                nested_struct_info *info = &nested_infos.v[ni];

                // Check if annotation is a bare symbol matching the nested name
                if (ast_node_is_symbol(ann) && str_eq(ann->symbol.name, info->bare_name)) {
                    if (info->n_type_args) {
                        // Replace with Foo_Bar(T1, ...)
                        ast_node_sized args = {
                          .size = info->n_type_args,
                          .v    = alloc_malloc(self->ast_arena, info->n_type_args * sizeof(ast_node *))};
                        for (u8 j = 0; j < info->n_type_args; j++) {
                            args.v[j] = ast_node_clone(self->ast_arena, info->type_args[j]);
                        }
                        ast_node *new_name = ast_node_create_sym(self->ast_arena, info->prefixed_name);
                        mangle_name(self, new_name);

                        ast_node *new_ann =
                          ast_node_create_nfa(self->ast_arena, new_name, args, (ast_node_sized){0});
                        out_fields->v[fi]->symbol.annotation = new_ann;
                    } else {
                        // Replace with just Foo_Bar (no type params)
                        ann->symbol.name = info->prefixed_name;
                        mangle_name(self, ann);
                    }
                    break;
                }
            }
        }
    }

    return 0;
}

// Shared post-field-parsing logic for struct and union type definitions.
// Creates the UTD node, checks for reserved names and unused type params (structs only),
// registers the module symbol, and wraps with nested UTDs if present (structs only).
static int finalize_type_definition(parser *self, ast_node *type_ident, ast_node_array fields,
                                    ast_node_array nested_utds, int is_union) {
    if (is_reserved_type_name(type_ident)) return ERROR_STOP;

    u8         n_type_args = 0;
    ast_node **type_args   = null;
    ast_node  *name        = null;

    if (ast_node_is_symbol(type_ident)) {
        name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        name        = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else fatal("logic error");

    ast_node *r = create_utd(self, name, n_type_args, type_args, fields, is_union);

    // Check for unused type parameters (structs only)
    if (!is_union && r->user_type_def.n_type_arguments) {
        for (u8 j = 0; j < r->user_type_def.n_type_arguments; j++) {
            int used = 0;
            for (u32 i = 0; i < r->user_type_def.n_fields; i++) {
                if (annotation_uses_type_param(r->user_type_def.field_annotations[i],
                                               r->user_type_def.type_arguments[j]->symbol.name)) {
                    used = 1;
                    break;
                }
            }
            if (!used) {
                self->error.tag = tl_err_unused_type_parameter;
                return ERROR_STOP;
            }
        }
    }

    add_module_symbol(self, type_ident);
    mangle_name(self, type_ident);

    // If there are nested structs, return a body with nested UTDs first, then parent
    if (nested_utds.size) {
        ast_node_array result_nodes = {.alloc = self->ast_arena};
        forall(i, nested_utds) {
            array_push(result_nodes, nested_utds.v[i]);
        }
        array_push(result_nodes, r);
        array_shrink(result_nodes);

        ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)array_sized(result_nodes));
        set_node_file(self, body);
        return result_ast_node(self, body);
    }

    return result_ast_node(self, r);
}

// Parse a single trait function signature: name(param: Type, ...) -> RetType
static int a_trait_signature(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *sig_name = self->result;

    if (a_try(self, a_type_arrow)) return 1;
    ast_node *arrow = self->result;

    sig_name->symbol.annotation = arrow;
    return result_ast_node(self, sig_name);
}

static int toplevel_trait(parser *self) {
    // Parse trait declarations:
    //   Name[T] : { sig(a: T) -> T }
    //   Name[T] : Parent[T] { sig(a: T) -> T }
    //   Name[T] : Parent1[T], Parent2[T] { }

    if (a_try(self, a_type_identifier)) return 1;
    ast_node *type_ident = self->result;

    // Traits require type arguments — e.g. Eq[T]. Plain names are structs.
    if (!ast_node_is_nfa(type_ident)) return 1;
    if (type_ident->named_application.n_type_arguments == 0) return 1;

    if (a_try(self, a_colon)) return 1;

    if (is_reserved_type_name(type_ident)) return ERROR_STOP;

    // Determine parents vs body
    ast_node_array parents = {.alloc = self->ast_arena};

    // If next is not '{', parse parent trait list
    if (0 != a_try(self, a_open_curly)) {
        // Must be parent list: Name : Parent1[T], Parent2[T] { ... }
        if (a_try(self, a_type_identifier)) return 1;
        array_push(parents, self->result);

        while (0 == a_try(self, a_comma)) {
            if (a_try(self, a_type_identifier)) return ERROR_STOP;
            array_push(parents, self->result);
        }

        // Now expect '{'
        if (a_try(self, a_open_curly)) return ERROR_STOP;
    }

    // Parse signatures inside { ... }
    ast_node_array sigs = {.alloc = self->ast_arena};

    // Try to parse first signature to disambiguate from struct
    if (0 != a_try(self, a_close_curly)) {
        // Body is not empty — try to parse a signature
        if (a_try(self, a_trait_signature)) {
            // Not a trait signature — backtrack so struct parser can try
            if (!parents.size) return 1;
            // With parents but body doesn't parse — error
            return ERROR_STOP;
        }
        array_push(sigs, self->result);

        // Parse remaining signatures
        while (0 != a_try(self, a_close_curly)) {
            if (a_try(self, a_trait_signature)) return ERROR_STOP;
            array_push(sigs, self->result);
        }
    } else {
        // Empty body — only valid with parents (combined trait)
        if (!parents.size) return 1; // Empty body without parents → struct
    }

    // Extract name and type args
    ast_node  *name        = null;
    u8         n_type_args = 0;
    ast_node **type_args   = null;

    if (ast_node_is_symbol(type_ident)) {
        name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        name        = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else return 1;

    // Create trait definition node
    ast_node *r            = ast_node_create(self->ast_arena, ast_trait_definition);
    r->trait_def.name             = name;
    r->trait_def.n_type_arguments = n_type_args;
    r->trait_def.type_arguments   = type_args;

    array_shrink(sigs);
    r->trait_def.n_signatures = sigs.size;
    r->trait_def.signatures   = sigs.v;

    array_shrink(parents);
    r->trait_def.n_parents = parents.size;
    r->trait_def.parents   = parents.v;

    set_node_file(self, r);

    add_module_symbol(self, type_ident);
    mangle_name(self, type_ident);

    return result_ast_node(self, r);
}

static int toplevel_struct(parser *self) {

    if (a_try(self, a_type_identifier)) return 1; // a_type_identifer mangles name
    ast_node *type_ident = self->result;

    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;

    // Check for reserved type keywords before parsing fields
    if (is_reserved_type_name(type_ident)) return ERROR_STOP;

    // Extract parent name and type args for nested struct parsing
    ast_node  *parent_name = null;
    u8         n_type_args = 0;
    ast_node **type_args   = null;

    if (ast_node_is_symbol(type_ident)) {
        parent_name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        parent_name = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else fatal("logic error");

    ast_node_array fields      = {.alloc = self->ast_arena};
    ast_node_array nested_utds = {.alloc = self->ast_arena};
    int            res =
      parse_struct_fields(self, parent_name->symbol.name, n_type_args, type_args, &fields, &nested_utds);
    if (res) return res;

    return finalize_type_definition(self, type_ident, fields, nested_utds, 0);
}

static int toplevel_union(parser *self) {

    if (a_try(self, a_type_identifier)) return 1; // mangles name
    ast_node *type_ident = self->result;

    if (a_try(self, a_colon)) return 1;

    // Format: MyUnion : { | variant1 : Type1 | variant2 : Type 2 }
    if (a_try(self, a_open_curly)) return 1;

    ast_node_array fields = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_vertical_bar)) return 1;
        if (a_try(self, a_param)) return ERROR_STOP;
        array_push(fields, self->result);
    }
    array_shrink(fields);

    ast_node_array no_nested_utds = {0};
    return finalize_type_definition(self, type_ident, fields, no_nested_utds, 1);
}

// Helper to check if an AST node references a type parameter name
static int annotation_uses_type_param(ast_node *node, str param_name) {
    if (!node) return 0;

    if (ast_node_is_symbol(node)) {
        if (str_eq(node->symbol.name, param_name)) return 1;
        // Also check the annotation
        return annotation_uses_type_param(node->symbol.annotation, param_name);
    }

    if (ast_node_is_nfa(node)) {
        // Check the nfa arguments recursively.
        // Since it's possible type arguments are reference in both the explicit type arguments and the
        // value arguments, iterate through both.
        for (u32 i = 0; i < node->named_application.n_arguments; i++) {
            if (annotation_uses_type_param(node->named_application.arguments[i], param_name)) return 1;
        }
        for (u32 i = 0; i < node->named_application.n_type_arguments; i++) {
            if (annotation_uses_type_param(node->named_application.type_arguments[i], param_name)) return 1;
        }
    }

    if (ast_node_is_arrow(node)) {
        if (annotation_uses_type_param(node->arrow.left, param_name)) return 1;
        if (annotation_uses_type_param(node->arrow.right, param_name)) return 1;
        return 0;
    }

    if (ast_node_is_tuple(node)) {
        for (u8 i = 0; i < node->tuple.n_elements; i++) {
            if (annotation_uses_type_param(node->tuple.elements[i], param_name)) return 1;
        }
        return 0;
    }

    return 0;
}

// Helper to collect type params used by a variant's fields
// Returns the number of used type params and fills used_type_args with the used params
static u8 collect_used_type_params(parser *self, u8 n_type_args, ast_node **type_args,
                                   ast_node_array fields, ast_node ***out_used_type_args) {
    if (!n_type_args || !fields.size) {
        *out_used_type_args = null;
        return 0;
    }

    // Track which type params are used
    u8 *used = alloc_malloc(self->ast_arena, n_type_args * sizeof(u8));
    for (u8 i = 0; i < n_type_args; i++) used[i] = 0;

    // Check each field's annotation for type param references
    forall(i, fields) {
        ast_node *ann = fields.v[i]->symbol.annotation;
        for (u8 j = 0; j < n_type_args; j++) {
            if (!used[j] && annotation_uses_type_param(ann, type_args[j]->symbol.name)) {
                used[j] = 1;
            }
        }
    }

    // Count and collect used type params
    u8 count = 0;
    for (u8 i = 0; i < n_type_args; i++) {
        if (used[i]) count++;
    }

    if (count == 0) {
        *out_used_type_args = null;
        return 0;
    }

    ast_node **result = alloc_malloc(self->ast_arena, count * sizeof(ast_node *));
    u8         idx    = 0;
    for (u8 i = 0; i < n_type_args; i++) {
        if (used[i]) {
            result[idx++] = ast_node_clone(self->ast_arena, type_args[i]);
        }
    }

    *out_used_type_args = result;
    return count;
}

// Helper to create a UTD node (struct or union) from name, type args, and fields
static ast_node *create_utd(parser *self, ast_node *name, u8 n_type_args, ast_node **type_args,
                            ast_node_array fields, int is_union) {
    ast_node *r                       = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union         = is_union;
    r->user_type_def.name             = name;
    r->user_type_def.n_type_arguments = n_type_args;
    r->user_type_def.type_arguments   = type_args;
    r->user_type_def.n_fields         = fields.size;
    r->user_type_def.field_types      = null;

    if (fields.size) {
        r->user_type_def.field_names = alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
        r->user_type_def.field_annotations =
          alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
        forall(i, fields) {
            r->user_type_def.field_names[i]       = fields.v[i];
            r->user_type_def.field_annotations[i] = fields.v[i]->symbol.annotation;
        }
    } else {
        r->user_type_def.field_names       = null;
        r->user_type_def.field_annotations = null;
    }

    set_node_file(self, r);
    return r;
}

// Helper to create an enum UTD node
static ast_node *create_enum_utd(parser *self, ast_node *name, ast_node_array idents) {
    ast_node *r                        = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union          = 0;
    r->user_type_def.name              = name;
    r->user_type_def.n_type_arguments  = 0;
    r->user_type_def.type_arguments    = null;
    r->user_type_def.field_types       = null;
    r->user_type_def.n_fields          = idents.size;
    r->user_type_def.field_names       = alloc_malloc(self->ast_arena, idents.size * sizeof(ast_node *));
    r->user_type_def.field_annotations = null;
    forall(i, idents) {
        r->user_type_def.field_names[i] = idents.v[i];
    }
    set_node_file(self, r);
    return r;
}

// Build the wrapping AST for a tagged union variant construction:
//   inner_call -> __Shape__Union_(Circle = inner_call)
//              -> __Shape__Tag_.Circle
//              -> Shape(tag = ..., u = ...)
// The 'module' parameter is used for cross-module mangling; pass str_empty() for same-module.
static ast_node *build_tagged_union_wrapping(parser *self, str tu_name, str var_name, str module,
                                             ast_node *inner_call) {
    allocator *arena = self->ast_arena;

    // Union construction: __Shape__Union_(Circle = innerCall)
    str       union_name_str               = str_cat_3(arena, S("__"), tu_name, S("__Union_"));
    ast_node *union_arg_name               = ast_node_create_sym(arena, var_name);
    ast_node *union_assign                 = ast_node_create_assignment(arena, union_arg_name, inner_call);
    union_assign->assignment.is_field_name = 1;
    set_node_file(self, union_assign);
    ast_node_array union_args = {.alloc = arena};
    array_push(union_args, union_assign);
    array_shrink(union_args);

    ast_node *union_call_name = ast_node_create_sym(arena, union_name_str);
    if (!str_is_empty(module)) mangle_name_for_module(self, union_call_name, module);
    else mangle_name(self, union_call_name);
    ast_node *union_call = ast_node_create_nfa(arena, union_call_name, (ast_node_sized){0},
                                               (ast_node_sized)array_sized(union_args));
    set_node_file(self, union_call);

    // Tag access: __Shape__Tag_.Circle
    str       tag_name_str = str_cat_3(arena, S("__"), tu_name, S("__Tag_"));
    ast_node *tag_type     = ast_node_create_sym(arena, tag_name_str);
    if (!str_is_empty(module)) mangle_name_for_module(self, tag_type, module);
    else mangle_name(self, tag_type);
    ast_node *dot_op      = ast_node_create_sym_c(arena, ".");
    ast_node *tag_variant = ast_node_create_sym(arena, var_name);
    ast_node *tag_access  = ast_node_create_binary_op(arena, dot_op, tag_type, tag_variant);
    set_node_file(self, tag_access);

    // Wrapper construction: Shape(tag = tagAccess, u = unionCall)
    ast_node *tag_arg_name               = ast_node_create_sym_c(arena, AST_TAGGED_UNION_TAG_FIELD);
    ast_node *tag_assign                 = ast_node_create_assignment(arena, tag_arg_name, tag_access);
    tag_assign->assignment.is_field_name = 1;
    set_node_file(self, tag_assign);

    ast_node *u_arg_name               = ast_node_create_sym_c(arena, AST_TAGGED_UNION_UNION_FIELD);
    ast_node *u_assign                 = ast_node_create_assignment(arena, u_arg_name, union_call);
    u_assign->assignment.is_field_name = 1;
    set_node_file(self, u_assign);

    ast_node_array wrapper_args = {.alloc = arena};
    array_push(wrapper_args, tag_assign);
    array_push(wrapper_args, u_assign);
    array_shrink(wrapper_args);

    ast_node *wrapper_call_name = ast_node_create_sym(arena, tu_name);
    if (!str_is_empty(module)) mangle_name_for_module(self, wrapper_call_name, module);
    else mangle_name(self, wrapper_call_name);
    ast_node *wrapper_call = ast_node_create_nfa(arena, wrapper_call_name, (ast_node_sized){0},
                                                 (ast_node_sized)array_sized(wrapper_args));
    set_node_file(self, wrapper_call);

    return wrapper_call;
}

// Helper to create a constructor function for a tagged union variant
// E.g., Shape_Circle(radius: Float) -> Shape { ... }
// Note: Type parameters are inferred during type checking, not passed explicitly.
static ast_node *create_variant_constructor(parser *self,
                                            str     tu_name_str,  // e.g., "Shape"
                                            str     var_name_str, // e.g., "Circle"
                                            u8 n_type_args, // number of type params (unused, for future)
                                            ast_node **type_args, // type param nodes (unused, for future)
                                            ast_node_array var_fields) // variant fields
{
    allocator     *arena       = self->ast_arena;
    ast_node_sized type_params = {.size = n_type_args, .v = type_args};

    // 1. Create function name: unscoped at module level (e.g., "Circle")
    str       func_name_str = var_name_str;
    ast_node *func_name     = ast_node_create_sym(arena, func_name_str);

    // 2. Clone variant fields as function parameters
    ast_node_array params = {.alloc = arena};
    forall(i, var_fields) {
        ast_node *field = var_fields.v[i];
        ast_node *param = ast_node_create_sym(arena, field->symbol.name);
        if (field->symbol.annotation) {
            param->symbol.annotation = ast_node_clone(arena, field->symbol.annotation);
        }
        array_push(params, param);
    }
    array_shrink(params);

    // 3. Build the return type annotation
    // For non-generic: Shape
    // For generic: Shape[T]
    ast_node *return_type = null;
    if (n_type_args) {
        ast_node_sized args = {.size = n_type_args,
                               .v    = alloc_malloc(arena, n_type_args * sizeof(ast_node *))};
        for (u8 i = 0; i < n_type_args; i++) {
            args.v[i] = ast_node_clone(arena, type_args[i]);
        }
        ast_node *wrapper_name = ast_node_create_sym(arena, tu_name_str);
        mangle_name(self, wrapper_name);
        // TYPE ANNOTATION NFA: Shape[T] — type params in type_args slot.
        return_type = ast_node_create_nfa(arena, wrapper_name, args, (ast_node_sized){0});
    } else {
        return_type = ast_node_create_sym(arena, tu_name_str);
        mangle_name(self, return_type);
    }

    // 4. Build the arrow annotation for function type: (params) -> ReturnType
    ast_node *param_tuple = ast_node_create_tuple(arena, (ast_node_sized)array_sized(params));
    set_node_file(self, param_tuple);
    ast_node *arrow = ast_node_create_arrow(arena, param_tuple, return_type, type_params);
    set_node_file(self, arrow);
    func_name->symbol.annotation = arrow;

    // 5. Build the function body
    // Inner variant construction: Circle(radius = radius)
    ast_node_array inner_args = {.alloc = arena};
    forall(i, var_fields) {
        ast_node *field                      = var_fields.v[i];
        ast_node *arg_name                   = ast_node_create_sym(arena, field->symbol.name);
        ast_node *arg_val                    = ast_node_create_sym(arena, field->symbol.name);
        ast_node *arg_assign                 = ast_node_create_assignment(arena, arg_name, arg_val);
        arg_assign->assignment.is_field_name = 1; // Mark as field name to prevent renaming
        set_node_file(self, arg_assign);
        array_push(inner_args, arg_assign);
    }
    array_shrink(inner_args);

    // Inner call constructs the scoped variant struct (e.g., Shape__Circle)
    str       var_struct_str  = str_cat_3(arena, tu_name_str, S("__"), var_name_str);
    ast_node *inner_call_name = ast_node_create_sym(arena, var_struct_str);
    mangle_name(self, inner_call_name);
    // VALUE CONSTRUCTION NFA: Shape__Circle(radius = radius) — field assignments are value args.
    ast_node *inner_call = ast_node_create_nfa(arena, inner_call_name, (ast_node_sized){0},
                                               (ast_node_sized)array_sized(inner_args));
    set_node_file(self, inner_call);

    // Wrap inner_call in union + tag + wrapper struct using shared helper
    ast_node *wrapper_call =
      build_tagged_union_wrapping(self, tu_name_str, var_name_str, str_empty(), inner_call);

    // Create body with just the wrapper call
    ast_node_array body_exprs = {.alloc = arena};
    array_push(body_exprs, wrapper_call);
    array_shrink(body_exprs);
    ast_node *body = ast_node_create_body(arena, (ast_node_sized)array_sized(body_exprs));
    set_node_file(self, body);

    // Create the function (let) node
    add_module_symbol(self, func_name);
    mangle_name(self, func_name);
    ast_node *let =
      ast_node_create_let(arena, func_name, (ast_node_sized){0}, (ast_node_sized)array_sized(params), body);
    set_node_parameters(self, let, &params);
    let->let.name = func_name;
    let->let.body = body;
    set_node_file(self, let);

    return let;
}

// Tagged union syntax:
//   Shape = | Circle { radius: Float }
//           | Square { length: Float }
//           | Rectangle { length: Float, height: Float }
//
// Or with generics:
//   Option[T] : | Some { value: T }
//               | None
//
// Desugars to:
//   _ShapeTag_   : { Circle, Square, Rectangle }
//   Circle       : { radius: Float }
//   Square       : { length: Float }
//   Rectangle    : { length: Float, height: Float }
//   _ShapeUnion_ : { | Circle: Circle | Square: Square | Rectangle: Rectangle }
//   Shape        : { tag: _ShapeTag_, u: _ShapeUnion_ }
//
// Plus constructor functions:
//   Shape_Circle(radius: Float) -> Shape { ... }
//   Shape_Square(length: Float) -> Shape { ... }
//   Shape_Rectangle(length: Float, height: Float) -> Shape { ... }
//
static int toplevel_tagged_union(parser *self) {
    // Parse type name (possibly with type parameters)
    if (a_try(self, a_type_identifier)) return 1;
    ast_node *type_ident = self->result;
    unmangle_name(self, type_ident);

    // Parse ':'
    if (a_try(self, a_colon)) return 1;

    // Parse first '|'
    if (a_try(self, a_vertical_bar)) return 1;

    // Extract type name and type arguments.
    // a_type_identifier -> a_funcall parses e.g. Option[T] with type params in .type_arguments.
    ast_node  *tu_name     = null;
    u8         n_type_args = 0;
    ast_node **type_args   = null;

    if (ast_node_is_symbol(type_ident)) {
        tu_name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        tu_name     = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else {
        return 1;
    }

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(tu_name)) return ERROR_STOP;

    str tu_name_str = tu_name->symbol.name;
    str_hset_insert(&self->nested_type_parents, tu_name_str);
    str_hset_insert(&self->tagged_union_variant_parents, tu_name_str);

    // Collect variants: { name, fields[] }
    typedef struct {
        ast_node      *name;
        ast_node_array fields;
    } variant;

    // Must match array_t layout: { v, alloc, size, capacity }
    struct {
        variant   *v;
        allocator *alloc;
        u32        size;
        u32        cap;
    } variants = {.v = null, .alloc = self->ast_arena};

    while (1) {
        // Parse variant name
        if (a_try(self, a_identifier)) return ERROR_STOP;
        ast_node *var_name = self->result;

        // Existing-type variant syntax (Module.Type) is no longer supported
        if (0 == a_try(self, a_dot)) {
            self->error.tag = tl_err_expected_expression;
            return ERROR_STOP;
        }

        // Parse optional struct body { field: Type, ... }
        ast_node_array fields = {.alloc = self->ast_arena};
        if (0 == a_try(self, a_open_curly)) {
            while (1) {
                int saw_comma = 0;
                if (0 == a_try(self, a_comma)) saw_comma = 1;
                if (0 == a_try(self, a_close_curly)) break;
                if (!saw_comma && fields.size) {
                    if (a_try(self, a_comma)) return ERROR_STOP;
                }
                if (a_try(self, a_param)) return ERROR_STOP;
                array_push(fields, self->result);
            }
        }
        array_shrink(fields);

        // Check for reserved type keywords to disallow
        if (is_reserved_type_name(var_name)) return ERROR_STOP;

        variant v = {.name = var_name, .fields = fields};
        array_push(variants, v);

        // Check for next variant or end
        if (a_try(self, a_vertical_bar)) break; // no more variants
    }
    array_shrink(variants);

    if (!variants.size) return 1;

    // Generate all the desugared type definitions
    ast_node_array result_nodes = {.alloc = self->ast_arena};

    // 1. Tag enum: __Shape__Tag_ : { Circle, Square, Rectangle }
    {
        str            tag_name_str = str_cat_3(self->ast_arena, S("__"), tu_name_str, S("__Tag_"));
        ast_node      *tag_name     = ast_node_create_sym(self->ast_arena, tag_name_str);

        ast_node_array tag_idents   = {.alloc = self->ast_arena};
        forall(i, variants) {
            ast_node *ident = ast_node_create_sym(self->ast_arena, variants.v[i].name->symbol.name);
            array_push(tag_idents, ident);
        }
        array_shrink(tag_idents);

        ast_node *tag_enum                        = create_enum_utd(self, tag_name, tag_idents);
        tag_enum->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, tag_name);
        mangle_name(self, tag_name);
        array_push(result_nodes, tag_enum);
    }

    // 2. Variant structs: Shape__Circle : { radius: Float }, etc.
    //    Scoped under tagged union type, accessed as Shape.Circle via nested_type_parents.
    forall(i, variants) {
        variant  *v            = &variants.v[i];

        str       var_name_str = str_cat_3(self->ast_arena, tu_name_str, S("__"), v->name->symbol.name);
        ast_node *var_name     = ast_node_create_sym(self->ast_arena, var_name_str);

        // For generics, determine which type params are actually used by this variant's fields
        ast_node **var_type_args = null;
        u8         var_n_type_args =
          collect_used_type_params(self, n_type_args, type_args, v->fields, &var_type_args);

        ast_node *var_struct = create_utd(self, var_name, var_n_type_args, var_type_args, v->fields, 0);
        var_struct->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, var_name);
        mangle_name(self, var_name);
        array_push(result_nodes, var_struct);
    }

    // 3. Union type: __Shape__Union_ : { | Circle: Circle | Square: Square | ... }
    {
        str       union_name_str = str_cat_3(self->ast_arena, S("__"), tu_name_str, S("__Union_"));
        ast_node *union_name     = ast_node_create_sym(self->ast_arena, union_name_str);

        // Build union fields: each field name is the variant name, annotation is the variant type
        ast_node_array union_fields = {.alloc = self->ast_arena};
        forall(i, variants) {
            variant *v = &variants.v[i];

            // Field name (e.g., "Circle")
            ast_node *field_name = ast_node_create_sym(self->ast_arena, v->name->symbol.name);

            // Field annotation: the variant type (may be generic)
            ast_node **used_type_args = null;
            u8         n_used_type_args =
              collect_used_type_params(self, n_type_args, type_args, v->fields, &used_type_args);

            str var_struct_name = str_cat_3(self->ast_arena, tu_name_str, S("__"), v->name->symbol.name);

            ast_node *field_ann = null;
            if (n_used_type_args) {
                // Generic variant with used type params
                ast_node_sized args          = {.size = n_used_type_args, .v = used_type_args};
                ast_node      *var_type_name = ast_node_create_sym(self->ast_arena, var_struct_name);
                mangle_name(self, var_type_name);
                // TYPE ANNOTATION NFA: e.g. Shape__Circle[a] — type params in type_args slot.
                field_ann = ast_node_create_nfa(self->ast_arena, var_type_name, args, (ast_node_sized){0});
            } else {
                // Non-generic variant (no fields or no type params used)
                field_ann = ast_node_create_sym(self->ast_arena, var_struct_name);
                mangle_name(self, field_ann);
            }

            field_name->symbol.annotation = field_ann;
            array_push(union_fields, field_name);
        }
        array_shrink(union_fields);

        // Create the union type with all parent type params
        ast_node **union_type_args = null;
        if (n_type_args) {
            union_type_args = alloc_malloc(self->ast_arena, n_type_args * sizeof(ast_node *));
            for (u8 i = 0; i < n_type_args; i++) {
                union_type_args[i] = ast_node_clone(self->ast_arena, type_args[i]);
            }
        }

        ast_node *union_utd = create_utd(self, union_name, n_type_args, union_type_args, union_fields, 1);
        union_utd->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, union_name);
        mangle_name(self, union_name);
        array_push(result_nodes, union_utd);
    }

    // 4. Wrapper struct: Shape : { tag: _ShapeTag_, u: _ShapeUnion_ }
    {
        ast_node *wrapper_name = ast_node_create_sym(self->ast_arena, tu_name_str);

        // Build wrapper fields: tag and u
        ast_node_array wrapper_fields = {.alloc = self->ast_arena};

        // Field: tag: __Shape__Tag_
        {
            ast_node *tag_field    = ast_node_create_sym_c(self->ast_arena, AST_TAGGED_UNION_TAG_FIELD);
            str       tag_type_str = str_cat_3(self->ast_arena, S("__"), tu_name_str, S("__Tag_"));
            ast_node *tag_ann      = ast_node_create_sym(self->ast_arena, tag_type_str);
            mangle_name(self, tag_ann);
            tag_field->symbol.annotation = tag_ann;
            array_push(wrapper_fields, tag_field);
        }

        // Field: u: __Shape__Union_ (or __Shape__Union_[T] for generics)
        {
            ast_node *u_field        = ast_node_create_sym_c(self->ast_arena, AST_TAGGED_UNION_UNION_FIELD);
            str       union_type_str = str_cat_3(self->ast_arena, S("__"), tu_name_str, S("__Union_"));

            ast_node *u_ann          = null;
            if (n_type_args) {
                ast_node_sized args = {.size = n_type_args,
                                       .v =
                                         alloc_malloc(self->ast_arena, n_type_args * sizeof(ast_node *))};
                forall(j, args) {
                    args.v[j] = ast_node_clone(self->ast_arena, type_args[j]);
                }
                ast_node *union_type_name = ast_node_create_sym(self->ast_arena, union_type_str);
                mangle_name(self, union_type_name);
                // TYPE ANNOTATION NFA: __Shape__Union_[T] — type params in type_args slot.
                u_ann = ast_node_create_nfa(self->ast_arena, union_type_name, args, (ast_node_sized){0});
            } else {
                u_ann = ast_node_create_sym(self->ast_arena, union_type_str);
                mangle_name(self, u_ann);
            }

            u_field->symbol.annotation = u_ann;
            array_push(wrapper_fields, u_field);
        }
        array_shrink(wrapper_fields);

        // Create wrapper with all type params
        ast_node **wrapper_type_args = null;
        if (n_type_args) {
            wrapper_type_args = alloc_malloc(self->ast_arena, n_type_args * sizeof(ast_node *));
            for (u8 i = 0; i < n_type_args; i++) {
                wrapper_type_args[i] = ast_node_clone(self->ast_arena, type_args[i]);
            }
        }

        ast_node *wrapper_utd =
          create_utd(self, wrapper_name, n_type_args, wrapper_type_args, wrapper_fields, 0);
        wrapper_utd->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, wrapper_name);
        mangle_name(self, wrapper_name);
        array_push(result_nodes, wrapper_utd);
    }

    // 5. Constructor functions for each variant
    forall(i, variants) {
        variant  *v    = &variants.v[i];

        ast_node *ctor = create_variant_constructor(self, tu_name_str, v->name->symbol.name, n_type_args,
                                                    type_args, v->fields);
        array_push(result_nodes, ctor);

        // Record nullary variants for auto-invocation (bare B or cross-module Opt.Empty)
        if (v->fields.size == 0) {
            str key = !str_is_empty(self->current_module)
                        ? str_copy(self->parent_alloc,
                                   mangle_str_for_module(self, v->name->symbol.name, self->current_module))
                        : str_copy(self->parent_alloc, v->name->symbol.name);
            str_map_set(&self->nullary_variant_parents, key, &tu_name_str);
        }
    }

    array_shrink(result_nodes);

    // Return as a body containing all the generated UTDs and constructor functions
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)array_sized(result_nodes));
    set_node_file(self, body);
    return result_ast_node(self, body);
}

static int toplevel(parser *self) {

    self->error.tag = tl_err_ok;

    // Switch to speculative arena for all allocations during pattern matching
    allocator *permanent = self->ast_arena;
    self->ast_arena      = self->speculative;

    while (!is_eof(self)) {

        int res = 0;

        if (0 == a_try(self, toplevel_c_chunk)) goto success_hash;
        if (0 == (res = a_try(self, toplevel_hash))) goto success_hash;
        else if (ERROR_STOP == res) goto error;
        if (0 == a_try(self, toplevel_type_alias)) goto success;

        // Tagged union must come before enum/struct since both start with identifier
        if (0 == (res = a_try(self, toplevel_tagged_union))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_enum))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_trait))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_struct))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_union))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_defun))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_assign))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_forward))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_symbol_annotation))) goto success;
        else if (ERROR_STOP == res) goto error;

        self->error.tag = tl_err_expected_toplevel;
        // Fall through to cleanup_fail

    cleanup_fail:
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return 1;

    error:
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return res;

    success:
        if (self->expect_module) {
            self->error.tag = tl_err_expected_module;
            goto cleanup_fail;
        }

    success_hash:
        if (!self->skip_module) {
            // Clone successful result to permanent arena
            self->result = ast_node_clone(permanent, self->result);
            arena_reset(self->speculative);
            self->ast_arena = permanent;
            return 0;
        }
        // skip_module: continue loop, reset speculative arena for next iteration
        arena_reset(self->speculative);
    }

    // EOF reached
    arena_reset(self->speculative);
    self->ast_arena = permanent;
    return 1;
}

static int toplevel_funcall_only(parser *self) {

    self->error.tag = tl_err_ok;

    // Switch to speculative arena for all allocations during pattern matching
    allocator *permanent = self->ast_arena;
    self->ast_arena      = self->speculative;

    while (!is_eof(self)) {

        int res = 0;

        if (0 == (res = a_try(self, a_funcall))) goto success;
        else if (ERROR_STOP == res) goto error;

        self->error.tag = tl_err_expected_funcall;

    error:
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return res;

    success:
        // Clone successful result to permanent arena before resetting speculative
        self->result = ast_node_clone(permanent, self->result);
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return 0;
    }

    // EOF reached
    arena_reset(self->speculative);
    self->ast_arena = permanent;
    return 1;
}

int parser_next(parser *self) {
    while (1) {
        if (!self->tokenizer) {

            // A new tokenizer is created for each file being parsed.
            tokenizer_opts tok_opts   = {.defines = self->opts.defines};

            self->error.tag           = tl_err_ok;
            self->tokenizer_error.tag = tl_err_ok;

            // Parse the prelude string before any files.
            if (self->opts.prelude && !self->prelude_consumed) {
                self->prelude_consumed = 1;
                char_csized data       = {.v = self->opts.prelude, .size = strlen(self->opts.prelude)};

                tok_opts.input         = data;
                tok_opts.file          = "<prelude>";
                self->tokenizer        = tokenizer_create(self->parent_alloc, &tok_opts);
            } else {
                if ((i32)self->files_index >= (i32)self->files.size) {
                    self->error.tag = tl_err_eof;
                    return 1;
                }

                // free prior data
                if (self->current_file_data.v) {
                    alloc_free(self->file_arena, (void *)self->current_file_data.v);
                    self->current_file_data.v    = null;
                    self->current_file_data.size = 0;
                }

                // read file — normalize backslashes to forward slashes so that
                // #line directives don't contain sequences like \U that C
                // compilers interpret as UCN escapes.
                str file_str = self->files.v[self->files_index++];
#ifdef MOS_WINDOWS
                file_str = str_replace_char(self->parent_alloc, file_str, '\\', '/');
#endif
                char const *file = str_cstr(&file_str);
                file_read(self->file_arena, file, (char **)&self->current_file_data.v,
                          &self->current_file_data.size);

                tok_opts.input      = self->current_file_data;
                tok_opts.file       = file;
                self->tokenizer     = tokenizer_create(self->parent_alloc, &tok_opts);
                self->expect_module = 1;
            }
        }

        int res = 0;
        if (mode_toplevel_funcall == self->mode) {
            res = toplevel_funcall_only(self);
        } else {
            res = toplevel(self);
        }

        if (0 == res) {
            self->result->file = self->error.file;
            self->result->line = self->error.line;
            self->result->col  = self->error.col;
            arena_reset(self->transient);
            return res;
        } else if (is_eof(self)) {
            if (self->tokenizer) tokenizer_destroy(&self->tokenizer);
            self->tokenizer   = null;
            self->tokens.size = 0;
            map_reset(self->module_aliases);

            // keep going with next file if possible
        } else {
            arena_reset(self->transient);
            return res;
        }
    }
}

int parser_parse_all(parser *self, ast_node_array *out) {

    self->mode = mode_source;

    int res    = 0;
    while (0 == (res = parser_next(self))) {
        ast_node *node;

        parser_result(self, &node);
        str str = v2_ast_node_to_string(self->transient, node);
        dbg(self, "parse_all: parsed node %s", str_cstr(&str));

        array_push(*out, node);
    }

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_toplevel_funcalls(parser *self, ast_node_array *out) {
    self->mode = mode_toplevel_funcall;

    int res    = 0;
    while (0 == (res = parser_next(self))) {
        ast_node *node;

        parser_result(self, &node);
        array_push(*out, node);
    }

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_symbols(parser *self) {
    int res    = 0;
    self->mode = mode_symbols;

    while (0 == (res = parser_next(self))) {
        ast_node *node;
        parser_result(self, &node);
        str str = v2_ast_node_to_string(self->transient, node);
        dbg(self, "parse_all_symbols: parsed node %s", str_cstr(&str));
    }

    save_current_module_symbols(self);

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_verbose(parser *p, ast_node_array *out) {
    p->verbose = 1;
    p->mode    = mode_source;

    dbg(p, "begin parse");
    int res = parser_parse_all(p, out);
    dbg(p, "end parse status %i", res);

    p->verbose = 0;
    return res;
}

void parser_result(parser *p, ast_node **handle) {
    if (handle) {
        *handle = p->result;
    }
}

static void tokens_push_back(struct parser *p, struct token *tok) {
    array_push(p->tokens, *tok);
}

static void tokens_shrink(struct parser *p, u32 n) {
    p->tokens.size = n;
}

//

static str next_var_name(parser *self) {
    char buf[64];
    int  len = snprintf(buf, sizeof buf, "parser_var_%u", self->next_var_name++);
    return str_init_n(self->transient, buf, len);
}

//

void parser_report_errors(parser *self) {
    if (tl_err_ok == self->error.tag) return;

    fprintf(stderr, "%s:%u:%u: syntax error: %s\n", self->error.file, self->error.line, self->error.col,
            tl_error_tag_to_string(self->error.tag));
}
void parser_set_verbose(parser *self, int val) {
    self->verbose = val;
}

static int too_many_arguments(parser *self) {
    self->error.tag = tl_err_too_many_arguments;
    return 1;
}

void dbg(struct parser *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "parser: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
