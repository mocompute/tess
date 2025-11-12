#include "parser.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "file.h"
#include "hashmap.h"
#include "infer.h"
#include "str.h"
#include "token.h"
#include "tokenizer.h"
#include "type.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PARSER_ARENA_SIZE 1024

struct parser {
    allocator             *parent_alloc;
    allocator             *file_arena;
    allocator             *tokens_arena; // for tokens only
    allocator             *ast_arena;    // for ast nodes and related
    allocator             *transient;    // reset after each call to parser_next

    parser_opts            opts;

    tokenizer             *tokenizer;

    str_sized              files;
    u32                    files_index;
    char_csized            current_file_data;
    hashmap               *modules_seen; // str hset

    ast_node              *result;
    token_array            tokens;

    struct parser_error    error;
    struct tokenizer_error tokenizer_error;
    struct token           token;

    str                    current_module;
    hashmap               *current_module_symbols; // hset str

    u32                    next_nil_name;
    int                    verbose;
    int                    indent_level;
    int                    in_function_application; // enable greedy parsing
    int                    skip_module;             // skip parsing until next module or file
    int expect_module; // expect a module immediately after a #unity_file before any terms
};

typedef int (*parse_fun)(parser *);
typedef int (*parse_fun_s)(parser *, char const *);
typedef int (*parse_fun_int)(parser *, int);

// -- overview --

static int           toplevel(parser *);

static int           a_param(parser *);
static int           a_assignment(parser *);
static int           a_field_assignment(parser *);
static int           a_body_element(parser *);
static int           a_expression(parser *);
static int           a_funcall(parser *);
static int           a_type_constructor(parser *);
static int           a_lambda_function(parser *);
static int           a_reassignment(parser *);
static int           a_statement(parser *);
static int           a_value(parser *);
static ast_node     *create_body(parser *self, ast_node_array exprs);
static int           operator_precedence(char const *op, int is_prefix);
static ast_node     *parse_base_expression(parser *);
static ast_node     *parse_expression(parser *, int min_preced);
static ast_node     *parse_if_expr(parser *);
static ast_node     *parse_cond_expr(parser *);
static ast_node     *parse_lvalue(parser *);
static int           toplevel_defun(parser *);

static int           result_ast(parser *, ast_tag);
static int           result_ast_bool(parser *, int);
static int           result_ast_f64(parser *, f64);
static int           result_ast_i64(parser *, i64);
static int           result_ast_node(parser *, ast_node *);
static int           result_ast_str(parser *, ast_tag, char const *s);
static int           result_ast_u64(parser *, u64);

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
static int           a_colon(parser *);
static int           a_vertical_bar(parser *);
static int           a_char(parser *);
static int           a_comma(parser *);
static int           a_ellipsis(parser *);
static int           a_equal_sign(parser *);
static int           a_identifier(parser *);
static int           a_nil(parser *);
static int           a_null(parser *);
static int           a_number(parser *);
static int           a_open_round(parser *);
static int           a_star(parser *);
static int           a_string(parser *);
static int           the_symbol(parser *, char const *const);

static int           string_to_number(parser *, char const *const);
static void          mangle_name_for_module(parser *, ast_node *, str);
static void          mangle_name(parser *, ast_node *);
static void          unmangle_name(parser *, ast_node *);

