#include "parser.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "mos_string.h"
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
    allocator             *parser_arena; // for tokens
    allocator             *ast_arena;    // for ast nodes
    allocator             *debug_arena;  // for debug strings

    tokenizer             *tokenizer;

    ast_node              *result;

    token_array            tokens;

    struct parser_error    error;
    struct tokenizer_error tokenizer_error;
    struct token           token;

    u32                    next_nil_name;

    bool                   verbose;
    int                    indent_level;
};

static void tokens_push_back(struct parser *, struct token *);
static void tokens_shrink(struct parser *, u32);
static int  too_many_arguments(parser *);
static bool has_error(parser *p);
static void log(struct parser *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, char_cslice input) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));

    alloc_zero(self);
    self->parent_alloc = alloc;
    self->parser_arena = arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->ast_arena    = arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->debug_arena  = arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->verbose      = false;

    // tokenizer
    self->tokenizer = tokenizer_create(alloc, input, "(no file)");

    // good_tokens
    self->tokens = (token_array){.alloc = self->parser_arena};

    // error
    token_init(&self->token, tok_invalid);
    self->error.token     = &self->token;
    self->error.tokenizer = &self->tokenizer_error;

    return self;
}

void parser_destroy(parser **self) {
    // error token: arena
    // tokens: arena

    // tokenizer
    tokenizer_destroy(&(*self)->tokenizer);

    // arena
    arena_destroy((*self)->parent_alloc, &(*self)->debug_arena);
    arena_destroy((*self)->parent_alloc, &(*self)->ast_arena);
    arena_destroy((*self)->parent_alloc, &(*self)->parser_arena);
    alloc_free((*self)->parent_alloc, *self);
    *self = null;
}

// -- parser --

typedef int (*parse_fun)(parser *);
typedef int (*parse_fun_s)(parser *, char const *);

static int struct_declaration(parser *);
static int expression(parser *);
static int expression_let(parser *);
static int function_argument(parser *);
static int grouped_expression(parser *);
static int if_then_else(parser *);
static int infix_operand(parser *);
static int infix_operation(parser *);
static int lambda_function(parser *);
static int lambda_function_application(parser *);
static int toplevel(parser *);
static int toplevel_let(parser *);
static int tuple_expression(parser *);

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

static int result_ast_bool(parser *p, bool val) {
    p->result            = ast_node_create(p->ast_arena, ast_bool);
    p->result->bool_.val = val;
    return 0;
}

static int result_ast_str(parser *p, ast_tag tag, char const *s) {
    p->result              = ast_node_create(p->ast_arena, tag);
    p->result->symbol.name = mos_string_init(p->ast_arena, s);
    // syms and strs use same union

    return 0;
}

static int result_ast_node(parser *p, ast_node *node) {
    p->result = node;
    return 0;
}

