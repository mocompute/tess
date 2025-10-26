#include "parser.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "file.h"
#include "str.h"
#include "token.h"
#include "tokenizer.h"

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

    tokenizer             *tokenizer;

    c_string_csized        files;
    u16                    files_index;
    char_csized            current_file_data;

    ast_node              *result;
    token_array            tokens;

    struct parser_error    error;
    struct tokenizer_error tokenizer_error;
    struct token           token;

    u32                    next_nil_name;
    int                    verbose;
    int                    indent_level;
    int                    in_function_application; // enable greedy parsing
};

typedef int (*parse_fun)(parser *);
typedef int (*parse_fun_s)(parser *, char const *);
typedef int (*parse_fun_int)(parser *, int);

// -- overview --

static int           toplevel(parser *);

static int           a_param(parser *);
static int           a_assignment(parser *);
static int           a_simple_assignment(parser *);
static int           a_body_element(parser *);
static int           a_expression(parser *);
static int           a_funcall(parser *);
static int           a_type_literal(parser *);
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
// nodiscard static int a_try_special(parser *, parse_fun);
// nodiscard static int a_try_special_ext(parser *, parse_fun);

static int  eat_comments(parser *);
static int  next_token(parser *);

static int  a_arrow(parser *);
static int  a_bool(parser *);
static int  a_close_round(parser *);
static int  a_colon(parser *);
static int  a_comma(parser *);
static int  a_equal_sign(parser *);
static int  a_identifier(parser *);
static int  a_nil(parser *);
static int  a_number(parser *);
static int  a_open_round(parser *);
static int  a_string(parser *);
static int  the_symbol(parser *, char const *const);

static int  string_to_number(parser *, char const *const);

static void tokens_push_back(struct parser *, struct token *);
static void tokens_shrink(struct parser *, u32);
static int  too_many_arguments(parser *);
static void log(struct parser *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, char_csized preamble, c_string_csized files) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));

    alloc_zero(self);
    self->parent_alloc            = alloc;
    self->file_arena              = arena_create(alloc, 64 * 1024);
    self->tokens_arena            = arena_create(alloc, PARSER_ARENA_SIZE);
    self->ast_arena               = arena_create(alloc, PARSER_ARENA_SIZE);
    self->transient               = arena_create(alloc, PARSER_ARENA_SIZE);
    self->files                   = files;
    self->files_index             = 0;
    self->current_file_data.v     = null;
    self->current_file_data.size  = 0;
    self->verbose                 = 0;
    self->indent_level            = 0;
    self->in_function_application = 0;

    self->tokenizer               = tokenizer_create(alloc, preamble, "std_preamble");
    self->tokens                  = (token_array){.alloc = self->tokens_arena};

    token_init(&self->token, tok_invalid);
    self->error.token     = &self->token;
    self->error.tokenizer = &self->tokenizer_error;

    return self;
}

parser *parser_create_simple(allocator *alloc, char const *in, u32 len) {
    return parser_create(alloc, (char_csized){.v = in, .size = len}, (c_string_csized){.v = null});
}

void parser_destroy(parser **self) {
    // error token: arena
    // tokens: arena

    // tokenizer
    if ((*self)->tokenizer) tokenizer_destroy(&(*self)->tokenizer);

    // arena
    allocator *alloc = (*self)->parent_alloc;
    arena_destroy(alloc, &(*self)->transient);
    arena_destroy(alloc, &(*self)->ast_arena);
    arena_destroy(alloc, &(*self)->tokens_arena);
    arena_destroy(alloc, &(*self)->file_arena);
    alloc_free(alloc, *self);
    *self = null;
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
    p->result          = ast_node_create(p->ast_arena, ast_i64);
    p->result->i64.val = val;
    set_result_file(p);
    return 0;
}

static int result_ast_u64(parser *p, u64 val) {
    p->result          = ast_node_create(p->ast_arena, ast_u64);
    p->result->u64.val = val;
    set_result_file(p);
    return 0;
}

static int result_ast_f64(parser *p, f64 val) {
    p->result          = ast_node_create(p->ast_arena, ast_f64);
    p->result->f64.val = val;
    set_result_file(p);
    return 0;
}