static void          tokens_push_back(struct parser *, struct token *);
static void          tokens_shrink(struct parser *, u32);
static int           too_many_arguments(parser *);
static void dbg(struct parser *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, parser_opts const *opts) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));
    alloc_zero(self);
    self->opts                    = *opts;
    self->parent_alloc            = alloc;
    self->file_arena              = arena_create(alloc, 64 * 1024);
    self->tokens_arena            = arena_create(alloc, PARSER_ARENA_SIZE);
    self->ast_arena               = arena_create(alloc, PARSER_ARENA_SIZE);
    self->transient               = arena_create(alloc, PARSER_ARENA_SIZE);
    self->tokenizer               = null;
    self->files                   = opts->files;
    self->files_index             = 0;
    self->current_file_data.v     = null;
    self->current_file_data.size  = 0;
    self->modules_seen            = hset_create(self->parent_alloc, 32);
    self->result                  = null;
    self->tokens                  = (token_array){0};
    self->error                   = (struct parser_error){0};
    self->tokenizer_error         = (struct tokenizer_error){0};
    self->token                   = (struct token){0};
    self->current_module          = str_empty();
    self->current_module_symbols  = hset_create(self->parent_alloc, 32);
    self->next_nil_name           = 0;
    self->verbose                 = 0;
    self->indent_level            = 0;
    self->in_function_application = 0;
    self->skip_module             = 0;
    self->expect_module           = 0;

    self->tokenizer               = null;
    self->tokens                  = (token_array){.alloc = self->tokens_arena};

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
    hset_destroy(&(*self)->current_module_symbols);
    hset_destroy(&(*self)->modules_seen);
    arena_destroy(alloc, &(*self)->transient);
    arena_destroy(alloc, &(*self)->ast_arena);
    arena_destroy(alloc, &(*self)->tokens_arena);
    arena_destroy(alloc, &(*self)->file_arena);
    alloc_free(alloc, *self);
    *self = null;
}

// -- module --

static void add_module_symbol(parser *self, ast_node *name) {
    if (ast_node_is_symbol(name)) {
        str_hset_insert(&self->current_module_symbols,
                        name->symbol.is_mangled ? name->symbol.original : name->symbol.name);
    } else if (ast_node_is_nfa(name)) {
        add_module_symbol(self, name->named_application.name);
    }
}

// -- parser --

static void set_result_file(parser *p) {
    p->result->file = p->token.file;
    p->result->line = p->token.line;
    p->result->col  = p->token.col;
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

static int result_ast_node(parser *p, ast_node *node) {
    p->result = node;
    set_result_file(p);
    return 0;
}

static int is_reserved(char const *s) {
    static char const *strings[] = {
      "beg", "begin", "else", "end", "false", "fun", "if", "in", "let", "then", "struct", "true", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

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
    return 0 == strcmp(s, "[");
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
    static char const *strings[] = {"!", "~", null};
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

        if (0) {
            char *str = token_to_string(p->transient, &p->token);
            dbg(p, "next_token: %s", str);
            alloc_free(p->transient, str);
        }

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

static int a_close_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_round == p->token.tag) return result_ast_str(p, ast_symbol, ")");
    p->error.tag = tl_err_expected_close_round;
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

static int a_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]");
    p->error.tag = tl_err_expected_close_square;
    return 1;
}