static bool is_reserved(char const *s) {
    static char const *strings[] = {
      "else", "end", "false", "fun", "if", "in", "let", "then", "true", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return true;

    return false;
}

static bool is_start_of_expression(char const *s) {
    static char const *strings[] = {
      "fun", "if", "in", "let", "struct", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return true;

    return false;
}

static bool is_arithmetic_operator(char const *s) {
    static char const *strings[] = {
      "+", "-", "*", "/", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return true;
    return false;
}

static bool is_relational_operator(char const *s) {
    static char const *strings[] = {
      "<", "<=", "==", "<>", ">=", ">", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return true;
    return false;
}

static bool is_eof(parser *p) {
    return p->tokenizer_error.tag == tess_err_eof;
}

nodiscard static int eat_newlines(parser *p) {

    while (true) {
        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            log(p, "eat_newlines: tokenizer error: %s", tess_error_tag_to_string(p->tokenizer_error.tag));
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.tag  = tess_err_tokenizer_error;
            return 1;
        }

        token_tag const tag = p->token.tag;
        if (tok_comment == tag || tok_one_newline == tag || tok_two_newline == tag ||
            tok_newline_indent == tag) {
            continue;
        } else {
            tokenizer_put_back(p->tokenizer, &p->token, 1);
            return result_ast(p, ast_eof);
        }
    }
}

nodiscard static int next_token(parser *p) {
    while (true) {

        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.tag  = tess_err_tokenizer_error;
            return 1;
        }

        // always update file/line
        p->error.file = p->token.file;
        p->error.line = p->token.line;

        if (tok_comment == p->token.tag) continue;

        char *str = token_to_string(p->debug_arena, &p->token);
        log(p, "next_token: %s", str);
        alloc_free(p->debug_arena, str);

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
            char *str = token_to_string(p->debug_arena, &p->tokens.v[save_toks]);
            log(p, "a_try: put back %i tokens starting with %s", p->tokens.size - save_toks, str);
            alloc_free(p->debug_arena, str);
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

static int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
    u32 const save_toks = p->tokens.size;
    if (fun(p, arg)) {
        if (p->tokens.size > save_toks) {
            char *str = token_to_string(p->debug_arena, &p->tokens.v[save_toks]);
            log(p, "a_try: put back %i tokens starting with %s", p->tokens.size - save_toks, str);
            alloc_free(p->debug_arena, str);

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
            char *str = token_to_string(p->debug_arena, &p->tokens.v[save_toks]);
            log(p, "a_try: put back %i tokens starting with %s", p->tokens.size - save_toks, str);
            alloc_free(p->debug_arena, str);

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

    p->error.tag = tess_err_expected_comma;
    return 1;
}

static int a_dot(parser *p) {
    if (next_token(p)) return 1;

    if (tok_dot == p->token.tag) return result_ast_str(p, ast_symbol, ".");

    p->error.tag = tess_err_expected_dot;
    return 1;
}

static int a_open_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_round == p->token.tag) return result_ast_str(p, ast_symbol, "(");

    p->error.tag = tess_err_expected_open_round;
    return 1;
}

static int a_close_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_round == p->token.tag) return result_ast_str(p, ast_symbol, ")");

    p->error.tag = tess_err_expected_close_round;
    return 1;
}

static int a_end_of_expression(parser *p) {

    if (eat_newlines(p) || next_token(p)) {
        if (is_eof(p)) return result_ast_str(p, ast_symbol, ";");
        return 1;
    }

    switch (p->token.tag) {
    case tok_one_newline:
    case tok_newline_indent:
    case tok_two_newline:
    case tok_semicolon:      return result_ast_str(p, ast_symbol, ";");

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
    case tok_arrow:
    case tok_open_round:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_comment:     break;
    }

    p->error.tag = tess_err_unfinished_expression;
    return 1;
}

static int a_newline(parser *p) {

    if (next_token(p)) {
        if (is_eof(p)) return result_ast_str(p, ast_symbol, ";");
        return 1;
    }

    switch (p->token.tag) {
    case tok_one_newline:
    case tok_newline_indent:
    case tok_two_newline:
    case tok_semicolon:      return result_ast_str(p, ast_symbol, ";");

    case tok_close_round:
        // signal special failure so the token gets put back, but use a magic
        // error code so that consumers of end_of_expression can treat it as a success
        return 2;

    case tok_symbol:
    case tok_comma:
    case tok_dot:
    case tok_colon:
    case tok_colon_equal:
    case tok_arrow:
    case tok_open_round:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_comment:     break;
    }

    p->error.tag = tess_err_expected_newline;
    return 1;
}

static int a_identifier(parser *p) {
    if (eat_newlines(p)) return 1; // FIXME I added this, is it ok?
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
    p->error.tag = tess_err_expected_identifier;
    return 1;
}

static int a_type_identifier(parser *p) {
    return a_try(p, a_identifier);
}

static int a_colon(parser *p);

static int a_identifier_typed(parser *p) {
    if (a_try(p, a_identifier)) return 1;
    ast_node *name       = p->result;

    ast_node *annotation = 0;

    if (0 == a_try(p, a_colon)) {

        if (a_try(p, a_type_identifier)) {
            return 1;
        }

        annotation = p->result;
    }

    ast_node *node    = ast_node_create(p->ast_arena, ast_symbol);
    node->symbol.name = name->symbol.name;
    if (annotation) node->symbol.annotation = annotation;
    return result_ast_node(p, node);
}

static int a_infix_operator(parser *p) {
    // + - * /, relationals: < <= == <> >= >
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {

        if (is_arithmetic_operator(p->token.s) || is_relational_operator(p->token.s))
            return result_ast_str(p, ast_symbol, p->token.s);
    }

    p->error.tag = tess_err_expected_operator;
    return 1;
}

static int the_symbol(parser *p, char const *const want) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp(want, p->token.s)) return result_ast_str(p, ast_symbol, p->token.s);
    }

    p->error.tag = tess_err_expected_symbol;
    return 1;
}