static int result_ast_bool(parser *p, int val) {
    p->result            = ast_node_create(p->ast_arena, ast_bool);
    p->result->bool_.val = val;
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

int is_index_operator(char const *s) {
    return 0 == strcmp(s, "[");
}

int is_struct_access_operator(char const *s) {
    static char const *strings[] = {".", "->", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

static int is_unary_operator(char const *s) {
    static char const *strings[] = {"!", "*", "&", "~", null};
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
            log(p, "tokenizer error: %s", tl_error_tag_to_string(p->tokenizer_error.tag));
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

        char *str = token_to_string(p->transient, &p->token);
        log(p, "next_token: %s", str);
        alloc_free(p->transient, str);

        tokens_push_back(p, &p->token);
        return 0;
    }
}

nodiscard static int a_try(parser *p, parse_fun fun) {
    int       result    = 0;
    u32 const save_toks = p->tokens.size;

    if (fun(p)) {
        assert(p->tokens.size >= save_toks);
        if (p->tokens.size > save_toks) {
            char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
            // log(p, "a_try: put back %i tokens starting with %s", p->tokens.size - save_toks, str);
            alloc_free(p->transient, str);
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }
        result = 1;
        goto cleanup;
    }

cleanup:
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return result;
}

nodiscard static int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
    u32 const save_toks = p->tokens.size;
    if (fun(p, arg)) {
        if (p->tokens.size > save_toks) {
            char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
            alloc_free(p->transient, str);

            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }

        return 1;
    }
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return 0;
}

nodiscard static int a_try_int(parser *p, parse_fun_int fun, int arg) {
    u32 const save_toks = p->tokens.size;
    if (fun(p, arg)) {
        if (p->tokens.size > save_toks) {
            char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
            alloc_free(p->transient, str);

            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }

        return 1;
    }
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return 0;
}

static int a_comma(parser *p) {
    if (next_token(p)) return 1;
    if (tok_comma == p->token.tag) return result_ast_str(p, ast_symbol, ",");
    p->error.tag = tl_err_expected_comma;
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

    case tok_star:         op = "*"; break;
    case tok_dot:          op = "."; break;
    case tok_equal_equal:  op = "=="; break;
    case tok_logical_and:  op = "&&"; break;
    case tok_arrow:        op = "->"; break;
    case tok_ampersand:    op = "&"; break;
    case tok_open_square:  op = "["; break;

    case tok_comma:
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
    case tok_comment:      return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 0);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

static int a_unary_operator(parser *self) {
    if (next_token(self)) {
        if (is_eof(self)) return result_ast_str(self, ast_symbol, ";");
        return 1;
    }

    if (tok_symbol == self->token.tag)
        if (is_unary_operator(self->token.s))
            return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, self->token.s));

    if (tok_star == self->token.tag || tok_ampersand == self->token.tag) {
        ast_node *sym;
        if (tok_star == self->token.tag) sym = ast_node_create_sym_c(self->ast_arena, "*");
        else sym = ast_node_create_sym_c(self->ast_arena, "&");
        return result_ast_node(self, sym);
    }

    return 1;
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

    if ((0 == a_open_round(self)) && (0 == a_close_round(self))) return result_ast(self, ast_nil);

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

    if (0 == a_try(self, a_funcall)) return 0;
    if (0 == a_try(self, a_identifier)) return 0;

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
    ast_node *node                      = ast_node_create(self->ast_arena, ast_named_function_application);
    node->named_application.arguments   = args.v;
    node->named_application.n_arguments = args.size;
    node->named_application.name        = name;
    return result_ast_node(self, node);
}

static int a_type_literal(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_curly)) goto done;
    if (0 == a_try(self, a_simple_assignment)) array_push(args, self->result);

    while (1) {
        if (0 == a_try(self, a_close_curly)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_simple_assignment)) return 1;
        array_push(args, self->result);
    }

done:
    array_shrink(args);
    ast_node *node                      = ast_node_create(self->ast_arena, ast_named_function_application);
    node->named_application.arguments   = args.v;
    node->named_application.n_arguments = args.size;
    node->named_application.name        = name;
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
    ast_node *body         = ast_node_create(self->ast_arena, ast_body);
    body->body.expressions = (ast_node_sized)sized_all(exprs);

    ast_node *l            = ast_node_create(self->ast_arena, ast_lambda_function);
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

done:

    array_shrink(args);
    ast_node *node                     = ast_node_create(self->ast_arena, ast_lambda_function_application);
    node->lambda_application.lambda    = lambda;
    node->lambda_application.arguments = args.v;
    node->lambda_application.n_arguments = args.size;
    return result_ast_node(self, node);
}

