#include "parser_internal.h"

#include "file.h"
#include "infer.h"
#include "platform.h"

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSER_ARENA_SIZE 1024

typedef int (*parse_fun_s)(parser *, char const *);
typedef int (*parse_fun_int)(parser *, int);

int annotation_uses_type_param(ast_node *node, str param_name);

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
    self->modules_version_seen         = hset_create(self->parent_alloc, 32);
    self->module_preludes_seen         = hset_create(self->parent_alloc, 32);
    self->nested_type_parents          = hset_create(self->parent_alloc, 1024);
    self->tagged_union_variant_parents = hset_create(self->parent_alloc, 256);
    self->module_aliases               = map_new(self->parent_alloc, str, str, 32);
    self->nullary_variant_parents      = map_new(self->parent_alloc, str, str, 16);
    self->module_pkg_prefixes          = opts->module_pkg_prefixes;
    self->file_pkg_prefixes            = opts->file_pkg_prefixes;
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
    hset_destroy(&(*self)->modules_version_seen);
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

void add_module_symbol(parser *self, ast_node *name) {
    // Keeps names before they are mangled with the module name prefix, but after they are mangled with
    // arity.
    if (ast_node_is_symbol(name)) {
        // For safety, don't add symbols which have already been mangled.
        if (name->symbol.is_module_mangled) return;

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

void set_node_file(parser *self, ast_node *node) {
    node->file = self->token.file;
    node->line = self->token.line;
    node->col  = self->token.col;
}

static void set_result_file(parser *self) {
    set_node_file(self, self->result);
}

int result_ast(parser *p, ast_tag tag) {
    p->result = ast_node_create(p->ast_arena, tag);
    set_result_file(p);
    return 0;
}

int result_ast_i64(parser *p, i64 val) {
    p->result = ast_node_create_i64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_u64(parser *p, u64 val) {
    p->result = ast_node_create_u64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_i64_z(parser *p, i64 val) {
    p->result = ast_node_create_i64_z(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_u64_zu(parser *p, u64 val) {
    p->result = ast_node_create_u64_zu(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_f64(parser *p, f64 val) {
    p->result = ast_node_create_f64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_bool(parser *p, int val) {
    p->result = ast_node_create_bool(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_str(parser *p, ast_tag tag, char const *s) {
    p->result      = ast_node_create_sym_c(p->ast_arena, s);
    p->result->tag = tag;
    set_result_file(p);
    return 0;
}

int result_ast_str_(parser *p, ast_tag tag, str s) {
    p->result      = ast_node_create_sym(p->ast_arena, s);
    p->result->tag = tag;
    set_result_file(p);
    return 0;
}

int result_ast_node(parser *p, ast_node *node) {
    p->result = node;
    set_result_file(p);
    return 0;
}

int is_reserved(char const *s) {
    static char const *strings[] = {
      "break",  "case", "continue", "defer", "else", "false", "if",    "in", "null",
      "return", "then", "true",     "try",   "void", "when",  "while", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

int is_reserved_type_keyword(char const *s) {
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

int is_reserved_type_name(ast_node const *name) {
    // Check for reserved type keywords to disallow
    if (!ast_node_is_symbol(name)) return 0;
    str word = ast_node_str(name);
    if (is_reserved_type_keyword(str_cstr(&word))) return 1;
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

int is_unary_operator(char const *s) {
    static char const *strings[] = {"!", "~", "+", "-", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_eof(parser *p) {
    return p->error.tag == tl_err_eof || p->tokenizer_error.tag == tl_err_eof;
}

int eat_comments(parser *p) {
    while (1) {
        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            parser_dbg(p, "tokenizer error: %s", tl_error_tag_to_string(p->tokenizer_error.tag));
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

int next_token(parser *p) {
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

nodiscard int a_try(parser *p, parse_fun fun) {
    int       result    = 0;
    u32 const save_toks = p->tokens.size;

    if ((result = fun(p))) {
        assert(p->tokens.size >= save_toks);
        if (p->tokens.size > save_toks) {
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

nodiscard int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
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

nodiscard int a_try_int(parser *p, parse_fun_int fun, int arg) {
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

// primitives

int a_char(parser *p) {
    if (next_token(p)) return 1;
    if (tok_char == p->token.tag) return result_ast_str(p, ast_char, p->token.s);
    p->error.tag = tl_err_expected_comma;
    return 1;
}

int a_comma(parser *p) {
    if (next_token(p)) return 1;
    if (tok_comma == p->token.tag) return result_ast_str(p, ast_symbol, ",");
    p->error.tag = tl_err_expected_comma;
    return 1;
}

int a_dot(parser *p) {
    if (next_token(p)) return 1;
    if (tok_dot == p->token.tag) return result_ast_str(p, ast_symbol, ".");
    p->error.tag = tl_err_expected_dot;
    return 1;
}

int a_ellipsis(parser *p) {
    if (next_token(p)) return 1;
    if (tok_ellipsis == p->token.tag) return result_ast_str(p, ast_symbol, "...");
    p->error.tag = tl_err_expected_ellipsis;
    return 1;
}

int a_vertical_bar(parser *p) {
    if (next_token(p)) return 1;
    if (tok_bar == p->token.tag) return result_ast_str(p, ast_symbol, "|");
    p->error.tag = tl_err_expected_vertical_bar;
    return 1;
}

int a_hash_command(parser *p) {
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

int a_c_block(parser *p) {
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

int a_open_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_round == p->token.tag) return result_ast_str(p, ast_symbol, "(");
    p->error.tag = tl_err_expected_open_round;
    return 1;
}

int a_open_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_square == p->token.tag) return result_ast_str(p, ast_symbol, "[");
    p->error.tag = tl_err_expected_open_square;
    return 1;
}

int a_close_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_round == p->token.tag) return result_ast_str(p, ast_symbol, ")");
    p->error.tag = tl_err_expected_close_round;
    return 1;
}

int a_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]");
    p->error.tag = tl_err_expected_close_square;
    return 1;
}

int a_open_curly(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_curly == p->token.tag) return result_ast_str(p, ast_symbol, "{");
    p->error.tag = tl_err_expected_open_curly;
    return 1;
}

int a_close_curly(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_curly == p->token.tag) return result_ast_str(p, ast_symbol, "}");
    p->error.tag = tl_err_expected_close_curly;
    return 1;
}

int        unmangle_arity(str name);
static str unmangle_arity_qualified_name(allocator *alloc, str name);

int        identifier_base(parser *p, str *name) {
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

int a_identifier(parser *p) {
    int res = 0;
    str name;
    if ((res = identifier_base(p, &name))) return res;

    result_ast_str_(p, ast_symbol, name);
    return 0;
}

int a_identifier_optional_arity(parser *p) {
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

int a_attributed_identifier(parser *self) {
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

int the_symbol(parser *p, char const *const want) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp(want, p->token.s)) return result_ast_str(p, ast_symbol, p->token.s);
    }

    p->error.tag = tl_err_expected_specific_symbol;
    return 1;
}

int a_string(parser *p) {
    if (next_token(p)) return 1;

    if (tok_string == p->token.tag) return result_ast_str(p, ast_string, p->token.s);

    p->error.tag = tl_err_expected_string;
    return 1;
}

int string_to_number(parser *parser, char const *const in) {
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

int a_number(parser *self) {
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

int a_bool(parser *p) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp("true", p->token.s)) return result_ast_bool(p, 1);
        if (0 == strcmp("false", p->token.s)) return result_ast_bool(p, 0);
    }

    p->error.tag = tl_err_expected_bool;
    return 1;
}

int a_equal_sign(parser *p) {
    if (next_token(p)) return 1;

    if (tok_equal_sign == p->token.tag) return result_ast_str(p, ast_symbol, "=");

    p->error.tag = tl_err_expected_equal_sign;
    return 1;
}

int a_colon(parser *p) {
    if (next_token(p)) return 1;
    if (tok_colon == p->token.tag) return result_ast_str(p, ast_symbol, ":");
    p->error.tag = tl_err_expected_colon;
    return 1;
}

int a_semicolon(parser *p) {
    if (next_token(p)) return 1;
    if (tok_semicolon == p->token.tag) return result_ast_str(p, ast_symbol, ";");
    p->error.tag = tl_err_expected_semicolon;
    return 1;
}

int a_double_open_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_double_open_square == p->token.tag) return result_ast_str(p, ast_symbol, "[[");
    p->error.tag = tl_err_expected_double_open_square;
    return 1;
}

int a_double_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_double_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]]");
    p->error.tag = tl_err_expected_double_close_square;
    return 1;
}

int a_star(parser *p) {
    if (next_token(p)) return 1;
    if (tok_star == p->token.tag) return result_ast_str(p, ast_symbol, "*");
    p->error.tag = tl_err_expected_star;
    return 1;
}

int is_ampersand(ast_node const *node) {
    return (ast_node_is_symbol(node) && str_eq(ast_node_str(node), S("&")));
}

int a_ampersand(parser *p) {
    if (next_token(p)) return 1;
    if (tok_ampersand == p->token.tag) return result_ast_str(p, ast_symbol, "&");
    p->error.tag = tl_err_expected_ampersand;
    return 1;
}

int a_colon_equal(parser *p) {
    if (next_token(p)) return 1;

    if (tok_colon_equal == p->token.tag) return result_ast_str(p, ast_symbol, ":=");

    p->error.tag = tl_err_expected_colon_equal;
    return 1;
}

int a_arrow(parser *p) {
    if (next_token(p)) return 1;

    if (tok_arrow == p->token.tag) return result_ast_str(p, ast_symbol, "->");

    p->error.tag = tl_err_expected_arrow;
    return 1;
}

int a_nil(parser *self) {

    if (0 == a_try_s(self, the_symbol, "void")) return result_ast(self, ast_void);
    if ((0 == a_open_round(self)) && (0 == a_close_round(self))) return result_ast(self, ast_void);

    self->error.tag = tl_err_expected_nil;
    return 1;
}

int a_null(parser *self) {
    if (0 == the_symbol(self, "null")) return result_ast(self, ast_nil);
    self->error.tag = tl_err_expected_nil;
    return 1;
}

int set_node_parameters(parser *self, ast_node *node, ast_node_array *parameters) {
    // given parsed parameters for a function or lambda, initialize
    // the node array properly

    array_shrink(*parameters);
    node->array.nodes = parameters->v;
    if (parameters->size > 0xff) return too_many_arguments(self);

    node->array.n = (u8)parameters->size;
    return 0;
}

int a_type_identifier_base(parser *self);

int maybe_mangle_binop(parser *self, ast_node *op, ast_node **inout, ast_node *right);

int a_type_identifier_base(parser *self) {
    // Callers expect name to be mangled.
    if (0 == a_try(self, a_type_arrow)) return 0;

    if (0 == a_try(self, a_attributed_identifier)) {
        ast_node *ident = self->result;

        // Iteratively resolve dotted names left-to-right (e.g., CommandLine.Args.Args).
        // This mirrors how parse_expression handles module-qualified names via
        // maybe_mangle_binop in a while loop.
        while (0 == a_try(self, a_dot)) {
            ast_node *op = self->result;

            if (a_try(self, a_attributed_identifier)) return 1;
            ast_node *right = self->result;

            if (maybe_mangle_binop(self, op, &ident, right)) {
                continue; // combined module or resolved member — check for more dots
            } else {
                mangle_name(self, right);
                return result_ast_node(self, right);
            }
        }

        // No (more) dots — parse optional type arguments.
        ast_node_array type_args;
        if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

        mangle_name(self, ident);

        maybe_mangle_implicit_submodule(self, ident);

        if (type_args.size) {
            ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                              (ast_node_sized){0});
            return result_ast_node(self, r);
        } else {
            return result_ast_node(self, ident);
        }
    }

    return 1;
}

int a_type_identifier(parser *self) {
    if (0 == a_try(self, a_ellipsis)) return 0;
    return a_type_identifier_base(self);
}

int a_type_annotation(parser *self) {
    if (0 == a_try(self, a_colon)) {
        int res = a_try(self, a_type_identifier);
        return res;
    }

    self->error.tag = tl_err_expected_type;
    return 1;
}

int a_param(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *ident = self->result;

    // If followed by type arguments (e.g., Ptr[K] in a function pointer type),
    // parse as a generic type application rather than a named parameter.
    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

    if (type_args.size) {
        mangle_name(self, ident);
        ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                          (ast_node_sized){0});
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

int unmangle_arity(str name) {
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

void unmangle_name(parser *self, ast_node *name) {
    (void)self;
    if (ast_node_is_nfa(name)) {
        unmangle_name(self, name->named_application.name);
        return;
    }
    if (!ast_node_is_symbol(name)) return;
    if (!name->symbol.is_module_mangled) return;
    if (str_is_empty(name->symbol.original)) return;
    name->symbol.name              = name->symbol.original;
    name->symbol.is_module_mangled = 0;
    str_deinit(self->ast_arena, &name->symbol.module);
}

// Check if a symbol is a known module-level function (not a type).
// Returns true only for symbols that exist in module_symbols (with arity mangling) and are not types.
// Used for arity mangling: only mangle calls to known module functions, not local variables.
int symbol_is_module_function(parser *self, ast_node *name, u8 arity) {
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
    hashmap *module_syms = resolve_module_symbols(self, module_name);
    if (!module_syms) return 0;

    // Check if arity-mangled symbol exists in that module
    return str_hset_contains(module_syms, mangled_name);
}

str mangle_str_for_module(parser *self, str name, str module) {
    str safe_module = str_replace_char_str(self->ast_arena, module, '.', S("__"));
    str result      = str_qualify(self->ast_arena, safe_module, name);

    if (self->module_pkg_prefixes) {
        str *prefix = str_map_get(self->module_pkg_prefixes, module);
        if (prefix) {
            result = str_qualify(self->ast_arena, *prefix, result);
        }
    }
    return result;
}

str mangle_str_for_arity(allocator *alloc, str name, u8 arity) {
    return str_fmt(alloc, "%s__%i", str_cstr(&name), (int)arity);
}

void mangle_name_for_module(parser *self, ast_node *name, str module) {
    if (ast_node_is_symbol(name) && !str_is_empty(module)) {
        ast_node_name_replace(name, mangle_str_for_module(self, name->symbol.name, module));
        name->symbol.is_module_mangled = 1;
        name->symbol.module            = str_copy(self->ast_arena, module);
    }
}

// Implicit submodule resolution: bare "Child" in #module Parent resolves to
// Parent__Child__Child if Parent.Child is a submodule with a Child symbol.
void maybe_mangle_implicit_submodule(parser *self, ast_node *name) {
    if (!ast_node_is_symbol(name)) return;
    if (name->symbol.is_module_mangled) return;
    if (str_is_empty(self->current_module)) return;

    str name_str = ast_node_str(name);

    // Top-level modules take precedence over implicit submodule resolution.
    if (str_hset_contains(self->modules_seen, name_str)) return;

    str sub_module = str_cat_3(self->transient, self->current_module, S("."), name_str);
    if (!str_hset_contains(self->modules_seen, sub_module)) return;

    hashmap *sub_syms = resolve_module_symbols(self, sub_module);
    if (sub_syms && str_hset_contains(sub_syms, name_str)) {
        mangle_name_for_module(self, name, sub_module);
    }
}

void mangle_name_for_arity(parser *self, ast_node *name, u8 arity, int is_definition) {
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
    parser_dbg(self, "arity mangle '%s' to '%s'\n", str_cstr(&name->symbol.original), str_cstr(&name_str));
}

void mangle_name(parser *self, ast_node *name) {
    // Note: module `main` set current_module to empty, so names are not mangled at all.
    if (str_is_empty(self->current_module)) return;
    if (ast_node_is_nfa(name)) {
        mangle_name(self, name->named_application.name);
        return;
    }
    if (!ast_node_is_symbol(name)) return;
    if (name->symbol.is_module_mangled) return;
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

ast_node *parse_lvalue(parser *self) {
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

int a_field_assignment(parser *self) {
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

// Parse an optional trait bound on a type parameter: T: Trait
// Expects self->result to be the type parameter identifier.
// On success, self->result is still the type parameter (with annotation set if bound present).
int maybe_trait_bound(parser *self) {
    ast_node *type_param = self->result;
    if (0 == a_try(self, a_colon)) {
        if (a_try(self, a_type_identifier)) return 1;
        type_param->symbol.annotation = self->result;
    }
    self->result = type_param;
    return 0;
}

int maybe_type_parameters(parser *self, ast_node_array *out) {
    *out = (ast_node_array){.alloc = self->ast_arena};
    if (0 == a_try(self, a_open_square)) {
        if (0 == a_try(self, a_close_square)) return 0;
        if (a_try(self, a_identifier)) return 1;
        if (maybe_trait_bound(self)) return 1;
        array_push(*out, self->result);

        while (1) {
            if (0 == a_try(self, a_close_square)) return 0;
            if (a_try(self, a_comma)) return 1;
            if (a_try(self, a_identifier)) return 1;
            if (maybe_trait_bound(self)) return 1;
            array_push(*out, self->result);
        }
    }
    return 0;
}

int toplevel_defun(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;
    ast_node_array type_params;
    ast_node_array params = {.alloc = self->ast_arena};

    if (maybe_type_parameters(self, &type_params)) return 1;
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

int toplevel_assign(parser *self) {
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

int toplevel_forward(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (maybe_type_parameters(self, &type_args)) return 1;

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

int toplevel_symbol_annotation(parser *self) {
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

int toplevel_c_chunk(parser *self) {
    if (a_try(self, a_c_block)) return 1;
    return 0;
}

// Build "prefix::module" versioned key for multi-version module isolation.
static str make_version_key(allocator *alloc, str prefix, str module_name) {
    return str_cat_3(alloc, prefix, S("::"), module_name);
}

hashmap *resolve_module_symbols(parser *self, str module_name) {
    if (self->module_pkg_prefixes) {
        str *pfx = str_map_get(self->module_pkg_prefixes, module_name);
        if (pfx) {
            // transient is fine: key is used for lookup only, map_set copies key bytes
            str      vkey = make_version_key(self->transient, *pfx, module_name);
            hashmap *syms = str_map_get_ptr(self->module_symbols, vkey);
            if (syms) return syms;
        }
    }
    return str_map_get_ptr(self->module_symbols, module_name);
}

void save_current_module_symbols(parser *self) {
    if (self->mode != mode_symbols) return;
    str      module_name = str_is_empty(self->current_module) ? S("main") : self->current_module;
    hashmap *copy        = map_copy(self->current_module_symbols);

    // Use versioned key when a package prefix exists, bare name otherwise.
    // resolve_module_symbols() mirrors this: versioned lookup first, bare fallback.
    if (self->module_pkg_prefixes) {
        str *pfx = str_map_get(self->module_pkg_prefixes, module_name);
        if (pfx) {
            str vkey = make_version_key(self->ast_arena, *pfx, module_name);
            str_map_set_ptr(&self->module_symbols, vkey, copy);
            return;
        }
    }
    str_map_set_ptr(&self->module_symbols, module_name, copy);
}

void load_module_symbols(parser *self) {
    str      module_name = str_is_empty(self->current_module) ? S("main") : self->current_module;
    hashmap *syms        = resolve_module_symbols(self, module_name);
    if (syms) {
        // Don't destroy current_module_symbols here - after the first load, it points to a hashmap
        // in module_symbols, and destroying it would corrupt module_symbols entries.
        // The memory is managed by module_symbols and will be freed when the parser is destroyed.
        self->current_module_symbols = syms;
    }
}

void toplevel_hash_unity_file(parser *self, str argument) {
    self->skip_module   = 0;
    self->expect_module = 1;
    tokenizer_set_file(self->tokenizer, argument);
    map_reset(self->module_aliases);
}

int toplevel_hash_module(parser *self, str cmd, str module) {
    // Modules can be re-opened: if #module Foo appears again after #module Foo.Bar, parsing
    // resumes in Foo with its previously collected symbols intact.
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

    // Version-aware dedup: when a package prefix is available, use "prefix::module" key in
    // modules_version_seen so different versions both get parsed while same-version diamond
    // deps are correctly skipped.  modules_seen keeps bare names only — all other consumers
    // (nested-parent validation, #use, #alias, etc.) are unaffected.
    int  already_seen = 0;
    str *pfx          = self->module_pkg_prefixes ? str_map_get(self->module_pkg_prefixes, module) : null;
    if (pfx) {
        str vkey     = make_version_key(self->transient, *pfx, module);
        already_seen = str_hset_contains(self->modules_version_seen, vkey);
        if (!already_seen) str_hset_insert(&self->modules_version_seen, vkey);
    } else {
        already_seen = str_hset_contains(self->modules_seen, module);
    }

    // save current module symbols, if any
    save_current_module_symbols(self);

    if (!already_seen && !is_prelude) {
        str_hset_insert(&self->modules_seen, module);
    }

    if (is_main_function(module)) self->current_module = str_empty();
    else {
        // Note: do not use ast_arena, as it could be speculative and discarded
        self->current_module = str_copy(self->parent_alloc, module);
    }

    // Only reset symbols on first encounter during symbol collection pass.
    // During second pass, current_module_symbols may point to a hashmap in
    // module_symbols (set by load_module_symbols), and resetting it would
    // corrupt module_symbols.
    if (!already_seen && self->mode == mode_symbols) {
        hset_reset(self->current_module_symbols);
    }

    // load module symbols — for re-opened modules, this restores previously
    // collected symbols
    load_module_symbols(self);

    if (is_prelude) {
        str_hset_insert(&self->module_preludes_seen, module);
    }
    return 0;
}

int toplevel_hash_alias(parser *self, str_array words) {
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

int toplevel_hash_unalias(parser *self, str alias) {
    if (!str_map_contains(self->module_aliases, alias)) {
        self->error.tag = tl_err_unalias_not_found;
        return ERROR_STOP;
    }
    str_map_erase(self->module_aliases, alias);
    return 0;
}

int toplevel_hash(parser *self) {
    if (a_try(self, a_hash_command)) return 1;
    ast_node *command = self->result;

    str_array words   = {.alloc = self->ast_arena};
    str_parse_words(command->hash_command.full, &words);

    if (words.size >= 2) {
        str cmd      = words.v[0];
        str argument = words.v[1];
        int res      = 0;
        parser_dbg(self, "hash: %s %s", str_cstr(&cmd), str_cstr(&argument));

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

int toplevel(parser *self) {

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

int toplevel_funcall_only(parser *self) {

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

                if (self->opts.preloaded_path && 0 == strcmp(file, self->opts.preloaded_path)) {
                    // Use pre-loaded data (e.g. stdin); copy into file_arena so
                    // the normal free path works when moving to the next file.
                    if (self->opts.preloaded_size > 0) {
                        char *buf = alloc_malloc(self->file_arena, self->opts.preloaded_size);
                        memcpy(buf, self->opts.preloaded_data, self->opts.preloaded_size);
                        self->current_file_data =
                          (char_csized){.v = buf, .size = self->opts.preloaded_size};
                    } else {
                        self->current_file_data = (char_csized){.v = null, .size = 0};
                    }
                } else {
                    file_read(self->file_arena, file, (char **)&self->current_file_data.v,
                              &self->current_file_data.size);
                }

                // Swap per-file prefix map if available
                if (self->file_pkg_prefixes) {
                    hashmap *per_file         = str_map_get_ptr(self->file_pkg_prefixes, file_str);
                    self->module_pkg_prefixes = per_file ? per_file : self->opts.module_pkg_prefixes;
                }

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
        parser_dbg(self, "parse_all: parsed node %s", str_cstr(&str));

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
        parser_dbg(self, "parse_all_symbols: parsed node %s", str_cstr(&str));
    }

    save_current_module_symbols(self);

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_verbose(parser *p, ast_node_array *out) {
    p->verbose = 1;
    p->mode    = mode_source;

    parser_dbg(p, "begin parse");
    int res = parser_parse_all(p, out);
    parser_dbg(p, "end parse status %i", res);

    p->verbose = 0;
    return res;
}

void parser_result(parser *p, ast_node **handle) {
    if (handle) {
        *handle = p->result;
    }
}

void tokens_push_back(struct parser *p, struct token *tok) {
    array_push(p->tokens, *tok);
}

void tokens_shrink(struct parser *p, u32 n) {
    p->tokens.size = n;
}

//

str next_var_name(parser *self) {
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

int too_many_arguments(parser *self) {
    self->error.tag = tl_err_too_many_arguments;
    return 1;
}

void parser_dbg(struct parser *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "[parse] %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
