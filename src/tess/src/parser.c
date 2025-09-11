#include "parser.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "file.h"
#include "hashmap.h"
#include "string_t.h"
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

    c_string_csized        files;
    u16                    files_index;
    char_csized            current_file_data;

    tokenizer             *tokenizer;

    ast_node              *result;

    hashmap               *forwards;

    token_array            tokens;

    struct parser_error    error;
    struct tokenizer_error tokenizer_error;
    struct token           token;

    u32                    next_nil_name;

    int                    verbose;
    int                    indent_level;
};

typedef int (*parse_fun)(parser *);
typedef int (*parse_fun_s)(parser *, char const *);

// -- overview --

static int           begin_end_expression(parser *);
static int           expression(parser *);
static int           expression_let(parser *);
static int           forward_declaration(parser *);
static int           function_application(parser *);
static int           function_argument(parser *);
static int           function_declaration(parser *);
static int           function_definition(parser *);
static int           grouped_expression(parser *);
static int           if_then_else(parser *);
static int           infix_operand(parser *);
static int           infix_operation(parser *);
static int           lambda_declaration(parser *);
static int           lambda_function(parser *);
static int           match_declaration(parser *);
static int           simple_declaration(parser *);
static int           struct_declaration(parser *);
static int           toplevel(parser *);
static int           toplevel_let(parser *);
static int           tuple_expression(parser *);

static int           continue_let_in(parser *, ast_node *);

static int           result_ast(parser *, ast_tag);
static int           result_ast_bool(parser *, int);
static int           result_ast_f64(parser *, f64);
static int           result_ast_i64(parser *, i64);
static int           result_ast_node(parser *, ast_node *);
static int           result_ast_str(parser *, ast_tag, char const *s);
static int           result_ast_u64(parser *, u64);

static int           is_arithmetic_operator(char const *);
static int           is_eof(parser *);
static int           is_relational_operator(char const *);
static int           is_reserved(char const *);
static int           is_start_of_expression(char const *);

nodiscard static int a_try(parser *, parse_fun);
nodiscard static int a_try_s(parser *, parse_fun_s, char const *);
nodiscard static int a_try_special(parser *, parse_fun);

static int           eat_comments(parser *);
static int           next_token(parser *);

static int           a_address_of(parser *);
static int           a_ampersand(parser *);
static int           a_arrow(parser *);
static int           a_assignment(parser *);
static int           a_bool(parser *);
static int           a_close_round(parser *);
static int           a_colon(parser *);
static int           a_colon_equal(parser *);
static int           a_comma(parser *);
static int           a_dereference(parser *);
static int           a_dot(parser *);
static int           a_end_of_block(parser *);
static int           a_end_of_expression(parser *);
static int           a_equal_sign(parser *);
static int           a_field_access(parser *);
static int           a_field_setter(parser *);
static int           a_identifier(parser *);
static int           a_identifier_typed(parser *);
static int           a_infix_operator(parser *);
static int           a_labelled_tuple(parser *);
static int           a_value(parser *);
static int           a_nil(parser *);
static int           a_number(parser *);
static int           a_open_round(parser *);
static int           a_star(parser *);
static int           a_string(parser *);
static int           a_type_annotation(parser *);
static int           a_type_identifier(parser *);
static int           the_symbol(parser *, char const *const);

static char         *make_nil_name(parser *);
static int           string_to_number(parser *, char const *const);

static int           has_error(parser *);
static void          tokens_push_back(struct parser *, struct token *);
static void          tokens_shrink(struct parser *, u32);
static int           too_many_arguments(parser *);
static void log(struct parser *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, char_csized preamble, c_string_csized files) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));

    alloc_zero(self);
    self->parent_alloc           = alloc;
    self->file_arena             = arena_create(self->parent_alloc, 64 * 1024);
    self->tokens_arena           = arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->ast_arena              = arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->transient              = arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->files                  = files;
    self->files_index            = 0;
    self->current_file_data.v    = null;
    self->current_file_data.size = 0;
    self->verbose                = 0;
    self->indent_level           = 0;

    self->forwards               = map_create(self->parent_alloc, sizeof(ast_node *));

    self->tokenizer              = tokenizer_create(alloc, preamble, "std_preamble");
    self->tokens                 = (token_array){.alloc = self->tokens_arena};

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

    map_destroy(&(*self)->forwards);

    // tokenizer
    if ((*self)->tokenizer) tokenizer_destroy(&(*self)->tokenizer);

    // arena
    arena_destroy((*self)->parent_alloc, &(*self)->transient);
    arena_destroy((*self)->parent_alloc, &(*self)->ast_arena);
    arena_destroy((*self)->parent_alloc, &(*self)->tokens_arena);
    arena_destroy((*self)->parent_alloc, &(*self)->file_arena);
    alloc_free((*self)->parent_alloc, *self);
    *self = null;
}

// -- parser --

// result_* functions construct a new ast_node and add it to the pool.
// The parser then no longer has a valid copy of the actual ast_node,
// it being replaced by a handle to an entry in the pool.

static int result_ast(parser *p, ast_tag tag) {
    p->result = ast_node_create(p->ast_arena, tag);
    return 0;
}

static int result_ast_i64(parser *p, i64 val) {
    p->result          = ast_node_create(p->ast_arena, ast_i64);
    p->result->i64.val = val;
    return 0;
}

static int result_ast_u64(parser *p, u64 val) {
    p->result          = ast_node_create(p->ast_arena, ast_u64);
    p->result->u64.val = val;
    return 0;
}