static int a_string(parser *p) {
    if (next_token(p)) return 1;

    if (tok_string == p->token.tag) return result_ast_str(p, ast_string, p->token.s);

    p->error.tag = tess_err_expected_string;
    return 1;
}

static int string_to_number(parser *parser, char const *const in) {
    i64 i;
    u64 u;
    f64 d;
    switch (mos_string_parse_number(in, &i, &u, &d)) {
    case 1:  return result_ast_i64(parser, i);
    case 2:  return result_ast_u64(parser, u);
    case 3:  return result_ast_f64(parser, d);
    default: return 1;
    }
}

static int a_number(parser *p) {
    if (next_token(p)) return 1;

    if (tok_number == p->token.tag) {

        if (string_to_number(p, p->token.s)) goto error;
        // sets parser result

        return 0;
    }

error:
    p->error.tag = tess_err_expected_number;
    return 1;
}

static int a_bool(parser *p) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp("true", p->token.s)) return result_ast_bool(p, true);
        if (0 == strcmp("false", p->token.s)) return result_ast_bool(p, false);
    }

    p->error.tag = tess_err_expected_bool;
    return 1;
}

static int a_literal(parser *p) {
    if (0 == a_try(p, a_string)) return 0;
    if (0 == a_try(p, a_number)) return 0;
    if (0 == a_try(p, a_bool)) return 0;
    p->error.tag = tess_err_expected_literal;
    return 1;
}

static int a_equal_sign(parser *p) {
    if (next_token(p)) return 1;

    if (tok_equal_sign == p->token.tag) return result_ast_str(p, ast_symbol, "=");

    p->error.tag = tess_err_expected_equal_sign;
    return 1;
}

static int a_colon(parser *p) {
    if (next_token(p)) return 1;

    if (tok_colon == p->token.tag) return result_ast_str(p, ast_symbol, ":");

    p->error.tag = tess_err_expected_colon;
    return 1;
}

static int a_colon_equal(parser *p) {
    if (next_token(p)) return 1;

    if (tok_colon_equal == p->token.tag) return result_ast_str(p, ast_symbol, ":=");

    p->error.tag = tess_err_expected_colon_equal;
    return 1;
}

static int a_arrow(parser *p) {
    if (next_token(p)) return 1;

    if (tok_arrow == p->token.tag) return result_ast_str(p, ast_symbol, "->");

    p->error.tag = tess_err_expected_arrow;
    return 1;
}

static int a_nil(parser *p) {

    if ((0 == a_open_round(p)) && (0 == a_close_round(p))) return result_ast(p, ast_nil);

    return 1;
}

static int a_end_of_block(parser *p) {

    if (eat_newlines(p) || next_token(p)) {
        if (is_eof(p)) return result_ast_str(p, ast_symbol, "end");

        return 1;
    }

    if (tok_symbol == p->token.tag && 0 == strcmp("end", p->token.s)) {
        return result_ast_str(p, ast_symbol, "end");
    }

    if (tok_one_newline == p->token.tag || tok_two_newline == p->token.tag) {
        return result_ast_str(p, ast_symbol, "end");
    }

    p->error.tag = tess_err_expected_end_of_block;
    return 1;
}

static int a_field_access(parser *p) {
    if (a_try(p, a_identifier)) return 1;
    ast_node *variable = p->result;

    if (a_try(p, a_dot)) return 1;

    if (a_try(p, a_identifier)) return 1;
    ast_node *field = p->result;

    log(p, "a_field_access looks good");
    ast_node                 *node = ast_node_create(p->ast_arena, ast_user_type_get);
    struct ast_user_type_get *v    = ast_node_utg(node);
    v->struct_name                 = variable;
    v->field_name                  = field;
    return result_ast_node(p, node);
}