static int a_binary_operator(parser *self, int min_prec) {
    if (next_token(self)) {
        if (is_eof(self)) return result_ast_str(self, ast_symbol, ";");
        return 1;
    }

    char const *op = null;
    switch (self->token.tag) {
    case tok_symbol:
        if (is_arithmetic_operator(self->token.s) || is_logical_operator(self->token.s) ||
            is_relational_operator(self->token.s)) {
            op = self->token.s;
        } else return 1;
        break;

    case tok_bang_equal:   op = "!="; break;
    case tok_star:         op = "*"; break;
    case tok_dot:          op = "."; break;
    case tok_equal_equal:  op = "=="; break;
    case tok_logical_and:  op = "&&"; break;
    case tok_arrow:        op = "->"; break;
    case tok_ampersand:    op = "&"; break;
    case tok_open_square:  op = "["; break;
    case tok_plus:         op = "+"; break;
    case tok_minus:        op = "-"; break;

    case tok_bang:
    case tok_bar:
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
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_char:
    case tok_comment:
    case tok_hash_command: return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 0);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

static int a_unary_operator(parser *self, int min_prec) {
    if (next_token(self)) {
        if (is_eof(self)) return result_ast_str(self, ast_symbol, ";");
        return 1;
    }

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
    case tok_semicolon:
    case tok_logical_and:
    case tok_arrow:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_open_square:
    case tok_close_square:
    case tok_equal_sign:
    case tok_equal_equal:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_char:
    case tok_comment:
    case tok_hash_command: return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 1);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

static int a_identifier(parser *p) {
    if (next_token(p)) return 1;

    if (tok_symbol != p->token.tag || 0 == strlen(p->token.s) || is_reserved(p->token.s)) goto error;

    char const c = p->token.s[0];
    if (('_' == c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {

        // first character good, check remaining characters
        char const *pc = &p->token.s[1];
        while (*pc) {
            if ('_' == *pc || ('a' <= *pc && *pc <= 'z') || ('A' <= *pc && *pc <= 'Z') ||
                ('0' <= *pc && *pc <= '9')) {
                pc++; // still good
            } else {
                goto error;
            }
        }

        // check for reserved words, which are not allowed as identifiers
        if (is_reserved(p->token.s)) goto error;

        return result_ast_str(p, ast_symbol, p->token.s);
    }

error:
    p->error.tag = tl_err_expected_identifier;
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

    if (tok_string == p->token.tag) return result_ast_str(p, ast_string, p->token.s);

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

static int a_star(parser *p) {
    if (next_token(p)) return 1;
    if (tok_star == p->token.tag) return result_ast_str(p, ast_symbol, "*");
    p->error.tag = tl_err_expected_star;
    return 1;
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

    if (0 == a_try_s(self, the_symbol, "void")) return result_ast(self, ast_nil);
    if ((0 == a_open_round(self)) && (0 == a_close_round(self))) return result_ast(self, ast_nil);

    self->error.tag = tl_err_expected_nil;
    return 1;
}

static int a_null(parser *self) {

    // FIXME: clarify difference between null and nil
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

static int a_type_identifier(parser *self) {
    // Callers expect name to be mangled.
    if (0 == a_try(self, a_funcall)) {
        mangle_name(self, self->result->named_application.name);
        return 0;
    }
    if (0 == a_try(self, a_identifier)) {
        mangle_name(self, self->result);
        return 0;
    }
    if (0 == a_try(self, a_ellipsis)) {
        return 0;
    }

    return 1;
}

static int a_type_annotation(parser *self) {
    if (0 == a_try(self, a_colon)) {
        int res = a_try(self, a_type_identifier);
        return res;
    }

    self->error.tag = tl_err_expected_colon;
    return 1;
}

static int a_param(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *ident = self->result;
    ast_node *ann   = null;
    if (0 == a_try(self, a_type_annotation)) {
        ann = self->result;
    }

    assert(ast_node_is_symbol(ident));
    ident->symbol.annotation = ann;
    return result_ast_node(self, ident);
}

static int a_funcall(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

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

done:

    array_shrink(args);
    mangle_name(self, name);
    ast_node *node = ast_node_create_nfa(self->ast_arena, name, (ast_node_sized)sized_all(args));
    return result_ast_node(self, node);
}

static int a_type_constructor(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

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
    ast_node *node = ast_node_create_nfa(self->ast_arena, name, (ast_node_sized)sized_all(args));
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
        if (a_try(self, a_expression)) return 1;
        array_push(exprs, self->result);
    }

    array_shrink(exprs);
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)sized_all(exprs));

    ast_node *l    = ast_node_create(self->ast_arena, ast_lambda_function);
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

static int a_value(parser *self) {
    if (0 == a_try(self, a_type_constructor)) return 0;
    if (0 == a_try(self, a_funcall)) return 0;
    if (0 == a_try(self, a_lambda_function)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    if (0 == a_try(self, a_string)) return 0;
    if (0 == a_try(self, a_char)) return 0;
    if (0 == a_try(self, a_bool)) return 0;
    if (0 == a_try(self, a_nil)) return 0;
    if (0 == a_try(self, a_null)) return 0;
    if (0 == a_try(self, a_identifier)) {
        mangle_name(self, self->result);
        return 0;
    }

    self->error.tag = tl_err_expected_value;
    return 1;
}

static int operator_precedence(char const *op, int is_prefix) {

    struct item {
        char const *op;
        int         p;
    };
    static struct item const infix[] = {
      {"||", 10},
      {"&&", 20},
      {"|", 30},
      {"&", 40},
      //
      {"==", 50},
      {"!=", 50},
      //
      {"<", 60},
      {"<=", 60},
      {">=", 60},
      {">", 60},
      //
      {"<<", 70},
      {">>", 70},
      //
      {"+", 80},
      {"-", 80},
      //
      {"*", 90},
      {"/", 90},
      {"%", 90},
      //
      {".", 120},
      {"->", 120},
      {"[", 110},
      //
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

    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (a_try(self, a_body_element)) return null;
        array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    ast_node *yes = create_body(self, exprs);
    exprs         = (ast_node_array){.alloc = self->ast_arena};

    ast_node *no  = null;

    if (0 == a_try_s(self, the_symbol, "else")) {
        if (0 == a_try_s(self, the_symbol, "if")) {
            no = parse_if_continue(self);
        } else {
            if (a_try(self, a_open_curly)) return null;
            while (1) {
                if (a_try(self, a_body_element)) return null;
                array_push(exprs, self->result);
                if (0 == a_try(self, a_close_curly)) break;
            }
            no = create_body(self, exprs);
        }
    }
    if (!no) no = null; // ok to have no case null

    ast_node *n = ast_node_create_if_then_else(self->ast_arena, cond, yes, no);
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

static ast_node *parse_cond_arm(parser *self) {

    ast_node *cond = parse_expression(self, INT_MIN);
    if (!cond) return null;

    if (a_try(self, a_open_curly)) return null;

    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (a_try(self, a_body_element)) return null;
        array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    ast_node *yes = create_body(self, exprs);

    if (0 == a_try(self, a_close_curly)) {
        // close the cond expr with no else case
        ast_node *n = ast_node_create_if_then_else(self->ast_arena, cond, yes, null);
        return n;
    }

    ast_node *no = parse_cond_arm(self);
    if (!no) return null;

    ast_node *n = ast_node_create_if_then_else(self->ast_arena, cond, yes, no);
    return n;
}

static ast_node *parse_cond_expr(parser *self) {
    if (a_try_s(self, the_symbol, "cond")) {
        self->error.tag = tl_err_ok;
        return null;
    }
    if (a_try(self, a_open_curly)) {
        return null;
    }
    return parse_cond_arm(self);
}

//

static ast_node *parse_base_expression(parser *self) {

    if (0 == a_try_int(self, a_unary_operator, INT_MIN)) {
        ast_node *op   = self->result;
        int       prec = operator_precedence(str_cstr(&op->symbol.name), 1);
        ast_node *expr = parse_expression(self, prec);
        if (!expr) return null;
        ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, expr);
        return unary;
    }

    // lambda function is identified by open round, so we need to parse it before nil and grouped
    // expressions.
    if (0 == a_try(self, a_lambda_funcall)) return self->result;
    if (0 == a_try(self, a_lambda_function)) return self->result;
    if (0 == a_try(self, a_nil)) return self->result; // parse () before (...)

    if (0 == a_try(self, a_open_round)) {
        ast_node *expr = parse_expression(self, INT_MIN);
        if (a_try(self, a_close_round)) return null;
        return expr;
    }

    ast_node *node;
    node = parse_if_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;
    node = parse_cond_expr(self);
    if (node) return node;

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
            if (0 == str_cmp_c(op->symbol.name, ".")) {
                if (0 == a_try(self, a_star)) {
                    op              = self->result;
                    ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, left);
                    left            = unary;
                    continue;
                } else if (0 == a_try(self, a_ampersand)) {
                    op              = self->result;
                    ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, left);
                    left            = unary;
                    continue;
                }
            }

            int prec = operator_precedence(str_cstr(&op->symbol.name), 0);
            assert(prec >= min_prec);

            // if opening an index expression, reset min prec to minimum
            if (0 == str_cmp_c(op->symbol.name, "[")) prec = INT_MIN;

            ast_node *right = parse_expression(self, prec + assoc);
            if (!right) return null;

            // check for unary + and minus operators and convert to binary
            if (ast_unary_op == right->tag) {
                if (0 == str_cmp_c(right->unary_op.op->symbol.name, "+") ||
                    0 == str_cmp_c(right->unary_op.op->symbol.name, "-")) {
                    ast_node *binop = ast_node_create_binary_op(self->ast_arena, right->unary_op.op, left,
                                                                right->unary_op.operand);
                    left            = binop;
                    continue;
                }
            }

            // Note: special case: mangle Module.foo and Module.bar() to simple expressions
            if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(left) &&
                str_hset_contains(self->modules_seen, left->symbol.name)) {
                ast_node *to_mangle = null;
                if (ast_node_is_symbol(right)) to_mangle = right;
                else if (ast_node_is_nfa(right)) to_mangle = right->named_application.name;
                if (to_mangle) {
                    unmangle_name(self, to_mangle);
                    mangle_name_for_module(self, to_mangle, left->symbol.name);
                    left = right;
                    continue;
                }
            }

            // Note: special case: [ as binary operator, need to close it with ] token
            if (0 == str_cmp_c(op->symbol.name, "["))
                if (a_try(self, a_close_square)) return null;

            ast_node *binop = ast_node_create_binary_op(self->ast_arena, op, left, right);
            left            = binop;

        } else break;
    }

    return left;
}

