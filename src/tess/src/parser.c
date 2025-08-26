#include "parser.h"

#include "alloc.h"
#include "ast.h"
#include "dbg.h"
#include "mos_string.h"
#include "token.h"
#include "tokenizer.h"
#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define PARSER_ARENA_SIZE 1024

struct parser {
    allocator             *parent_alloc;
    allocator             *parser_arena;
    allocator             *ast_arena;
    tokenizer             *tokenizer;

    ast_node              *result;

    struct vector          seen_tokens; // for backtracking

    struct parser_error    error;
    struct tokenizer_error tokenizer_error; // does not require deinit
    struct token           token;           // requires deinit
};

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, char const *input, size_t input_len) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));

    alloc_zero(self);
    self->parent_alloc = alloc;
    self->parser_arena = alloc_arena_create(self->parent_alloc, PARSER_ARENA_SIZE);
    self->ast_arena    = alloc_arena_create(self->parent_alloc, PARSER_ARENA_SIZE);

    // tokenizer
    self->tokenizer = tokenizer_create(self->parser_arena, input, input_len);

    // good_tokens
    vec_init(self->parser_arena, &self->seen_tokens, sizeof(struct token), 0);

    // error
    token_init(&self->token, tok_invalid);
    self->error.token     = &self->token;
    self->error.tokenizer = &self->tokenizer_error;

    return self;
}

void parser_destroy(parser **self) {
    // error token: arena
    // good_tokens: arena
    // tokenizer: arena

    // arena
    alloc_arena_destroy((*self)->parent_alloc, &(*self)->ast_arena);
    alloc_arena_destroy((*self)->parent_alloc, &(*self)->parser_arena);
    alloc_free((*self)->parent_alloc, *self);
    *self = null;
}

// -- parser --

typedef int (*parse_fun)(parser *);
typedef int (*parse_fun_s)(parser *, char const *);

static int expression(parser *);
static int function_argument(parser *);
static int grouped_expression(parser *);
static int if_then_else(parser *);
static int infix_operand(parser *);
static int infix_operation(parser *);
static int lambda_function(parser *);
static int lambda_function_application(parser *);
static int let_in_form(parser *);
static int let_form(parser *);
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
      "if", "then", "else", "fun", "let", "in", "true", "false", null,
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

nodiscard static int eat_newlines(parser *p) {

    while (true) {
        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            p->error.tag = tess_err_tokenizer_error;
            return 1;
        }

        token_tag const tag = p->token.tag;
        if (tok_comment == tag || tok_one_newline == tag || tok_two_newline == tag ||
            tok_newline_indent == tag) {
            continue;
        } else {
            if (tokenizer_put_back(p->tokenizer, &p->token, 1)) return 1;
            return result_ast(p, ast_eof);
        }
    }
}

nodiscard static int next_token(parser *p) {
    while (true) {

        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            p->error.tag = tess_err_tokenizer_error;
            return 1;
        }

        if (tok_comment == p->token.tag) continue;

        vec_push_back(p->parser_arena, &p->seen_tokens, &p->token);
        return 0;
    }
}

nodiscard static int a_try(parser *p, parse_fun fun) {
    u32 const save_toks = vec_size(&p->seen_tokens);
    if (fun(p)) {
        assert(vec_size(&p->seen_tokens) >= save_toks);
        if (tokenizer_put_back(p->tokenizer, ((token const *)vec_data(&p->seen_tokens)) + save_toks,
                               vec_size(&p->seen_tokens) - save_toks))
            return 2; // TODO handle oom error

        vec_resize(p->parser_arena, &p->seen_tokens, save_toks);

        return 1;
    }
    return 0;
}

static int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
    u32 const save_toks = vec_size(&p->seen_tokens);
    if (fun(p, arg)) {
        assert(vec_size(&p->seen_tokens) >= save_toks);
        if (tokenizer_put_back(p->tokenizer, ((token const *)vec_data(&p->seen_tokens)) + save_toks,
                               vec_size(&p->seen_tokens) - save_toks))
            return 2; // TODO handle oom error

        vec_resize(p->parser_arena, &p->seen_tokens, save_toks);

        return 1;
    }
    return 0;
}

static int a_comma(parser *p) {
    if (next_token(p)) return 1;

    if (tok_comma == p->token.tag) return result_ast_str(p, ast_symbol, ",");

    p->error.tag = tess_err_expected_comma;
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
    if (next_token(p)) {
        if (tess_err_eof == p->tokenizer_error.tag) return result_ast_str(p, ast_symbol, ";");
        return 1;
    }

    if (tok_semicolon == p->token.tag || tok_one_newline == p->token.tag || tok_two_newline == p->token.tag)
        return result_ast_str(p, ast_symbol, ";");

    p->error.tag = tess_err_unfinished_expression;
    return 1;
}