static int a_field_setter(parser *p) {
    if (a_try(p, a_field_access)) return 1;
    log(p, "a_field_setter got field_access");
    ast_node *user_field = p->result;
    if (a_try(p, a_colon_equal)) return 1;
    if (a_try(p, expression)) return 1;
    ast_node *value = p->result;

    log(p, "a_field_setter looks good");
    ast_node                 *node = ast_node_create(p->ast_arena, ast_user_type_set);
    struct ast_user_type_set *v    = ast_node_uts(node);
    struct ast_user_type_get *vget = ast_node_utg(user_field);
    v->struct_name                 = vget->struct_name;
    v->field_name                  = vget->field_name;
    v->value                       = value;
    return result_ast_node(p, node);
}

static int struct_declaration(parser *self) {
    //     struct name = ... end
    if (a_try_s(self, the_symbol, "struct")) {
        self->error.tag = tess_err_ok;
        return 1;
    }
    log(self, "struct begin");
    self->indent_level++;

    if (a_try(self, a_identifier)) {
        self->error.tag = tess_err_expected_struct_name;
        goto error;
    }

    ast_node      *name        = self->result;

    ast_node_array field_names = {.alloc = self->ast_arena};
    ast_node_array field_types = {.alloc = self->ast_arena};

    if (a_try(self, a_equal_sign)) {
        self->error.tag = tess_err_expected_equal_sign;
        goto error;
    }

    // check for empty struct
    if (0 == a_try(self, a_end_of_block)) {
        ast_node *node           = ast_node_create(self->ast_arena, ast_user_type_definition);
        node->user_type_def.name = name;
        result_ast_node(self, node);
        goto success;
    }

    // accumulate names and types until end of block is seen
    while (true) {
        if (eat_newlines(self)) {
            self->error.tag = tess_err_unfinished_struct;
            goto error;
        }

        if (0 == a_try(self, a_identifier)) {
            ast_node *field_name = self->result;
            log(self, "struct_declaration: field %s", ast_node_to_string(self->debug_arena, field_name));

            if (a_try(self, a_colon)) {
                self->error.tag = tess_err_expected_colon;
                goto error;
            }

            if (a_try(self, a_type_identifier)) {
                self->error.tag = tess_err_expected_type;
                goto error;
            }
            ast_node *type = self->result;
            log(self, "struct_declaration: type %s", ast_node_to_string(self->debug_arena, type));

            array_push(field_names, &field_name);
            array_push(field_types, &type);

            if (a_try_special(self, a_newline)) {
                self->error.tag = tess_err_expected_newline;
                goto error; // expect ; or newline after field
            }

            continue;
        }

        if (0 == a_try(self, a_end_of_block)) {

            ast_node *node           = ast_node_create(self->ast_arena, ast_user_type_definition);
            node->user_type_def.name = name;

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

        // anything else is an error
        self->error.tag = tess_err_expected_end_of_block;
        return 1;
    }

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int function_declaration(parser *p) {
    // f a b c... = : only symbols allowed, terminated by =.

    if (a_try(p, a_identifier)) return 1;

    ast_node *const name       = p->result; // function name
    ast_node_array  parameters = {.alloc = p->parser_arena};

    // check: f () declares function with no parameters
    if (0 == a_try(p, a_nil)) {

        // next token must be equal sign
        if (0 == a_try(p, a_equal_sign)) {
            ast_node *node                  = ast_node_create(p->ast_arena, ast_function_declaration);
            node->function_declaration.name = name;
            return result_ast_node(p, node);
        }

        return 1;
    }

    // must have at least one parameter
    if (a_try(p, a_identifier_typed)) return 1;

    array_push(parameters, &p->result);

    // accumulate identifiers as parameters until equal sign is seen
    while (true) {
        if (0 == a_try(p, a_identifier_typed)) {
            array_push(parameters, &p->result);
            continue;
        }

        if (0 == a_try(p, a_equal_sign)) {

            ast_node *node                  = ast_node_create(p->ast_arena, ast_function_declaration);
            node->function_declaration.name = name;

            array_shrink(parameters);
            node->array.nodes = parameters.v;
            if (parameters.size > 0xff) return too_many_arguments(p);
            node->array.n = (u8)parameters.size;

            log(p, "function_declaration: returning %s", ast_node_to_string(p->debug_arena, node));
            return result_ast_node(p, node);
        }

        // anything else is an error
        p->error.tag = tess_err_expected_argument;
        return 1;
    }
}

static int lambda_declaration(parser *p) {
    // a b c... -> : only symbols allowed, terminated by ->

    ast_node_array parameters = {.alloc = p->parser_arena};

    // accumulate identifiers as parameters until an arrow is seen
    while (true) {
        if (0 == a_try(p, a_identifier_typed)) {
            array_push(parameters, &p->result);
            continue;
        }

        if (0 == a_try(p, a_arrow)) {
            ast_node *node = ast_node_create(p->ast_arena, ast_lambda_declaration);

            array_shrink(parameters);
            node->array.n     = (u8)parameters.size;
            node->array.nodes = parameters.v;

            if (parameters.size > 0xff) {
                too_many_arguments(p);
                array_free(parameters);
                return 1;
            }

            return result_ast_node(p, node);
        }

        // anything else is an error
        p->error.tag = tess_err_unfinished_lambda_declaration;
        return 1;
    }
}

static int function_definition(parser *p) {
    return expression(p);
}

static int function_application(parser *self) {
    // f a b c ..., terminated by semicolon or one_newline or two_newline

    if (a_try(self, a_identifier)) {
        self->error.tag = tess_err_ok;
        return 1;
    }

    ast_node *const name = self->result;
    assert(ast_symbol == name->tag);

    ast_node_array arguments = {.alloc = self->parser_arena};

    // must have at least one argument
    if (a_try(self, function_argument)) {
        self->error.tag = tess_err_ok;
        return 1;
    }

    log(self, "begin function application");
    self->indent_level++;

    array_push(arguments, &self->result);

    while (true) {
        if (0 == a_try(self, function_argument)) {
            array_push(arguments, &self->result);
            continue;
        }

        if (0 == a_try_special(self, a_end_of_expression)) {
            // 2: "fails" due to close_round, which must not be
            // consumed, so that grouped_expression catches it.

            ast_node *node = ast_node_create(self->ast_arena, ast_named_function_application);
            mos_string_copy(self->ast_arena, &node->named_application.name, &name->symbol.name);

            array_shrink(arguments);
            node->array.n     = (u8)arguments.size;
            node->array.nodes = arguments.v;
            if (arguments.size > 0xff) {
                too_many_arguments(self);
                goto error;
            }

            log(self, "function_application: got %s", ast_node_to_string(self->debug_arena, node));
            result_ast_node(self, node);
            goto success;
        }

        self->error.tag = tess_err_expected_function_application_argument;
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

    log(p, "function_argument: try grouped_expression");
    if (0 == a_try(p, grouped_expression)) goto cleanup;

    log(p, "function_argument: try nil");
    if (0 == a_try(p, a_nil)) goto cleanup;

    log(p, "function_argument: try field access");
    if (0 == a_try(p, &a_field_access)) goto cleanup;

    log(p, "function_argument: try identifier");
    if (0 == a_try(p, a_identifier)) goto cleanup;

    log(p, "function_argument: try literal");
    if (0 == a_try(p, a_literal)) goto cleanup;

    p->indent_level--;
    log(p, "not function_argument");

    p->indent_level--;
    return 1;

cleanup:
    p->indent_level--;
    return 0;
}

static int if_then_else(parser *self) {

    ast_node *cond, *yes, *no;

    if (a_try_s(self, the_symbol, "if")) {
        self->error.tag = tess_err_ok;
        return 1;
    }

    log(self, "begin if-then-else");
    self->indent_level++;

    if (expression(self)) {
        self->error.tag = tess_err_expected_if_condition;
        goto error;
    }
    cond = self->result;

    if (a_try_s(self, the_symbol, "then")) {
        self->error.tag = tess_err_expected_keyword_then;
        goto error;
    }

    if (expression(self)) {
        self->error.tag = tess_err_expected_if_then_arm;
        goto error;
    }
    yes = self->result;

    if (a_try_s(self, the_symbol, "else")) {
        self->error.tag = tess_err_expected_keyword_else;
        goto error;
    }

    if (expression(self)) {
        self->error.tag = tess_err_expected_if_else_arm;
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
        self->error.tag = tess_err_ok;
        return 1;
    }
    ast_node *const lhs = self->result;

    if (a_try(self, a_infix_operator)) {
        self->error.tag = tess_err_ok;
        return 1;
    }
    ast_node *op_node = self->result;

    log(self, "begin infix");
    self->indent_level++;

    ast_operator op;
    if (string_to_ast_operator(ast_node_name_string(op_node), &op)) {
        self->error.tag = tess_err_expected_operator;
        goto error;
    }

    if (infix_operand(self)) {
        self->error.tag = tess_err_expected_infix_operand;
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
        self->error.tag = tess_err_ok;
        return 1;
    }

    log(self, "begin lambda function");
    self->indent_level++;

    if (a_try(self, lambda_declaration)) {
        self->error.tag = tess_err_expected_lambda;
        goto error;
    }
    ast_node *decl = self->result;

    if (a_try(self, function_definition)) {
        self->error.tag = tess_err_expected_function_definition;
        goto error;
    }
    ast_node *defn = self->result;

    // require end keyword to end parse of lambda function definition
    if (a_try(self, a_end_of_block)) {
        self->error.tag = tess_err_expected_end_of_block;
        goto error;
    }

    ast_node *node             = ast_node_create(self->ast_arena, ast_lambda_function);
    node->lambda_function.body = defn;

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
        self->error.tag = tess_err_ok;
        return 1;
    }
    ast_node *lambda = self->result;

    // there must be at least one argument
    ast_node_array arguments = {.alloc = self->parser_arena};

    if (a_try(self, function_argument)) {
        self->error.tag = tess_err_ok;
        return 1;
    }

    log(self, "begin lambda function application");
    self->indent_level++;

    array_push(arguments, &self->result);

    while (true) {
        if (0 == a_try(self, function_argument)) {
            array_push(arguments, &self->result);
            continue;
        }

        if (0 == a_try_special(self, a_end_of_expression)) {
            ast_node *node = ast_node_create(self->ast_arena, ast_lambda_function_application);
            node->lambda_application.lambda = lambda;

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
        self->error.tag = tess_err_expected_lambda_function_application_argument;
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
        self->error.tag = tess_err_ok;
        return 1;
    }

    ast_node_array elements = {.alloc = self->parser_arena}; // will be moved to node on success

    // first, expect an expression, which must be followed by a comma
    // then, zero or more expressions before a close round. So (expr,)
    // is a valid tuple.
    if (a_try(self, expression)) {
        self->error.tag = tess_err_ok;
        return 1;
    }

    array_push(elements, &self->result);

    if (a_comma(self)) goto cleanup;

    log(self, "begin tuple");

    int count = 0;

    while (true) {

        if (0 == a_try(self, a_close_round)) {
            ast_node *node = ast_node_create(self->ast_arena, ast_tuple);

            array_shrink(elements);
            node->array.n     = (u8)elements.size;
            node->array.nodes = elements.v;
            if (elements.size > 0xff) {
                too_many_arguments(self);
                goto cleanup;
            }

            return result_ast_node(self, node);
        }

        // comma required if this is not the first time through the loop
        if (count++ > 0)
            if (a_try(self, a_comma)) {
                self->error.tag = tess_err_expected_comma;
                goto cleanup;
            }

        // expression
        if (0 == a_try(self, expression)) {
            array_push(elements, &self->result);
        }

        // loop to check for close round, or else
    }

cleanup:
    array_free(elements);
    return 1;
}

static int grouped_expression(parser *self) {
    if (a_try(self, a_open_round)) {
        self->error.tag = tess_err_ok;
        return 1;
    }

    if (a_try(self, expression)) {
        self->error.tag = tess_err_ok;
        return 1;
    }
    ast_node *const out = self->result;

    log(self, "begin grouped expression");

    if (a_try(self, a_close_round)) {
        self->error.tag = tess_err_expected_close_round;
        return 1;
    }

    // replace parser result with expression node
    self->result = out;
    return 0;
}

static int expression(parser *self) {

    if (eat_newlines(self)) return 1;

    self->indent_level++;

    if (0 == a_try(self, &expression_let)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, &infix_operation)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, &tuple_expression)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, &if_then_else)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, &lambda_function_application)) goto success; // before lambda_function
    if (has_error(self)) goto error;

    if (0 == a_try(self, &lambda_function)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, &function_application)) goto success;
    if (has_error(self)) goto error;

    if (0 == a_try(self, &grouped_expression)) goto success;
    if (has_error(self)) goto error;

    // the rest of the cases are standalone values

    if (0 == a_try(self, &a_nil)) goto success;
    if (0 == a_try(self, &a_field_setter)) goto success; // before field_access
    if (0 == a_try(self, &a_field_access)) goto success;
    if (0 == a_try(self, &a_identifier)) goto success;
    if (0 == a_try(self, &a_number)) goto success;
    if (0 == a_try(self, &a_bool)) goto success;

    goto error;

    // if (0 == a_try(self, &a_end_of_expression)) goto success;

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int continue_let_in(parser *self, ast_node *name_or_nil) {
    // assumes caller has incremented indent_level

    log(self, "begin let-in declaration");

    if (a_try(self, expression)) {
        self->error.tag = tess_err_expected_value;
        goto error;
    }
    ast_node *defn = self->result;

    // eat the optional 'in' token if it's present
    a_try_s(self, the_symbol, "in");

    if (a_try(self, expression)) {
        self->error.tag = tess_err_expected_body;
        goto error;
    }
    ast_node *body = self->result;

    if (a_try(self, a_end_of_block)) {
        self->error.tag = tess_err_expected_end_of_block;
        goto error;
    }
    ast_node *node     = ast_node_create(self->ast_arena, ast_let_in);
    node->let_in.name  = name_or_nil;
    node->let_in.value = defn;
    node->let_in.body  = body;

    result_ast_node(self, node);
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
        self->error.tag = tess_err_ok;
        return 1;
    }
    log(self, "begin let");

    self->indent_level++;

    if (0 == a_try(self, function_declaration)) {
        ast_node *decl = self->result;
        log(self, "begin let function declaration");

        if (a_try(self, function_definition)) {
            self->error.tag = tess_err_expected_function_definition;
            goto error;
        }
        ast_node *defn = self->result;
        ast_node *node = ast_node_create(self->ast_arena, ast_let);

        // move declaration into new node
        mos_string_copy(self->ast_arena, &node->let.name, &decl->function_declaration.name->symbol.name);
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

success:
    self->indent_level--;
    return 0;

error:
    self->indent_level--;
    return 1;
}