static int a_expression(parser *self) {
    ast_node *res = parse_expression(self, INT_MIN);
    if (!res) return 1;
    return result_ast_node(self, res);
}

static void unmangle_name(parser *self, ast_node *name) {
    (void)self;
    if (!ast_node_is_symbol(name)) return;
    if (!name->symbol.is_mangled) return;
    if (str_is_empty(name->symbol.original)) return;
    name->symbol.name       = name->symbol.original;
    name->symbol.is_mangled = 0;
}

static void mangle_name_for_module(parser *self, ast_node *name, str module) {
    if (ast_node_is_symbol(name) && !str_is_empty(module)) {
        ast_node_name_replace(name, str_cat_3(self->ast_arena, module, S("_"), name->symbol.name));
        name->symbol.is_mangled = 1;
        if (0) {
            fprintf(stderr, "parser: mangle '%s' to '%s'\n", str_cstr(&name->symbol.original),
                    str_cstr(&name->symbol.name));
        }
    }
}

static void mangle_name(parser *self, ast_node *name) {
    if (str_is_empty(self->current_module)) return;
    if (ast_node_is_nfa(name)) return mangle_name(self, name->named_application.name);
    if (!ast_node_is_symbol(name)) return;
    if (name->symbol.is_mangled) return;

    // Don't mangle names of known types
    str name_str = ast_node_str(name);
    if (tl_type_registry_get(self->opts.registry, name_str)) return;

    // Don't mangle c_ names
    if (is_c_symbol(name_str)) return;

    // Don't mangle names in 'builtin' module
    if (str_eq(self->current_module, S("builtin"))) return;

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

    if (a_try(self, a_equal_sign)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a = ast_node_create_assignment(self->ast_arena, lval, val);
    return result_ast_node(self, a);
}

static int a_field_assignment(parser *self) {
    // x = val (for type literals)
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_equal_sign)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a                 = ast_node_create_assignment(self->ast_arena, name, val);
    a->assignment.is_field_name = 1;
    return result_ast_node(self, a);
}