static int a_identifier(parser *p) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 != strlen(p->token.s) && !is_reserved(p->token.s)) {

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
        }
    }

error:
    p->error.tag = tess_err_expected_identifier;
    return 1;
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
    if (0 == a_try(p, &a_string)) return 0;
    if (0 == a_try(p, &a_number)) return 0;
    if (0 == a_try(p, &a_bool)) return 0;
    p->error.tag = tess_err_expected_literal;
    return 1;
}

static int a_equal_sign(parser *p) {
    if (next_token(p)) return 1;

    if (tok_equal_sign == p->token.tag) return result_ast_str(p, ast_symbol, "=");

    p->error.tag = tess_err_expected_equal_sign;
    return 1;
}

static int a_arrow(parser *p) {
    if (next_token(p)) return 1;

    if (tok_arrow == p->token.tag) return result_ast_str(p, ast_symbol, "->");

    p->error.tag = tess_err_expected_arrow;
    return 1;
}

static int a_nil(parser *p) {

    if ((0 == a_try(p, &a_open_round)) && (0 == a_try(p, &a_close_round))) return result_ast(p, ast_nil);

    p->error.tag = tess_err_expected_arrow;
    return 1;
}

static int function_declaration(parser *p) {
    // f a b c... = : only symbols allowed, terminated by =.

    if (a_try(p, &a_identifier)) return 1;

    ast_node *const name = p->result; // function name

    vector          parameters;
    ast_vector_init(p->ast_arena, &parameters);

    // check: f () declares function with no parameters
    if (0 == a_try(p, &a_nil)) {

        // next token must be equal sign
        if (0 == a_try(p, &a_equal_sign)) {
            ast_node *node                  = ast_node_create(p->ast_arena, ast_function_declaration);
            node->function_declaration.name = name;
            vec_move(&node->function_declaration.parameters, &parameters);
            return result_ast_node(p, node);
        }

        return 1;
    }

    // accumulate identifiers as parameters until equal sign is seen
    while (true) {
        if (0 == a_try(p, &a_identifier)) {
            vec_push_back(p->parser_arena, &parameters, &p->result);
            continue;
        }

        if (0 == a_try(p, &a_equal_sign)) {

            ast_node *node                  = ast_node_create(p->ast_arena, ast_function_declaration);
            node->function_declaration.name = name;
            vec_move(&node->function_declaration.parameters, &parameters);
            return result_ast_node(p, node);
        }

        // anything else is an error
        p->error.tag = tess_err_expected_argument;
        return 1;
    }
}

static int lambda_declaration(parser *p) {
    // a b c... -> : only symbols allowed, terminated by ->

    vector parameters;
    ast_vector_init(p->ast_arena, &parameters);

    // accumulate identifiers as parameters until an arrow is seen
    while (true) {
        if (0 == a_try(p, &a_identifier)) {
            vec_push_back(p->parser_arena, &parameters, &p->result);
            continue;
        }

        if (0 == a_try(p, &a_arrow)) {
            ast_node *node = ast_node_create(p->ast_arena, ast_lambda_declaration);
            vec_move(&node->lambda_declaration.parameters, &parameters);
            return result_ast_node(p, node);
        }

        // anything else is an error
        p->error.tag = tess_err_expected_argument;
        return 1;
    }
}

static int function_definition(parser *p) {
    return expression(p);
}

static int function_application(parser *p) {
    // f a b c ..., terminated by semicolon or one_newline or two_newline

    if (a_try(p, &a_identifier)) return 1;

    ast_node *const name = p->result;

    vector          arguments;
    ast_vector_init(p->ast_arena, &arguments);

    // must have at least one argument
    if (a_try(p, &function_argument)) return 1;
    vec_push_back(p->parser_arena, &arguments, &p->result);

    while (true) {
        if (0 == a_try(p, &function_argument)) {
            vec_push_back(p->parser_arena, &arguments, &p->result);
            continue;
        }

        if ((tess_err_eof == p->error.tag) || 0 == a_try(p, &a_end_of_expression)) {

            ast_node *node               = ast_node_create(p->ast_arena, ast_named_function_application);
            node->named_application.name = name;
            vec_move(&node->named_application.arguments, &arguments);
            return result_ast_node(p, node);
        }

        p->error.tag = tess_err_expected_argument;
        return 1;
    }
}

//