static int a_value(parser *self) {
    if (0 == a_try(self, a_type_literal)) return 0;
    if (0 == a_try(self, a_funcall)) return 0;
    if (0 == a_try(self, a_lambda_function)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    if (0 == a_try(self, a_string)) return 0;
    if (0 == a_try(self, a_bool)) return 0;
    if (0 == a_try(self, a_nil)) return 0;
    if (0 == a_try(self, a_identifier)) return 0;
    // if (0 == a_try(self, b_field)) return 0;

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
      {".", 110},
      {"->", 110},
      {"[", 110},
      //
      {null, 0},
    };

    static struct item const prefix[] = {
      {"-", 100},
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
    if (a_try(self, a_open_curly)) return null;

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
    if (!no) no = ast_node_create(self->ast_arena, ast_nil);

    ast_node *n               = ast_node_create(self->ast_arena, ast_if_then_else);
    n->if_then_else.condition = cond;
    n->if_then_else.yes       = yes;
    n->if_then_else.no        = no;
    return n;
}

static ast_node *parse_if_expr(parser *self) {
    if (a_try_s(self, the_symbol, "if")) return null;
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
        ast_node *n               = ast_node_create(self->ast_arena, ast_if_then_else);
        n->if_then_else.condition = cond;
        n->if_then_else.yes       = yes;
        n->if_then_else.no        = ast_node_create(self->ast_arena, ast_nil);
        return n;
    }

    ast_node *no = parse_cond_arm(self);
    if (!no) return null;

    ast_node *n               = ast_node_create(self->ast_arena, ast_if_then_else);
    n->if_then_else.condition = cond;
    n->if_then_else.yes       = yes;
    n->if_then_else.no        = no;
    return n;
}

static ast_node *parse_cond_expr(parser *self) {
    if (a_try_s(self, the_symbol, "cond")) return null;
    if (a_try(self, a_open_curly)) return null;
    return parse_cond_arm(self);
}

//

static ast_node *parse_base_expression(parser *self) {

    if (0 == a_try(self, a_unary_operator)) {
        ast_node *op   = self->result;
        int       prec = operator_precedence(str_cstr(&op->symbol.name), 1);
        ast_node *expr = parse_expression(self, prec);
        if (!expr) return null;
        ast_node *unary         = ast_node_create(self->ast_arena, ast_unary_op);
        unary->unary_op.operand = expr;
        unary->unary_op.op      = op;
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
    node = parse_cond_expr(self);
    if (node) return node;

    if (0 == a_try(self, a_value)) return self->result;
    return null;
}

static ast_node *parse_expression(parser *self, int min_prec) {

    ast_node *left = parse_base_expression(self);
    if (!left) return null;
    while (1) {
        if (0 == a_try_int(self, a_binary_operator, min_prec)) {
            ast_node *op   = self->result;

            int       prec = operator_precedence(str_cstr(&op->symbol.name), 0);
            assert(prec >= min_prec);
            ast_node *right = parse_expression(self, prec + 1); // (prec+1): left-associative
            if (!right) return null;

            // Note: special case: [ as binary operator, need to close it with ] token
            if (0 == str_cmp_c(op->symbol.name, "["))
                if (a_try(self, a_close_square)) return null;

            ast_node *binop        = ast_node_create(self->ast_arena, ast_binary_op);
            binop->binary_op.left  = left;
            binop->binary_op.right = right;
            binop->binary_op.op    = op;
            left                   = binop;
        } else break;
    }

    return left;
}

static int a_expression(parser *self) {
    ast_node *res = parse_expression(self, INT_MIN);
    if (!res) return 1;
    return result_ast_node(self, res);
}

static ast_node *parse_lvalue(parser *self) {
    ast_node *ident = null;

    ast_node *expr  = parse_expression(self, INT_MIN);
    if (!expr) return null;

    if (ast_node_is_symbol(expr)) ident = expr;
    else if (ast_binary_op == expr->tag) {
        char const *op = str_cstr(&expr->binary_op.op->symbol.name);
        if (is_struct_access_operator(op)) ident = expr;
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
    // x := newval
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    if (a_try(self, a_colon_equal)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a         = ast_node_create(self->ast_arena, ast_assignment);
    a->assignment.name  = lval;
    a->assignment.value = val;
    return result_ast_node(self, a);
}

static int a_simple_assignment(parser *self) {
    // x = val (for type literals)
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_equal_sign)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a         = ast_node_create(self->ast_arena, ast_assignment);
    a->assignment.name  = name;
    a->assignment.value = val;
    return result_ast_node(self, a);
}

static int a_assignment(parser *self) {
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    if (a_try(self, a_equal_sign)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (a_body_element(self)) break;
        array_push(exprs, self->result);
    }

    ast_node *body  = create_body(self, exprs);

    ast_node *a     = ast_node_create(self->ast_arena, ast_let_in);
    a->let_in.name  = lval;
    a->let_in.value = val;
    a->let_in.body  = body;
    return result_ast_node(self, a);
}

static int a_return_statement(parser *self) {
    if (a_try_s(self, the_symbol, "return")) return 1;

    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return 1;

    ast_node *r                   = ast_node_create(self->ast_arena, ast_return);
    r->return_.value              = value;
    r->return_.is_break_statement = 0;
    return result_ast_node(self, r);
}

static int a_break_statement(parser *self) {
    if (a_try_s(self, the_symbol, "break")) return 1;

    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return 1;

    ast_node *r                   = ast_node_create(self->ast_arena, ast_return);
    r->return_.value              = value;
    r->return_.is_break_statement = 1;
    return result_ast_node(self, r);
}

static int a_continue_statement(parser *self) {
    if (a_try_s(self, the_symbol, "continue")) return 1;

    ast_node *r = ast_node_create(self->ast_arena, ast_continue);
    return result_ast_node(self, r);
}

static int a_while_statement(parser *self) {
    if (a_try_s(self, the_symbol, "while")) return 1;
    if (a_try(self, a_open_round)) return 1;

    ast_node *condition = parse_expression(self, INT_MIN);
    if (!condition) return 1;
    if (a_try(self, a_close_round)) return 1;

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (a_try(self, a_body_element)) return 1;
        array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    ast_node *body      = create_body(self, exprs);

    ast_node *r         = ast_node_create(self->ast_arena, ast_while);
    r->while_.condition = condition;
    r->while_.body      = body;
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
    ast_node *body         = ast_node_create(self->ast_arena, ast_body);
    body->body.expressions = (ast_node_sized)sized_all(exprs);
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
        if (a_body_element(self)) return 1;
        array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs);

    ast_node *let  = ast_node_create(self->ast_arena, ast_let);
    set_node_parameters(self, let, &params);
    let->let.name = name;
    let->let.body = body;

    result_ast_node(self, let);

    return 0;
}

static int toplevel_assign(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *name = self->result;
    if (a_try(self, a_equal_sign)) return 1;
    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return 1;

    ast_node *n     = ast_node_create(self->ast_arena, ast_let_in);
    n->let_in.body  = null;
    n->let_in.name  = name;
    n->let_in.value = value;
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
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_param)) return 1;
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
    ast_node *tup         = ast_node_create(self->ast_arena, ast_tuple);
    tup->tuple.elements   = params.v;
    tup->tuple.n_elements = params.size;

    // make arrow
    ast_node *arrow    = ast_node_create(self->ast_arena, ast_arrow);
    arrow->arrow.left  = tup;
    arrow->arrow.right = ann;

    // attach to name
    name->symbol.annotation = arrow;

    return result_ast_node(self, name);
}