static int a_assignment(parser *self) {
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    if (a_try(self, a_colon_equal)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (a_body_element(self)) break;
        array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs);

    ast_node *a    = ast_node_create_let_in(self->ast_arena, lval, val, body);
    return result_ast_node(self, a);
}

static int a_return_statement(parser *self) {
    if (a_try_s(self, the_symbol, "return")) return 1;

    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return 1;

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

    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (a_try(self, a_body_element)) return 1;
        array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    ast_node *body = create_body(self, exprs);

    ast_node *r    = ast_node_create_while(self->ast_arena, condition, update, body);
    return result_ast_node(self, r);
}

static int a_statement(parser *self) {
    if (0 == a_try(self, a_assignment)) return 0;
    if (0 == a_try(self, a_reassignment)) return 0;
    if (0 == a_try(self, a_while_statement)) return 0;
    if (0 == a_try(self, a_break_statement)) return 0;
    if (0 == a_try(self, a_continue_statement)) return 0;
    if (0 == a_try(self, a_return_statement)) return 0;

    return 1;
}

static int a_body_element(parser *self) {
    // Note: statement before expression, because assignment and ident are ambiguous
    if (0 == a_try(self, a_statement) || 0 == a_try(self, a_expression)) return 0;
    else return 1;
}

static ast_node *create_body(parser *self, ast_node_array exprs) {
    array_shrink(exprs);
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)sized_all(exprs));
    return body;
}