static int function_argument(parser *p) {

    if (0 == a_try(p, &lambda_function_application)) return 0;
    if (0 == a_try(p, &grouped_expression)) return 0;
    if (0 == a_try(p, &let_in_form)) return 0;
    if (0 == a_try(p, &if_then_else)) return 0;
    if (0 == a_try(p, &a_nil)) return 0;
    if (0 == a_try(p, &a_identifier)) return 0;
    if (0 == a_try(p, &a_literal)) return 0;
    if (0 == a_try(p, &a_end_of_expression)) {
        // if looking for a function argument, consider end of expression as an eof error
        p->error.tag = tess_err_eof;
        return 1;
    }

    return 1;
}

static int if_then_else(parser *p) {

    ast_node *cond, *yes, *no;

    if (a_try_s(p, &the_symbol, "if")) return 1;
    if (a_try(p, &expression)) return 1;
    cond = p->result;

    if (a_try_s(p, &the_symbol, "then")) return 1;
    if (a_try(p, &expression)) return 1;
    yes = p->result;

    if (a_try_s(p, &the_symbol, "else")) return 1;
    if (a_try(p, &expression)) return 1;
    no                           = p->result;

    ast_node *node               = ast_node_create(p->ast_arena, ast_if_then_else);
    node->if_then_else.condition = cond;
    node->if_then_else.yes       = yes;
    node->if_then_else.no        = no;
    return result_ast_node(p, node);
}

static int infix_operand(parser *p) {
    return function_argument(p);
}

static int infix_operation(parser *p) {
    // a * b

    if (a_try(p, &infix_operand)) return 1;
    ast_node *const lhs = p->result;

    if (a_try(p, &a_infix_operator)) return 1;
    ast_node    *op_node = p->result;

    ast_operator op;
    if (string_to_ast_operator(mos_string_str(&op_node->symbol.name), &op)) return 1;

    if (a_try(p, &infix_operand)) return 1;
    ast_node *const rhs  = p->result;

    ast_node       *node = ast_node_create(p->ast_arena, ast_infix);
    node->infix.left     = lhs;
    node->infix.right    = rhs;
    node->infix.op       = op;
    return result_ast_node(p, node);
}

static int lambda_function(parser *p) {
    // fun a b c... -> rhs

    if (a_try_s(p, &the_symbol, "fun")) return 1;
    if (a_try(p, &lambda_declaration)) return 1;
    ast_node *decl = p->result;

    if (a_try(p, &function_definition)) return 1;
    ast_node *defn_h           = p->result;

    ast_node *node             = ast_node_create(p->ast_arena, ast_lambda_function);
    node->lambda_function.body = defn_h;

    // move the vector from the function_declaration node to the new ast node
    vec_move(&node->lambda_function.parameters, &decl->function_declaration.parameters);

    // reset moved-from vector to an empty vector so the node in the
    // ast_pool is still valid.
    // TODO: better would be to remove the node from the pool
    vec_init_empty(&decl->function_declaration.parameters, node->lambda_function.parameters.element_size);
    return result_ast_node(p, node);
}

static int lambda_function_application(parser *p) {

    if (a_try(p, &grouped_expression)) return 1;
    // a lambda application must be a grouped lambda function, i.e.
    // surrouneded by round braces
    ast_node *lambda = p->result;

    if (lambda->tag != ast_lambda_function) {
        p->error.tag = tess_err_expected_lambda;
        return 1;
    }

    // there must be at least one argument
    vector arguments;
    ast_vector_init(p->ast_arena, &arguments);

    if (a_try(p, &function_argument)) return 1;
    vec_push_back(p->parser_arena, &arguments, (void *)&p->result);

    while (true) {
        if (0 == a_try(p, &function_argument)) {
            vec_push_back(p->parser_arena, &arguments, (void *)&p->result);
            continue;
        }

        if (0 == a_try(p, &a_end_of_expression)) {
            ast_node *node = ast_node_create(p->ast_arena, ast_lambda_function_application);
            node->lambda_application.lambda = lambda;
            vec_move(&node->lambda_application.arguments, &arguments);
            return result_ast_node(p, node);
        }

        // anything else is an error
        p->error.tag = tess_err_expected_argument;
        return 1;
    }
}

static int simple_declaration(parser *p) {
    // a = ...
    // a single identifier followed by an equal sign
    if (a_try(p, a_identifier)) return 1;
    ast_node *sym = p->result;

    if (a_try(p, a_equal_sign)) return 1;

    return result_ast_node(p, sym);
}