static int toplevel_struct(parser *self) {

    if (a_try(self, a_colon)) return 1;

    if (a_try(self, a_type_identifier)) return 1;
    ast_node *type_ident = self->result;

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array fields = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_param)) return 1;
        array_push(fields, self->result);
    }
    array_shrink(fields);

    ast_node *r = ast_node_create(self->ast_arena, ast_user_type_definition);
    if (ast_node_is_symbol(type_ident)) {
        r->user_type_def.n_type_arguments = 0;
        r->user_type_def.type_arguments   = null;
        r->user_type_def.name             = type_ident;
    } else if (ast_node_is_named_application(type_ident)) {
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

    return result_ast_node(self, r);
}

static int toplevel(parser *self) {

    self->error.tag = tl_err_ok;

    if (0 == a_try(self, toplevel_struct)) return 0;
    if (0 == a_try(self, toplevel_defun)) return 0;
    if (0 == a_try(self, toplevel_assign)) return 0;
    if (0 == a_try(self, toplevel_forward)) return 0;

    self->error.tag = tl_err_expected_toplevel;
    return 1;
}

int parser_next(parser *self) {
    if (!self->tokenizer) {

        if (self->files_index >= self->files.size) {
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
        char const *file = self->files.v[self->files_index++];
        file_read(self->file_arena, file, (char **)&self->current_file_data.v,
                  &self->current_file_data.size);

        self->tokenizer = tokenizer_create(self->parent_alloc, self->current_file_data, file);
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
        log(p, "parse_all: parsed node %s", str_cstr(&str));

        array_push(*out, node);
    }

    if (is_eof(p)) return 0;

    return res;
}

int parser_parse_all_verbose(parser *p, ast_node_array *out) {
    p->verbose = 1;

    log(p, "begin parse");
    int res = parser_parse_all(p, out);
    log(p, "end parse status %i", res);

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

void log(struct parser *self, char const *restrict fmt, ...) {
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