static int toplevel_defun(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node      *name   = self->result;
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
        if (a_try(self, a_body_element)) return 2; // stop parsing
        array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs);

    add_module_symbol(self, name);
    mangle_name(self, name);
    ast_node *let = ast_node_create_let(self->ast_arena, name, (ast_node_sized)sized_all(params), body);
    set_node_parameters(self, let, &params);
    let->let.name = name;
    let->let.body = body;

    result_ast_node(self, let);

    return 0;
}

static int toplevel_assign(parser *self) {
    // cannot use parse_lvalue here
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_colon_equal)) return 1;
    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return 2;

    ast_node *n = ast_node_create_let_in(self->ast_arena, name, value, null);
    return result_ast_node(self, n);
}

static int toplevel_forward(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node      *name   = self->result;
    ast_node_array params = {.alloc = self->ast_arena};

    if (a_try(self, a_open_round)) return 1;
    if (0 == a_try(self, a_close_round)) goto decl_done;
    if (0 == a_try(self, a_param)) array_push(params, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto decl_done;
        if (a_try(self, a_comma)) return 2;
        if (a_try(self, a_param)) return 2;
        array_push(params, self->result);
    }

decl_done:

    if (a_try(self, a_arrow)) return 1;

    if (a_try(self, a_type_identifier)) return 1;
    ast_node *ann = self->result;

    // convert param type annotations into an ast tuple so we can make an ast arrow
    forall(i, params) {
        assert(ast_node_is_symbol(params.v[i]));
        assert(params.v[i]->symbol.annotation);

        // replace param with its annotation
        params.v[i] = params.v[i]->symbol.annotation;
    }

    // make tuple
    array_shrink(params);
    ast_node *tup = ast_node_create_tuple(self->ast_arena, (ast_node_sized)sized_all(params));

    // make arrow
    ast_node *arrow = ast_node_create_arrow(self->ast_arena, tup, ann);

    // attach to name
    name->symbol.annotation = arrow;

    add_module_symbol(self, name);
    mangle_name(self, name);

    return result_ast_node(self, name);
}

static int toplevel_symbol_annotation(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *ident = self->result;

    if (a_try(self, a_type_annotation)) return 2;
    ast_node *ann = self->result;

    assert(ast_node_is_symbol(ident));
    ident->symbol.annotation = ann;

    add_module_symbol(self, ident);
    mangle_name(self, ident);
    return result_ast_node(self, ident);
}

static int toplevel_c_chunk(parser *self) {
    if (a_try(self, a_c_block)) return 1;
    return 0;
}