static int let_in_form(parser *p) {
    // let a = 2 in expression
    if (a_try_s(p, &the_symbol, "let")) return 1;
    if (a_try(p, &simple_declaration)) return 1;
    ast_node *sym = p->result;

    if (a_try(p, &expression)) return 1;
    ast_node *defn = p->result;

    if (a_try_s(p, &the_symbol, "in")) return 1;
    if (a_try(p, &expression)) return 1;
    ast_node *body     = p->result;

    ast_node *node     = ast_node_create(p->ast_arena, ast_let_in);
    node->let_in.name  = sym;
    node->let_in.value = defn;
    node->let_in.body  = body;
    return result_ast_node(p, node);
}

static int let_form(parser *p) {
    // let f a b c... = ...
    if (a_try_s(p, &the_symbol, "let")) return 1;
    if (a_try(p, &function_declaration)) return 1;
    ast_node *decl = p->result;

    if (a_try(p, &function_definition)) return 1;
    ast_node *defn = p->result;

    ast_node *node = ast_node_create(p->ast_arena, ast_let);

    // get declaration out of pool to move into new node
    node->let.name = decl->function_declaration.name;
    node->let.body = defn;

    // move the vector from the function_declaration node to the new ast node
    vec_move(&node->let.parameters, &decl->function_declaration.parameters);
    // reset moved-from vector to an empty vector so the node in the
    // ast_pool is still valid.
    // TODO: better would be to remove the node from the pool
    vec_init_empty(&decl->function_declaration.parameters, node->let.parameters.element_size);
    return result_ast_node(p, node);
}

static int tuple_expression(parser *p) {

    if (a_try(p, a_open_round)) return 1;

    vector elements;
    ast_vector_init(p->ast_arena, &elements);

    // first, expect an expression, which must be followed by a comma
    // then, zero or more expressions before a close round. So (expr,)
    // is a valid tuple.
    if (a_try(p, &expression)) return 1;
    vec_push_back(p->ast_arena, &elements, (void *)&p->result);
    if (a_try(p, &a_comma)) goto cleanup;

    int count = 0;

    while (true) {

        if (0 == a_try(p, &a_close_round)) {
            ast_node *node = ast_node_create(p->ast_arena, ast_tuple);
            vec_move(&node->tuple.elements, &elements);
            return result_ast_node(p, node);
        }

        // comma required if this is not the first time through the loop
        if (count++ > 0)
            if (a_try(p, &a_comma)) goto cleanup;

        // expression
        if (0 == a_try(p, &expression)) vec_push_back(p->ast_arena, &elements, (void *)&p->result);

        // loop to check for close round, or else
    }

cleanup:
    vec_deinit(p->parser_arena, &elements);
    return 1;
}

static int grouped_expression(parser *parser) {
    if (a_try(parser, &a_open_round)) return 1;
    if (a_try(parser, &expression)) return 1;

    // save expression node result
    ast_node *const out = parser->result;

    if (a_try(parser, &a_close_round)) return 1;

    // replace parser result with expression node
    parser->result = out;
    return 0;
}

static int expression(parser *parser) {
    if (eat_newlines(parser)) return 1;

    if (0 == a_try(parser, &lambda_function_application)) return 0;
    if (0 == a_try(parser, &infix_operation)) return 0;
    if (0 == a_try(parser, &tuple_expression)) return 0;
    if (0 == a_try(parser, &grouped_expression)) return 0;
    if (0 == a_try(parser, &a_nil)) return 0;
    if (0 == a_try(parser, &lambda_function)) return 0;
    if (0 == a_try(parser, &let_in_form)) return 0;
    if (0 == a_try(parser, &let_form)) return 0;
    if (0 == a_try(parser, &if_then_else)) return 0;
    if (0 == a_try(parser, &function_application)) return 0;
    if (0 == a_try(parser, &a_identifier)) return 0;
    if (0 == a_try(parser, &a_number)) return 0;
    if (0 == a_try(parser, &a_bool)) return 0;
    if (0 == a_try(parser, &a_end_of_expression)) return 0;

    return 1;
}

int parser_next(parser *parser) {
    return expression(parser);
}

int parser_parse_all(allocator *alloc, parser *p, vector *out) {
    assert(sizeof(ast_node *) == out->element_size);

    int res = 0;
    while (0 == (res = parser_next(p))) {
        ast_node *node;
        parser_result(p, &node);
        vec_push_back(alloc, out, (void *)&node);
    }

    if (tess_err_tokenizer_error == p->error.tag && tess_err_eof == p->tokenizer_error.tag) return 0;

    return res;
}

void parser_result(parser *p, ast_node **handle) {
    if (handle) {
        *handle = p->result;
    }
}