static int toplevel(parser *self) {
    if (eat_newlines(self)) return 1;

    self->error.tag = tess_err_ok;

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
        self->error.tag = tess_err_ok;
        return 1;
    }
    log(self, "begin let expression");

    self->indent_level++;

    if (0 == a_try(self, simple_declaration)) {
        ast_node *sym_or_nil = self->result;
        return continue_let_in(self, sym_or_nil);
    }

    self->indent_level--;
    return 1;
}

int parser_next(parser *parser) {
    return toplevel(parser);
}

int parser_parse_all(parser *p, ast_node_array *out) {

    int res = 0;
    while (0 == (res = parser_next(p))) {
        ast_node *node;

        parser_result(p, &node);
        log(p, "parse_all: parsed node %s", ast_node_to_string(p->debug_arena, node));

        array_push(*out, &node);
    }

    if (is_eof(p)) return 0;

    return res;
}

int parser_parse_all_verbose(parser *p, ast_node_array *out) {
    p->verbose = true;

    log(p, "begin parse");
    int res = parser_parse_all(p, out);
    log(p, "end parse");

    p->verbose = false;
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
    if (tess_err_ok == self->error.tag) return;

    fprintf(stderr, "error: %s:%u: %s\n", self->error.file, self->error.line,
            tess_error_tag_to_string(self->error.tag));
}
static int too_many_arguments(parser *self) {
    self->error.tag = tess_err_too_many_arguments;
    return 1;
}

static bool has_error(parser *self) {
    return self->error.tag != tess_err_ok;
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