static int toplevel_hash(parser *self) {
    if (a_try(self, a_hash_command)) return 1;
    ast_node *command = self->result;

    str_array words   = {.alloc = self->ast_arena};
    str_parse_words(command->hash_command.full, &words);

    if (words.size >= 2) {
        str command  = words.v[0];
        str argument = words.v[1];
        dbg(self, "hash: %s %s", str_cstr(&command), str_cstr(&argument));
        if (str_eq(command, S("unity_file"))) {
            self->skip_module   = 0;
            self->expect_module = 1;
            tokenizer_set_file(self->tokenizer, argument);
        }

        else if (str_eq(command, S("module"))) {
            // Modules: the name's sole use is to prevent multiple evaluations of the same terms. If a
            // duplicate name is seen, parsing will stop returning terms it sees until a new #module or new
            // #unity_file directive is seen.
            str module          = argument;
            self->skip_module   = 0;
            self->expect_module = 0;
            if (str_hset_contains(self->modules_seen, module)) self->skip_module = 1;
            else {
                str_hset_insert(&self->modules_seen, module);
                if (str_eq(module, S("main"))) self->current_module = str_empty();
                else self->current_module = module;
                hset_reset(self->current_module_symbols);
            }
        }
    }

    array_shrink(words);
    command->hash_command.words = (str_sized)sized_all(words);

    return 0;
}

static int toplevel_type_alias(parser *self) {
    ast_node *name = null;
    if (0 == a_try(self, a_funcall) || 0 == a_try(self, a_identifier)) name = self->result;
    else return 1;
    if (a_try(self, a_equal_sign)) return 1;

    ast_node *target = null;
    if (0 == a_try(self, a_funcall) || 0 == a_try(self, a_identifier)) target = self->result;
    else return 1;

    add_module_symbol(self, name);
    mangle_name(self, name);
    ast_node *node = ast_node_create_type_alias(self->ast_arena, name, target);
    return result_ast_node(self, node);

    return 0;
}