static int result_ast_f64(parser *p, f64 val) {
    p->result          = ast_node_create(p->ast_arena, ast_f64);
    p->result->f64.val = val;
    return 0;
}

static int result_ast_bool(parser *p, int val) {
    p->result            = ast_node_create(p->ast_arena, ast_bool);
    p->result->bool_.val = val;
    return 0;
}

static int result_ast_str(parser *p, ast_tag tag, char const *s) {
    p->result                         = ast_node_create(p->ast_arena, tag);
    p->result->symbol.name            = string_t_init(p->ast_arena, s);
    p->result->symbol.original        = string_t_init_empty();
    p->result->symbol.annotation      = null;
    p->result->symbol.annotation_type = null;

    // syms and strs use same union

    return 0;
}

static int result_ast_node(parser *p, ast_node *node) {
    p->result  = node;
    node->file = p->error.file;
    node->line = p->error.line;

    log(p, "result: %s", ast_node_to_string(p->transient, node));
    return 0;
}

static int is_reserved(char const *s) {
    static char const *strings[] = {
      "begin", "else", "end", "false", "fun", "if", "in", "let", "then", "true", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

static int is_start_of_expression(char const *s) {
    static char const *strings[] = {
      "begin", "fun", "if", "in", "let", "struct", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

static int is_arithmetic_operator(char const *s) {
    static char const *strings[] = {
      "+", "-", "*", "/", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

static int is_relational_operator(char const *s) {
    static char const *strings[] = {
      "<", "<=", "==", "<>", ">=", ">", null,
    };
    char const **it = strings;
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
            p->error.tag  = tl_err_tokenizer_error;
            return 1;
        }

        token_tag const tag = p->token.tag;
        if (tok_comment == tag) {
            continue;
        } else {
            tokenizer_put_back(p->tokenizer, &p->token, 1);
            return result_ast(p, ast_eof);
        }
    }
}

static int next_token(parser *p) {
    if (eat_comments(p)) return 1;

    while (1) {

        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.tag  = tl_err_tokenizer_error;
            return 1;
        }

        // always update file/line
        p->error.file = p->token.file;
        p->error.line = p->token.line;

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

nodiscard static int a_try_special(parser *p, parse_fun fun) {
    // if fun returns 2, tokens are restored as in the failure case,
    // but this function returns success.
    u32 const save_toks = p->tokens.size;
    int const res       = fun(p);
    if (res) {
        if (p->tokens.size > save_toks) {
            char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
            alloc_free(p->transient, str);

            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }
        return res == 2 ? 0 : 1;
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

static int a_dot(parser *p) {
    if (next_token(p)) return 1;

    if (tok_dot == p->token.tag) return result_ast_str(p, ast_symbol, ".");

    p->error.tag = tl_err_expected_dot;
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

static int a_end_of_expression(parser *p) {

    if (next_token(p)) {
        if (is_eof(p)) return result_ast_str(p, ast_symbol, ";");
        return 1;
    }

    log(p, "end_of_expression token: %s", token_to_string(p->transient, &p->token));

    switch (p->token.tag) {
    case tok_semicolon: return result_ast_str(p, ast_symbol, ";");

    case tok_close_round:
        // signal special failure so the token gets put back, but use a magic
        // error code so that consumers of end_of_expression can treat it as a success
        return 2;

    case tok_symbol:

        if (is_start_of_expression(p->token.s)) return 2;
        break;

    case tok_comma:
    case tok_dot:
    case tok_colon:
    case tok_colon_equal:
    case tok_ampersand:
    case tok_star:
    case tok_arrow:
    case tok_open_round:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_comment:     break;
    }

    p->error.tag = tl_err_unfinished_expression;
    return 1;
}

static int a_address_of(parser *self) {

    if (a_try(self, a_ampersand)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin address_of");

    // FIXME for now only address of an identifier
    if (a_try(self, a_identifier)) {
        self->error.tag = tl_err_expected_addressable;
        return 1;
    }

    ast_node *target        = self->result;

    ast_node *node          = ast_node_create(self->ast_arena, ast_address_of);
    node->address_of.target = target;
    return result_ast_node(self, node);
}

static int a_dereference(parser *self) {

    // FIXME for now only address of an identifier

    if (a_try(self, a_identifier)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    ast_node *target = self->result;

    if (a_try(self, a_dot)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    if (a_try(self, a_star)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin dereference");

    ast_node *node           = ast_node_create(self->ast_arena, ast_dereference);
    node->dereference.target = target;
    return result_ast_node(self, node);
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

static int a_type_identifier(parser *self) {
    // TODO rename this because it's no longer just an identifier

    // * int -> int ==> (*int) -> int, not *(int -> int)
    // * has higher precedence than ->

    int is_pointer = 0;
    if (0 == a_try(self, a_star)) {
        is_pointer = 1;
    }

    // TODO so much duplication in this function...

    if (0 == a_try(self, a_identifier)) {
        ast_node *left  = self->result;
        ast_node *right = null;

        // followed by arrow?
        if (0 == a_try(self, a_arrow)) {
            if (a_try(self, a_type_identifier)) {
                self->error.tag = tl_err_expected_type;
                return 1;
            }
            right             = self->result;

            ast_node *node    = ast_node_create(self->ast_arena, ast_arrow);
            node->arrow.left  = left;
            node->arrow.right = right;

            if (is_pointer) {
                // TODO reusing this ast type to mean something different
                ast_node *ptr          = ast_node_create(self->ast_arena, ast_address_of);
                ptr->address_of.target = node;
                node                   = ptr;
            }
            return result_ast_node(self, node);
        }

        if (is_pointer) {
            // TODO reusing this ast type to mean something different
            ast_node *ptr          = ast_node_create(self->ast_arena, ast_address_of);
            ptr->address_of.target = left;
            left                   = ptr;
        }
        self->result = left;
        return 0;
    }

    if (0 == a_try(self, tuple_expression)) {
        ast_node *left  = self->result;
        ast_node *right = null;

        // followed by arrow?
        if (0 == a_try(self, a_arrow)) {
            if (a_try(self, a_type_identifier)) {
                self->error.tag = tl_err_expected_type;
                return 1;
            }
            right = self->result;
        }

        if (right) {
            ast_node *node    = ast_node_create(self->ast_arena, ast_arrow);
            node->arrow.left  = left;
            node->arrow.right = right;

            if (is_pointer) {
                // TODO reusing this ast type to mean something different
                ast_node *ptr          = ast_node_create(self->ast_arena, ast_address_of);
                ptr->address_of.target = node;
                node                   = ptr;
            }

            return result_ast_node(self, node);
        }

        if (is_pointer) {
            // TODO reusing this ast type to mean something different
            ast_node *ptr          = ast_node_create(self->ast_arena, ast_address_of);
            ptr->address_of.target = left;
            left                   = ptr;
        }
        self->result = left;
        return 0;
    }

    return 1;
}

static int a_type_annotation(parser *self) {
    if (0 == a_try(self, a_colon)) {
        return a_try(self, a_type_identifier);
    }

    self->error.tag = tl_err_expected_colon;
    return 1;
}

static int a_identifier_typed(parser *self) {
    // either an identifier or a group: (a : int)
    if (0 == a_try(self, a_identifier)) return 0;

    if (a_try(self, a_open_round)) return 1;

    if (a_try(self, a_identifier)) return 1;
    ast_node *name       = self->result;

    ast_node *annotation = null;
    if (0 == a_try(self, a_type_annotation)) annotation = self->result;

    if (a_try(self, a_close_round)) return 1;

    ast_node *node        = ast_node_create(self->ast_arena, ast_symbol);
    node->symbol.name     = name->symbol.name;
    node->symbol.original = string_t_init_empty();
    if (annotation) node->symbol.annotation = annotation;
    else node->symbol.annotation = null;
    node->symbol.annotation_type = null;
    return result_ast_node(self, node);
}

static int forward_declaration(parser *self) {
    // sym : type

    if (a_try(self, a_identifier)) return 1;
    ast_node *sym = self->result;

    if (a_try(self, a_type_annotation)) return 1;
    ast_node *ann               = self->result;

    sym->symbol.original        = string_t_init_empty();
    sym->symbol.annotation      = ann;
    sym->symbol.annotation_type = null;
    return result_ast_node(self, sym);
}

static int a_infix_operator(parser *p) {
    // + - * /, relationals: < <= == <> >= >
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {

        if (is_arithmetic_operator(p->token.s) || is_relational_operator(p->token.s))
            return result_ast_str(p, ast_symbol, p->token.s);
    }

    p->error.tag = tl_err_expected_operator;
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

static int a_star(parser *p) {
    if (next_token(p)) return 1;

    if (tok_star == p->token.tag) return result_ast_str(p, ast_symbol, "*");

    p->error.tag = tl_err_expected_star;
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
    switch (string_t_parse_number(in, &i, &u, &d)) {
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

static int a_labelled_tuple(parser *self) {
    // (label1 = expr1, ...)
    if (a_try(self, a_open_round)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    ast_node_array assignments = {.alloc = self->ast_arena};

    if (a_try(self, a_assignment)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin labelled tuple");

    array_push(assignments, &self->result);

    while (1) {

        if (0 == a_try(self, a_close_round)) {

            if (assignments.size > 0xff) {
                too_many_arguments(self);
                goto error;
            }

            ast_node *node = ast_node_create(self->ast_arena, ast_labelled_tuple);
            array_shrink(assignments);

            node->array.n              = (u8)assignments.size;
            node->array.nodes          = assignments.v;
            node->labelled_tuple.flags = 0;

            return result_ast_node(self, node);
        }

        if (a_try(self, a_comma)) {
            self->error.tag = tl_err_expected_comma;
            goto error;
        }

        // next assignment expression
        if (0 == a_try(self, a_assignment)) array_push(assignments, &self->result);

        // loop to check for close round
    }

error:
    array_free(assignments);
    return 1;
}

static int a_value(parser *self) {
    if (0 == a_try(self, a_nil)) return 0;
    if (0 == a_try(self, a_field_setter)) return 0; // before field_access
    if (0 == a_try(self, a_field_access)) return 0;
    if (0 == a_try(self, a_dereference)) return 0; // before identifier
    if (0 == a_try(self, a_identifier)) return 0;
    if (0 == a_try(self, a_address_of)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    if (0 == a_try(self, a_string)) return 0;
    if (0 == a_try(self, a_bool)) return 0;

    self->error.tag = tl_err_expected_value;
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

static int a_ampersand(parser *p) {
    if (next_token(p)) return 1;

    if (tok_ampersand == p->token.tag) return result_ast_str(p, ast_symbol, "&");

    p->error.tag = tl_err_expected_ampersand;
    return 1;
}

static int a_arrow(parser *p) {
    if (next_token(p)) return 1;

    if (tok_arrow == p->token.tag) return result_ast_str(p, ast_symbol, "->");

    p->error.tag = tl_err_expected_arrow;
    return 1;
}

static int a_assignment(parser *self) {
    // ident1 = expr1

    if (a_try(self, a_identifier)) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    ast_node *name = self->result;

    if (a_try(self, a_equal_sign)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    if (a_try(self, expression)) {
        self->error.tag = tl_err_expected_assignment_value;
        return 1;
    }
    ast_node *value        = self->result;

    ast_node *node         = ast_node_create(self->ast_arena, ast_assignment);
    node->assignment.name  = name;
    node->assignment.value = value;
    return result_ast_node(self, node);
}

static int a_nil(parser *self) {

    if ((0 == a_open_round(self)) && (0 == a_close_round(self))) return result_ast(self, ast_nil);

    self->error.tag = tl_err_expected_nil;
    return 1;
}

static int a_end_of_block(parser *self) {

    if (next_token(self)) {
        if (is_eof(self)) return result_ast_str(self, ast_symbol, "end");
        log(self, "end_of_block: other error");
        return 1;
    }
    if (tok_symbol == self->token.tag) {
        if (0 == strcmp("end", self->token.s)) return result_ast_str(self, ast_symbol, "end");
        if (is_start_of_expression(self->token.s)) return 2;
    }

    self->error.tag = tl_err_expected_end_of_block;
    return 1;
}

static int a_field_access(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *variable = self->result;

    if (a_try(self, a_dot)) return 1;

    log(self, "begin field access");

    if (a_try(self, a_identifier)) return 1;
    ast_node                 *field = self->result;

    ast_node                 *node  = ast_node_create(self->ast_arena, ast_user_type_get);
    struct ast_user_type_get *v     = ast_node_utg(node);
    v->struct_name                  = variable;
    v->field_name                   = field;
    return result_ast_node(self, node);
}

static int a_field_setter(parser *self) {
    if (a_try(self, a_field_access)) return 1;

    ast_node *user_field = self->result;
    if (a_try(self, a_colon_equal)) return 1;

    log(self, "begin field setter");

    if (a_try(self, expression)) return 1;
    ast_node                 *value = self->result;

    ast_node                 *node  = ast_node_create(self->ast_arena, ast_user_type_set);
    struct ast_user_type_set *v     = ast_node_uts(node);
    struct ast_user_type_get *vget  = ast_node_utg(user_field);
    v->struct_name                  = vget->struct_name;
    v->field_name                   = vget->field_name;
    v->value                        = value;
    return result_ast_node(self, node);
}

static int struct_declaration(parser *self) {
    //     struct name = ... end
    if (a_try_s(self, the_symbol, "struct")) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    log(self, "struct begin");
    self->indent_level++;

    if (a_try(self, a_identifier)) {
        self->error.tag = tl_err_expected_struct_name;
        goto error;
    }

    ast_node      *name        = self->result;

    ast_node_array field_names = {.alloc = self->ast_arena};
    ast_node_array field_types = {.alloc = self->ast_arena};

    if (a_try(self, a_equal_sign)) {
        self->error.tag = tl_err_expected_equal_sign;
        goto error;
    }

    // check for empty struct
    if (0 == a_try_special(self, a_end_of_block)) {
        ast_node *node                        = ast_node_create(self->ast_arena, ast_user_type_definition);
        node->user_type_def.name              = name;
        node->user_type_def.field_annotations = null;
        node->user_type_def.field_names       = null;
        node->user_type_def.field_types       = null;
        node->user_type_def.n_fields          = 0;
        result_ast_node(self, node);
        goto success;
    }

    // accumulate names and types until end of block is seen
    while (1) {

        if (0 == a_try(self, a_identifier)) {
            ast_node *field_name = self->result;
            log(self, "struct_declaration: field %s", ast_node_to_string(self->transient, field_name));

            if (a_try(self, a_colon)) {
                self->error.tag = tl_err_expected_colon;
                goto error;
            }

            if (a_try(self, a_type_identifier)) {
                self->error.tag = tl_err_expected_type;
                goto error;
            }
            ast_node *type = self->result;
            log(self, "struct_declaration: type %s", ast_node_to_string(self->transient, type));

            array_push(field_names, &field_name);
            array_push(field_types, &type);

            if (a_try_special(self, a_end_of_expression)) {
                // accept an 'end' instead of final semicolon
                if (0 == a_try_special(self, a_end_of_block)) goto end_of_block;

                self->error.tag = tl_err_expected_end_of_expression;
                goto error; // expect ; after field
            }

            continue;
        }

        if (0 == a_try_special(self, a_end_of_block)) {

        end_of_block: {
            ast_node *node           = ast_node_create(self->ast_arena, ast_user_type_definition);
            node->user_type_def.name = null;
            node->user_type_def.field_annotations = null;
            node->user_type_def.field_names       = null;
            node->user_type_def.field_types       = null;
            node->user_type_def.n_fields          = 0;

            node->user_type_def.name              = name;

            array_shrink(field_names);
            node->user_type_def.field_names = field_names.v;

            if (field_names.size > 0xff) {
                too_many_arguments(self);
                goto error;
            }

            node->user_type_def.n_fields = (u8)field_names.size;

            array_shrink(field_types);
            node->user_type_def.field_annotations = field_types.v;

            result_ast_node(self, node);
            goto success;
        }
        }

        // anything else is an error
        self->error.tag = tl_err_expected_end_of_block;
        return 1;
    }

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int function_declaration(parser *self) {
    // f a b c... = : only symbols allowed, terminated by =.

    // annotated: f a b c : (int,int,int) -> int = ...

    // check annotations in self->forwards

    if (a_try(self, a_identifier)) return 1;

    ast_node *const name       = self->result; // function name
    char const     *name_str   = ast_node_name_string(name);

    ast_node_array  parameters = {.alloc = self->ast_arena};
    ast_node       *annotation = null;

    // check: f () declares function with no parameters
    if (0 == a_try(self, a_nil)) {

        // FIXME does not parse an annotation in this case

        // next token must be equal sign
        if (0 == a_try(self, a_equal_sign)) {
            ast_node *node                  = ast_node_create(self->ast_arena, ast_function_declaration);
            node->function_declaration.name = name;
            node->function_declaration.n_parameters = 0;
            node->function_declaration.parameters   = null;
            return result_ast_node(self, node);
        }

        return 1;
    }

    // must have at least one parameter
    if (a_try(self, a_identifier_typed)) return 1;
    array_push(parameters, &self->result);

    // check forward declarations.
    ast_node **forward = map_get(self->forwards, name_str, strlen(name_str));
    if (forward) {
        assert(ast_symbol == (*forward)->tag);
        annotation = (*forward)->symbol.annotation;
    }

    int require_equal_sign = 0;

    // accumulate identifiers as parameters until equal sign is seen
    while (1) {
        if (0 == a_try(self, a_identifier_typed)) {
            array_push(parameters, &self->result);
            continue;
        }

        if (0 == a_try(self, a_type_annotation)) {
            if (annotation) {
                // error, cannot provide inline annotation to a forward declared function
                self->error.tag = tl_err_unexpected_inline_annotation;
                return 1;
            }
            annotation         = self->result;
            require_equal_sign = 1;
        }

        if (0 == a_try(self, a_equal_sign)) {

            ast_node *node                  = ast_node_create(self->ast_arena, ast_function_declaration);
            node->function_declaration.name = name;
            node->function_declaration.n_parameters = 0;
            node->function_declaration.parameters   = null;

            array_shrink(parameters);
            node->array.nodes = parameters.v;
            if (parameters.size > 0xff) return too_many_arguments(self);
            node->array.n = (u8)parameters.size;

            // attach annotation to function name
            assert(ast_symbol == name->tag);
            name->symbol.annotation = annotation;

            log(self, "function_declaration: returning %s", ast_node_to_string(self->transient, node));
            return result_ast_node(self, node);
        } else if (require_equal_sign) {
            self->error.tag = tl_err_expected_equal_sign;
            return 1;
        }

        // anything else is an error
        self->error.tag = tl_err_expected_argument;
        return 1;
    }
}

static int lambda_declaration(parser *self) {
    // a b c... -> : only symbols allowed, terminated by ->

    ast_node_array parameters = {.alloc = self->ast_arena};

    // accumulate identifiers as parameters until an arrow is seen
    while (1) {
        if (0 == a_try(self, a_identifier_typed)) {
            array_push(parameters, &self->result);
            continue;
        }

        if (0 == a_try(self, a_arrow)) {
            ast_node *node = ast_node_create(self->ast_arena, ast_lambda_declaration);
            node->lambda_declaration.n_parameters = 0;
            node->lambda_declaration.parameters   = null;

            array_shrink(parameters);
            node->array.n     = (u8)parameters.size;
            node->array.nodes = parameters.v;

            if (parameters.size > 0xff) {
                too_many_arguments(self);
                array_free(parameters);
                return 1;
            }

            return result_ast_node(self, node);
        }

        // anything else is an error
        self->error.tag = tl_err_unfinished_lambda_declaration;
        return 1;
    }
}

static int function_definition(parser *p) {
    return expression(p);
}

static void repair_single_nil_argument(ast_node_array *arguments) {
    // detect special case of a single nil argument and replace with
    // empty argument list
    if (arguments->size == 1 && ast_nil == arguments->v[0]->tag) arguments->size = 0;
}

static int function_application(parser *self) {
    // f a b c ..., terminated by semicolon or one_newline or two_newline

    if (a_try(self, a_identifier)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    ast_node *const name = self->result;
    assert(ast_symbol == name->tag);

    ast_node_array arguments = {.alloc = self->ast_arena};

    // must have at least one argument
    if (a_try(self, function_argument)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin function application");
    self->indent_level++;

    array_push(arguments, &self->result);

    while (1) {
        if (0 == a_try(self, function_argument)) {
            array_push(arguments, &self->result);
            continue;
        }

        if (0 == a_try_special(self, a_end_of_expression)) {
            // 2: "fails" due to close_round, which must not be
            // consumed, so that grouped_expression catches it.

            // catch intrinsic names here
            char const *name_str = ast_node_name_string(name);
            if (0 == strncmp("_tl_", name_str, 4)) {
                ast_node *node = ast_node_create(self->ast_arena, ast_intrinsic_application);
                node->intrinsic_application.arguments   = null;
                node->intrinsic_application.n_arguments = 0;
                node->intrinsic_application.name        = name;

                // TODO remove duplication with the other branch

                array_shrink(arguments);
                repair_single_nil_argument(&arguments);
                node->array.n     = (u8)arguments.size;
                node->array.nodes = arguments.v;
                if (arguments.size > 0xff) {
                    too_many_arguments(self);
                    goto error;
                }

                log(self, "function_application: got intrinsic %s",
                    ast_node_to_string(self->transient, node));
                result_ast_node(self, node);
                goto success;

            } else {
                ast_node *node = ast_node_create(self->ast_arena, ast_named_function_application);
                node->named_application.arguments   = null;
                node->named_application.n_arguments = 0;
                node->named_application.specialized = null;
                node->named_application.name        = name;

                array_shrink(arguments);
                repair_single_nil_argument(&arguments);
                node->array.n     = (u8)arguments.size;
                node->array.nodes = arguments.v;
                if (arguments.size > 0xff) {
                    too_many_arguments(self);
                    goto error;
                }

                log(self, "function_application: got %s", ast_node_to_string(self->transient, node));
                result_ast_node(self, node);
                goto success;
            }
        }

        self->error.tag = tl_err_expected_function_application_argument;
        goto error;
    }

success:
    self->indent_level--;
    return 0;
error:
    self->indent_level--;
    return 1;
}

//

static int function_argument(parser *p) {

    p->indent_level++;
    log(p, "try function_argument");

    p->indent_level++;
    if (0 == a_try(p, a_value)) goto cleanup;
    if (0 == a_try(p, grouped_expression)) goto cleanup;
    p->indent_level--;

    p->indent_level--;
    return 1;

cleanup:
    p->indent_level--;
    return 0;
}

static int if_then_else(parser *self) {

    ast_node *cond, *yes, *no;

    if (a_try_s(self, the_symbol, "if")) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin if-then-else");
    self->indent_level++;

    if (expression(self)) {
        self->error.tag = tl_err_expected_if_condition;
        goto error;
    }
    cond = self->result;

    if (a_try_s(self, the_symbol, "then")) {
        self->error.tag = tl_err_expected_keyword_then;
        goto error;
    }

    if (expression(self)) {
        self->error.tag = tl_err_expected_if_then_arm;
        goto error;
    }
    yes = self->result;

    if (a_try_s(self, the_symbol, "else")) {
        self->error.tag = tl_err_expected_keyword_else;
        goto error;
    }

    if (expression(self)) {
        self->error.tag = tl_err_expected_if_else_arm;
        goto error;
    }
    no                           = self->result;

    ast_node *node               = ast_node_create(self->ast_arena, ast_if_then_else);
    node->if_then_else.condition = cond;
    node->if_then_else.yes       = yes;
    node->if_then_else.no        = no;
    result_ast_node(self, node);
    goto success;

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int infix_operand(parser *p) {
    return a_try(p, function_argument);
}

static int infix_operation(parser *self) {
    // a * b

    if (infix_operand(self)) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    ast_node *const lhs = self->result;

    if (a_try(self, a_infix_operator)) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    ast_node *op_node = self->result;

    log(self, "begin infix");
    self->indent_level++;

    ast_operator op;
    if (string_to_ast_operator(ast_node_name_string(op_node), &op)) {
        self->error.tag = tl_err_expected_operator;
        goto error;
    }

    if (infix_operand(self)) {
        self->error.tag = tl_err_expected_infix_operand;
        goto error;
    }

    ast_node *const rhs  = self->result;

    ast_node       *node = ast_node_create(self->ast_arena, ast_infix);
    node->infix.left     = lhs;
    node->infix.right    = rhs;
    node->infix.op       = op;

    result_ast_node(self, node);

    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int lambda_function(parser *self) {
    // fun a b c... -> rhs

    if (a_try_s(self, the_symbol, "fun")) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin lambda function");
    self->indent_level++;

    if (a_try(self, lambda_declaration)) {
        self->error.tag = tl_err_expected_lambda;
        goto error;
    }
    ast_node *decl = self->result;

    if (a_try(self, function_definition)) {
        if (self->error.tag == tl_err_ok) self->error.tag = tl_err_expected_function_definition;
        goto error;
    }
    ast_node *defn = self->result;

    // require end keyword to end parse of lambda function definition: do not use try_special, as we really
    // want the actual 'end' keyword
    if (a_try(self, a_end_of_block)) {
        self->error.tag = tl_err_expected_end_of_block;
        goto error;
    }

    ast_node *node                     = ast_node_create(self->ast_arena, ast_lambda_function);
    node->lambda_function.parameters   = null;
    node->lambda_function.n_parameters = 0;
    node->lambda_function.body         = defn;

    // move the vector from the function_declaration node to the new ast node
    node->array.nodes = decl->array.nodes;
    node->array.n     = decl->array.n;
    decl->array.nodes = null;
    decl->array.n     = 0;

    result_ast_node(self, node);
    goto success;

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int lambda_function_application(parser *self) {

    if (a_try(self, lambda_function)) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    ast_node *lambda = self->result;

    // there must be at least one argument
    ast_node_array arguments = {.alloc = self->ast_arena};

    if (a_try(self, function_argument)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin lambda function application");
    self->indent_level++;

    array_push(arguments, &self->result);

    while (1) {
        if (0 == a_try(self, function_argument)) {
            array_push(arguments, &self->result);
            continue;
        }

        if (0 == a_try_special(self, a_end_of_expression)) {
            ast_node *node = ast_node_create(self->ast_arena, ast_lambda_function_application);
            node->lambda_application.arguments   = null;
            node->lambda_application.n_arguments = 0;
            node->lambda_application.lambda      = lambda;

            array_shrink(arguments);
            node->array.n     = (u8)arguments.size;
            node->array.nodes = arguments.v;
            if (arguments.size > 0xff) {
                too_many_arguments(self);
                goto error;
            }

            result_ast_node(self, node);
            goto success;
        }

        // anything else is an error
        self->error.tag = tl_err_expected_lambda_function_application_argument;
        goto error;
    }

success:
    self->indent_level--;
    return 0;
error:
    self->indent_level--;
    return 1;
}

static char *make_nil_name(parser *p) {
#define fmt "_gen_nil_%u_"
    int len = snprintf(null, 0, fmt, p->next_nil_name) + 1;
    if (len < 0) fatal("make_nil_name");
    char *out = alloc_malloc(p->ast_arena, (u32)len);
    snprintf(out, (u32)len, fmt, p->next_nil_name++);
    return out;
#undef fmt
}

static int match_declaration(parser *self) {
    // (a = x, ...) = ...

    if (a_try(self, a_labelled_tuple)) return 1;

    ast_node *lt = self->result;

    if (a_try(self, a_equal_sign)) return 1;

    return result_ast_node(self, lt);
}

static int simple_declaration(parser *p) {
    // a = ... a single identifier or nil, optionally typed, followed by an
    // equal sign
    if (0 == a_try(p, a_nil)) {
        // need to match the equal sign too
        if (a_try(p, a_equal_sign)) return 1;

        return result_ast_str(p, ast_symbol, make_nil_name(p));
    }

    if (a_try(p, a_identifier_typed)) return 1;
    ast_node *sym = p->result;

    if (a_try(p, a_equal_sign)) return 1;

    return result_ast_node(p, sym);
}

// -- public read-only portion of struct --
// FIXME put this elsewhere
struct tokenizer {
    allocator  *parent;
    allocator  *strings;
    char const *input;
    size_t      input_len;
    size_t      pos;
};

static int tuple_expression(parser *self) {

    if (a_try(self, a_open_round)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    ast_node_array elements = {.alloc = self->ast_arena}; // will be moved to node on success

    // first, expect an expression, which must be followed by a comma
    // then, zero or more expressions before a close round. So (expr,)
    // is a valid tuple.
    if (a_try(self, expression)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    array_push(elements, &self->result);

    if (a_try(self, a_comma)) goto cleanup;

    log(self, "begin tuple");

    int count = 0;
    while (1) {

        if (0 == a_try(self, a_close_round)) {
            if (elements.size > 0xff) {
                too_many_arguments(self);
                goto cleanup;
            }

            ast_node *node    = ast_node_create(self->ast_arena, ast_tuple);
            node->array.n     = 0;
            node->array.nodes = null;
            node->tuple.flags = 0;

            array_shrink(elements);
            node->array.n     = (u8)elements.size;
            node->array.nodes = elements.v;

            return result_ast_node(self, node);
        }

        // comma required if this is not the first time through the loop
        if (count++ > 0)
            if (a_try(self, a_comma)) {
                self->error.tag = tl_err_expected_comma;
                goto cleanup;
            }

        // expression
        if (0 == a_try(self, expression)) array_push(elements, &self->result);

        // loop to check for close round
    }

cleanup:
    array_free(elements);
    return 1;
}

static int begin_end_expression(parser *self) {
    if (a_try_s(self, the_symbol, "begin")) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    log(self, "begin begin...end expression");
    self->indent_level++;
    ast_node_array exprs = {.alloc = self->ast_arena}; // will be moved to node on success

    // detect empty begin end block and reject
    if (0 == a_try_s(self, the_symbol, "end")) {
        self->error.tag = tl_err_unfinished_begin_end;
        goto error;
    }

    while (1) {
        if (a_try(self, expression)) {
            // propagate error
            goto error;
        }

        log(self, "begin-end got %s", ast_node_to_string(self->transient, self->result));
        array_push(exprs, &self->result);

        // some expressions eat the end_of_expression (e.g. function
        // application), but some don't (e.g. numbers)
        (void)a_try_special(self, a_end_of_expression);

        if (0 == a_try_s(self, the_symbol, "end")) {
            ast_node *node                = ast_node_create(self->ast_arena, ast_begin_end);
            node->begin_end.expressions   = null;
            node->begin_end.n_expressions = 0;

            array_shrink(exprs);
            node->array.n     = (u8)exprs.size;
            node->array.nodes = exprs.v;
            if (exprs.size > 0xff) {
                self->error.tag = tl_err_too_many_expressions;
                goto error;
            }

            result_ast_node(self, node);
            goto success;
        }
    }

success:
    self->indent_level--;
    return 0;

error:
    array_free(exprs);
    self->indent_level--;
    return 1;
}

static int grouped_expression(parser *self) {
    if (a_try(self, a_open_round)) {
        self->error.tag = tl_err_ok;
        return 1;
    }

    if (a_try(self, expression)) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    ast_node *const out = self->result;

    log(self, "begin grouped expression");

    if (a_try(self, a_close_round)) {
        self->error.tag = tl_err_expected_close_round;
        return 1;
    }

    // replace parser result with expression node
    self->result = out;
    return 0;
}

static int expression(parser *self) {

    self->indent_level++;

    if (0 == a_try(self, expression_let)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, infix_operation)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, a_labelled_tuple)) goto success; // TODO naming is odd
    if (has_error(self)) goto error;

    if (0 == a_try(self, tuple_expression)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, if_then_else)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, lambda_function_application)) goto success; // before lambda_function
    if (has_error(self)) goto error;

    if (0 == a_try(self, lambda_function)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, function_application)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, grouped_expression)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, begin_end_expression)) goto success;
    if (has_error(self)) goto error;

    // the rest of the cases are standalone values

    if (0 == a_try(self, a_value)) goto success;

    // if (0 == a_try(self, a_nil)) goto success;
    // if (0 == a_try(self, a_field_setter)) goto success; // before field_access
    // if (0 == a_try(self, a_field_access)) goto success;
    // if (0 == a_try(self, a_dereference)) goto success; // before identifier
    // if (0 == a_try(self, a_identifier)) goto success;
    // if (0 == a_try(self, a_address_of)) goto success;
    // if (0 == a_try(self, a_number)) goto success;
    // if (0 == a_try(self, a_bool)) goto success;

    self->error.tag = tl_err_expected_expression;

    goto error;

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int continue_let_in(parser *self, ast_node *name_or_nil_or_lt) {
    // assumes caller has incremented indent_level

    log(self, "begin let-in declaration line %i", self->token.line);

    if (a_try(self, expression)) {
        self->error.tag = tl_err_expected_value;
        goto error;
    }
    ast_node *defn = self->result;

    // eat the optional 'in' token if it's present
    (void)a_try_s(self, the_symbol, "in");

    if (a_try(self, expression)) {
        // let the error propagate
        goto error;
    }
    ast_node *body = self->result;

    if (a_try_special(self, a_end_of_block)) {
        self->error.tag = tl_err_expected_end_of_block;
        goto error;
    }

    if (ast_labelled_tuple == name_or_nil_or_lt->tag) {

        // let-match-in
        ast_node *node           = ast_node_create(self->ast_arena, ast_let_match_in);
        node->let_match_in.lt    = name_or_nil_or_lt;
        node->let_match_in.value = defn;
        node->let_match_in.body  = body;

        result_ast_node(self, node);

    } else {

        ast_node *node     = ast_node_create(self->ast_arena, ast_let_in);
        node->let_in.name  = name_or_nil_or_lt;
        node->let_in.value = defn;
        node->let_in.body  = body;

        result_ast_node(self, node);
    }

    log(self, "end let-in declaration line %i", self->token.line);
    goto success;

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int toplevel_let(parser *self) {
    if (a_try_s(self, the_symbol, "let")) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    log(self, "begin let");

    self->indent_level++;

    if (0 == a_try(self, function_declaration)) {
        ast_node *decl = self->result;
        log(self, "begin let function declaration");

        if (a_try(self, function_definition)) {
            if (self->error.tag == tl_err_ok) self->error.tag = tl_err_expected_function_definition;
            goto error;
        }
        ast_node *defn             = self->result;
        ast_node *node             = ast_node_create(self->ast_arena, ast_let);
        node->let.parameters       = null;
        node->let.n_parameters     = 0;
        node->let.flags            = 0;
        node->let.name             = null;
        node->let.body             = null;
        node->let.arrow            = null;
        node->let.specialized_name = string_t_init_empty();

        // move declaration into new node
        // string_t_copy(self->ast_arena, &node->let.name,
        // &decl->function_declaration.name->symbol.name);
        node->let.name = decl->function_declaration.name;
        node->let.body = defn;

        // move the vector from the function_declaration node to the new ast node
        node->array.nodes = decl->array.nodes;
        node->array.n     = decl->array.n;
        decl->array.nodes = null;
        decl->array.n     = 0;

        result_ast_node(self, node);
        goto success;
    }

    if (0 == a_try(self, simple_declaration)) {
        ast_node *sym_or_nil = self->result;
        return continue_let_in(self, sym_or_nil);
    }

    if (0 == a_try(self, forward_declaration)) {
        ast_node *sym = self->result;
        assert(ast_symbol == sym->tag);
        map_set_v(&self->forwards, string_t_str(&sym->symbol.name), string_t_size(&sym->symbol.name), sym);

        // TODO error on duplicate declaration

        goto success;
    }

    self->error.tag = tl_err_expected_declaration;
    goto error;

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int toplevel(parser *self) {

    self->error.tag = tl_err_ok;

    if (0 == a_try(self, struct_declaration)) return 0;
    if (has_error(self)) return 1;

    if (0 == a_try(self, toplevel_let)) return 0;
    if (has_error(self)) return 1;

    if (0 == a_try(self, expression)) return 0;
    if (has_error(self)) return 1;

    return 1;
}

static int expression_let(parser *self) {
    // allows let-in but not let expressions
    if (a_try_s(self, the_symbol, "let")) {
        self->error.tag = tl_err_ok;
        return 1;
    }
    log(self, "begin let expression");

    self->indent_level++;

    if (0 == a_try(self, simple_declaration)) {
        return continue_let_in(self, self->result);
    }

    if (0 == a_try(self, match_declaration)) {
        return continue_let_in(self, self->result);
    }

    self->indent_level--;
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
        log(p, "parse_all: parsed node %s", ast_node_to_string(p->transient, node));

        array_push(*out, &node);
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
    array_push(p->tokens, tok);
}

static void tokens_shrink(struct parser *p, u32 n) {
    p->tokens.size = n;
}

void parser_report_errors(parser *self) {
    if (tl_err_ok == self->error.tag) return;

    fprintf(stderr, "%s:%u: %s\n", self->error.file, self->error.line,
            tl_error_tag_to_string(self->error.tag));
}
static int too_many_arguments(parser *self) {
    self->error.tag = tl_err_too_many_arguments;
    return 1;
}

static int has_error(parser *self) {
    return self->error.tag != tl_err_ok;
}

void log(struct parser *self, char const *restrict fmt, ...) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
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
#pragma clang diagnostic pop
}