static int toplevel_enum(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;
    ast_node_array idents = {.alloc = self->ast_arena};
    while (1) {
        int saw_comma = 0;
        if (0 == a_try(self, a_comma)) saw_comma = 1; // optional comma
        if (0 == a_try(self, a_close_curly)) break;
        if (!saw_comma && idents.size) {
            // require comma separators
            if (a_try(self, a_comma)) return 1;
        }
        if (a_try(self, a_identifier))
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

static int toplevel_struct(parser *self) {

    if (a_try(self, a_type_identifier)) return 1; // a_type_identifer mangles name
    ast_node *type_ident = self->result;

    if (a_try(self, a_colon)) return 1;

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array fields = {.alloc = self->ast_arena};
    while (1) {
        int saw_comma = 0;
        if (0 == a_try(self, a_comma)) saw_comma = 1; // optional comma
        if (0 == a_try(self, a_close_curly)) break;
        if (!saw_comma && fields.size) {
            // require comma separators
            if (a_try(self, a_comma)) return 1;
        }

        // this syntax overlaps with enums, so we can't exit parse too early
        if (a_try(self, a_param)) return saw_comma ? 2 : 1; // far enough along to exit parse
        array_push(fields, self->result);
    }
    array_shrink(fields);

    ast_node *r               = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union = 0;
    if (ast_node_is_symbol(type_ident)) {
        r->user_type_def.n_type_arguments = 0;
        r->user_type_def.type_arguments   = null;
        r->user_type_def.name             = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        r->user_type_def.n_type_arguments = type_ident->named_application.n_arguments;
        r->user_type_def.type_arguments   = type_ident->named_application.arguments;
        r->user_type_def.name             = type_ident->named_application.name;
    } else fatal("logic error");

    // The utd struct separates names from annotations, while they are both in the same symbol ast
    // node variant. So we have to do this splitting just for it to recombine later.
    r->user_type_def.n_fields          = fields.size;
    r->user_type_def.field_names       = alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
    r->user_type_def.field_annotations = alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
    forall(i, fields) {
        r->user_type_def.field_names[i]       = fields.v[i];
        r->user_type_def.field_annotations[i] = fields.v[i]->symbol.annotation;
    }

    add_module_symbol(self, type_ident);
    mangle_name(self, type_ident);
    return result_ast_node(self, r);
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
        if (a_try(self, a_param)) return 2; // exit parse
        array_push(fields, self->result);
    }
    array_shrink(fields);

    ast_node *r               = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union = 1;
    if (ast_node_is_symbol(type_ident)) {
        r->user_type_def.n_type_arguments = 0;
        r->user_type_def.type_arguments   = null;
        r->user_type_def.name             = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        r->user_type_def.n_type_arguments = type_ident->named_application.n_arguments;
        r->user_type_def.type_arguments   = type_ident->named_application.arguments;
        r->user_type_def.name             = type_ident->named_application.name;
    } else fatal("logic error");

    // The utd struct separates names from annotations, while they are both in the same symbol ast
    // node variant. So we have to do this splitting just for it to recombine later.
    r->user_type_def.n_fields          = fields.size;
    r->user_type_def.field_names       = alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
    r->user_type_def.field_annotations = alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
    forall(i, fields) {
        r->user_type_def.field_names[i]       = fields.v[i];
        r->user_type_def.field_annotations[i] = fields.v[i]->symbol.annotation;
    }

    add_module_symbol(self, type_ident);
    mangle_name(self, type_ident);
    return result_ast_node(self, r);
}

static int toplevel(parser *self) {

    self->error.tag = tl_err_ok;

    while (!is_eof(self)) {

        int res = 0;

        if (0 == a_try(self, toplevel_c_chunk)) goto success_hash;
        if (0 == a_try(self, toplevel_hash)) goto success_hash;
        if (0 == a_try(self, toplevel_type_alias)) goto success;

        if (0 == (res = a_try(self, toplevel_enum))) goto success;
        else if (2 == res) goto error;

        if (0 == (res = a_try(self, toplevel_struct))) goto success;
        else if (2 == res) goto error;

        if (0 == (res = a_try(self, toplevel_union))) goto success;
        else if (2 == res) goto error;

        if (0 == (res = a_try(self, toplevel_defun))) goto success;
        else if (2 == res) goto error;

        if (0 == (res = a_try(self, toplevel_assign))) goto success;
        else if (2 == res) goto error;

        if (0 == (res = a_try(self, toplevel_forward))) goto success;
        else if (2 == res) goto error;

        if (0 == (res = a_try(self, toplevel_symbol_annotation))) goto success;
        else if (2 == res) goto error;

        self->error.tag = tl_err_expected_toplevel;
        return 1;

    error:
        return res;

    success:
        if (self->expect_module) {
            self->error.tag = tl_err_expected_module;
            return 1;
        }

    success_hash:
        if (!self->skip_module) return 0;
    }

    return 1;
}

int parser_next(parser *self) {
    if (!self->tokenizer) {

        // A new tokenizer is created for each file being parsed.

        self->error.tag           = tl_err_ok;
        self->tokenizer_error.tag = tl_err_ok;

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

        // read file
        char const *file = str_cstr(&self->files.v[self->files_index++]);
        file_read(self->file_arena, file, (char **)&self->current_file_data.v,
                  &self->current_file_data.size);

        self->tokenizer     = tokenizer_create(self->parent_alloc, self->current_file_data, file);
        self->expect_module = 1;
    }

    int res = toplevel(self);

    if (0 == res) {
        self->result->file = self->error.file;
        self->result->line = self->error.line;
        self->result->col  = self->error.col;
    } else if (is_eof(self)) {
        if (self->tokenizer) tokenizer_destroy(&self->tokenizer);
        self->tokenizer   = null;
        self->tokens.size = 0;

        // keep going with next file if possible
        res = parser_next(self);
    }

    arena_reset(self->transient);

    return res;
}

int parser_parse_all(parser *p, ast_node_array *out) {

    int res = 0;
    while (0 == (res = parser_next(p))) {
        ast_node *node;

        parser_result(p, &node);
        str str = v2_ast_node_to_string(p->transient, node);
        dbg(p, "parse_all: parsed node %s", str_cstr(&str));

        array_push(*out, node);
    }

    if (is_eof(p)) return 0;

    return res;
}

int parser_parse_all_verbose(parser *p, ast_node_array *out) {
    p->verbose = 1;

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

void parser_report_errors(parser *self) {
    if (tl_err_ok == self->error.tag) return;

    fprintf(stderr, "%s:%u:%u: %s\n", self->error.file, self->error.line, self->error.col,
            tl_error_tag_to_string(self->error.tag));
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
