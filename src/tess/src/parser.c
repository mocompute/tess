#include "parser.h"
#include "ast.h"
#include "token.h"
#include "tokenizer.h"

#include "alloc.h"
#include "vector.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

struct parser {
  mos_allocator_t       *alloc;
  tokenizer_t           *tokenizer;
  ast_pool_t            *ast_pool;

  struct ast_node        result_ast_node;   // parse result
  size_t                 result_ast_node_h; // pool handle

  struct mos_vector      good_tokens; // for backtracking

  struct parser_error    error;
  struct tokenizer_error tokenizer_error; // does not require deinit
  struct token           error_token;     // requires deinit
};

// -- statics --

static bool is_reserved(char const *s) {
  static char const *strings[] = {
    "if", "then", "else", "fun", "let", "in", "true", "false", NULL,
  };
  char const **it = strings;
  while (*it != NULL)
    if (0 == strcmp(*it++, s)) return true;

  return false;
}

static bool is_arithmetic_operator(char const *s) {
  static char const *strings[] = {
    "+", "-", "*", "/", NULL,
  };
  char const **it = strings;
  while (*it != NULL)
    if (0 == strcmp(*it++, s)) return true;
  return false;
}

static bool is_relational_operator(char const *s) {
  static char const *strings[] = {
    "<", "<=", "==", "<>", ">=", ">", NULL,
  };
  char const **it = strings;
  while (*it != NULL)
    if (0 == strcmp(*it++, s)) return true;
  return false;
}

// -- allocation and deallocation --

parser_t *parser_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(struct parser));
}

void parser_dealloc(mos_allocator_t *alloc, parser_t **parser) {
  alloc->free(*parser);
  *parser = 0;
}

int parser_init(mos_allocator_t *alloc, parser_t *parser, char const *input, size_t input_len) {

  memset(parser, 0, sizeof *parser);
  parser->alloc = alloc;

  // tokenizer
  parser->tokenizer = tokenizer_alloc(alloc);
  if (!parser->tokenizer) return 1;
  tokenizer_init(alloc, parser->tokenizer, input, input_len);

  // ast pool
  parser->ast_pool = ast_pool_alloc(alloc);
  if (!parser->ast_pool) return 1;
  ast_pool_init(alloc, parser->ast_pool);

  // result_ast_node
  ast_node_init(&parser->result_ast_node, tess_ast_eof);

  // good_tokens
  mos_vector_init(&parser->good_tokens, sizeof(struct token));

  // error
  token_init(&parser->error_token, tok_invalid);
  parser->error.token     = &parser->error_token;
  parser->error.tokenizer = &parser->tokenizer_error;

  return 0;
}

void parser_deinit(parser_t *parser) {

  // error
  token_deinit(parser->alloc, &parser->error_token);

  // good_tokens
  mos_vector_deinit(parser->alloc, &parser->good_tokens);

  // result_ast_node
  ast_node_deinit(parser->alloc, &parser->result_ast_node);

  // ast_pool
  if (parser->ast_pool) {
    ast_pool_deinit(parser->alloc, parser->ast_pool);
    ast_pool_dealloc(parser->alloc, &parser->ast_pool);
  }

  // tokenizer
  if (parser->tokenizer) {
    tokenizer_deinit(parser->alloc, parser->tokenizer);
    tokenizer_dealloc(parser->alloc, &parser->tokenizer);
  }

  mos_alloc_invalidate(parser, sizeof *parser);
}

// -- parser --

typedef int (*parse_fun)(parser_t *);
typedef int (*parse_fun_s)(parser_t *, char const *);

static int  expression(parser_t *);
static int  function_argument(parser_t *);
static int  grouped_expression(parser_t *);
static int  if_then_else(parser_t *);
static int  infix_operand(parser_t *);
static int  infix_operation(parser_t *);
static int  lambda_function(parser_t *);
static int  lambda_function_application(parser_t *);
static int  let_in_form(parser_t *);
static int  let_form(parser_t *);
static int  tuple_expression(parser_t *);

static void result_ast(parser_t *parser, ast_tag_t tag) {
  ast_node_replace(parser->alloc, &parser->result_ast_node, tag);
}

static void result_ast_i64(parser_t *parser, int64_t val) {
  ast_node_replace(parser->alloc, &parser->result_ast_node, tess_ast_i64);
  parser->result_ast_node.i64_val = val;
}

static void result_ast_u64(parser_t *parser, uint64_t val) {
  ast_node_replace(parser->alloc, &parser->result_ast_node, tess_ast_u64);
  parser->result_ast_node.u64_val = val;
}

static void result_ast_f64(parser_t *parser, double val) {
  ast_node_replace(parser->alloc, &parser->result_ast_node, tess_ast_f64);
  parser->result_ast_node.f64_val = val;
}

static void result_ast_bool(parser_t *parser, bool val) {
  ast_node_replace(parser->alloc, &parser->result_ast_node, tess_ast_bool);
  parser->result_ast_node.bool_val = val;
}

static int result_ast_str(parser_t *parser, ast_tag_t tag, char const *s) {

  ast_node_replace(parser->alloc, &parser->result_ast_node, tag);

  // TODO strings
  parser->result_ast_node.name = parser->alloc->malloc(strlen(s) + 1);
  if (!parser->result_ast_node.name) return 1;
  strcpy(parser->result_ast_node.name, s);
  return 0;
}

static int result_ast_node(parser_t *parser, ast_node_t *node) {

  ast_node_deinit(parser->alloc, &parser->result_ast_node);

  // add to pool and then to parser result data
  size_t handle;
  if (ast_pool_move_back(parser->alloc, parser->ast_pool, node, &handle)) return 1;

  ast_node_t *in_pool = ast_pool_at(parser->ast_pool, handle);
  memcpy(&parser->result_ast_node, in_pool, sizeof parser->result_ast_node);

  return 0;
}

static int eat_newlines(parser_t *parser) {

  while (true) {
    if (tokenizer_next(parser->alloc, parser->tokenizer, &parser->error_token, &parser->tokenizer_error)) {
      parser->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    token_tag_t const tag = parser->error_token.tag;
    if (tok_comment == tag || tok_one_newline == tag || tok_two_newline == tag ||
        tok_newline_indent == tag) {
      continue;
    } else {
      tokenizer_put_back(parser->alloc, parser->tokenizer, &parser->error_token, 1);
      result_ast(parser, tess_ast_eof);
      return 0;
    }
  }
}

static int next_token(parser_t *parser, token_t *out_tok) {
  while (true) {

    if (tokenizer_next(parser->alloc, parser->tokenizer, out_tok, &parser->tokenizer_error)) {
      parser->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    if (tok_comment == out_tok->tag) continue;

    mos_vector_push_back(parser->alloc, &parser->good_tokens, out_tok);
    return 0;
  }
}

static int a_try(parser_t *parser, parse_fun fun) {
  size_t const save_toks = mos_vector_size(&parser->good_tokens);
  if (fun(parser)) {
    assert(mos_vector_size(&parser->good_tokens) >= save_toks);
    tokenizer_put_back(parser->alloc, parser->tokenizer,
                       (token_t const *)mos_vector_data(&parser->good_tokens) + save_toks,
                       mos_vector_size(&parser->good_tokens) - save_toks);
    return 1;
  }
  return 0;
}

static int a_try_s(parser_t *parser, parse_fun_s fun, char const *arg) {
  size_t const save_toks = mos_vector_size(&parser->good_tokens);
  if (fun(parser, arg)) {
    assert(mos_vector_size(&parser->good_tokens) >= save_toks);
    tokenizer_put_back(parser->alloc, parser->tokenizer,
                       (token_t const *)mos_vector_data(&parser->good_tokens) + save_toks,
                       mos_vector_size(&parser->good_tokens) - save_toks);
    return 1;
  }
  return 0;
}

static int a_comma(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_comma == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, ",");
    return 0;
  }

  parser->error.tag = tess_err_expected_comma;
  return 1;
}

static int a_open_round(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_open_round == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, "(");
    return 0;
  }

  parser->error.tag = tess_err_expected_open_round;
  return 1;
}

static int a_close_round(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_close_round == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, ")");
    return 0;
  }

  parser->error.tag = tess_err_expected_close_round;
  return 1;
}

static int a_end_of_expression(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_semicolon == tok->tag || tok_one_newline == tok->tag || tok_two_newline == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, ";");
    return 0;
  }

  parser->error.tag = tess_err_unfinished_expression;
  return 1;
}

static int a_symbol(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_symbol == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, tok->s);
    return 0;
  }

  parser->error.tag = tess_err_expected_close_round;
  return 1;
}

static int a_identifier(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_symbol == tok->tag) {
    if (0 != strlen(tok->s) && !is_reserved(tok->s)) {

      char const c = tok->s[0];
      if (('_' == c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {

        // first character good, check remaining characters
        char const *p = &tok->s[1];
        while (*p) {
          if ('_' == *p || ('a' <= *p && *p <= 'z') || ('A' <= *p && *p <= 'Z') ||
              ('0' <= *p && *p <= '9')) {
            p++; // still good
          } else {
            goto error;
          }
        }

        // check for reserved words, which are not allowed as identifiers
        if (is_reserved(tok->s)) goto error;

        result_ast_str(parser, tess_ast_symbol, tok->s);
        return 0;
      }
    }
  }

error:
  parser->error.tag = tess_err_expected_identifier;
  return 1;
}

static int a_infix_operator(parser_t *parser) {
  // + - * /, relationals: < <= == <> >= >
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_symbol == tok->tag) {

    if (is_arithmetic_operator(tok->s) || is_relational_operator(tok->s)) {
      result_ast_str(parser, tess_ast_symbol, tok->s);
      return 0;
    }
  }

  parser->error.tag = tess_err_expected_operator;
  return 1;
}

static int the_symbol(parser_t *parser, char const *const want) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_symbol == tok->tag) {
    if (0 == strcmp(want, tok->s)) {
      result_ast_str(parser, tess_ast_symbol, tok->s);
      return 0;
    }
  }

  parser->error.tag = tess_err_expected_symbol;
  return 1;
}

static int a_string(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_string == tok->tag) {
    result_ast_str(parser, tess_ast_string, tok->s);
    return 0;
  }

  parser->error.tag = tess_err_expected_string;
  return 1;
}

static int string_to_number(parser_t *parser, char const *const in) {
  errno                 = 0;
  ptrdiff_t const len   = (ptrdiff_t)strlen(in);

  char           *p_end = 0;
  long long int   i     = strtoll(in, &p_end, 10);
  if (p_end - in == len && !errno) {
    result_ast_i64(parser, i);
    return 0;

  } else {

    errno                    = 0;
    p_end                    = 0;
    unsigned long long int u = strtoull(in, &p_end, 10);
    if (p_end - in == len && !errno) {
      result_ast_u64(parser, u);
      return 0;

    } else {

      errno    = 0;
      p_end    = 0;
      double d = strtod(in, &p_end);
      if (p_end - in == len && !errno) {
        result_ast_f64(parser, d);
        return 0;
      }
    }
  }

  return 1;
}

static int string_to_operator(char const *in, ast_operator_t *out) {
  return string_to_ast_operator(in, out);
}

static int a_number(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_number == tok->tag) {

    if (string_to_number(parser, tok->s)) goto error;
    // sets parser result

    return 0;
  }

error:
  parser->error.tag = tess_err_expected_number;
  return 1;
}

static int a_bool(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_symbol == tok->tag) {
    if (0 == strcmp("true", tok->s)) {
      result_ast_bool(parser, true);
      return 0;
    }
    if (0 == strcmp("false", tok->s)) {
      result_ast_bool(parser, false);
      return 0;
    }
  }

  parser->error.tag = tess_err_expected_bool;
  return 1;
}

static int a_literal(parser_t *parser) {
  if (0 == a_try(parser, &a_string)) return 0;
  if (0 == a_try(parser, &a_number)) return 0;
  if (0 == a_try(parser, &a_bool)) return 0;
  parser->error.tag = tess_err_expected_literal;
  return 1;
}

static int a_equal_sign(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_equal_sign == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, "=");
    return 0;
  }

  parser->error.tag = tess_err_expected_equal_sign;
  return 1;
}

static int a_arrow(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) return 1;
  token_t const *const tok = &parser->error_token;

  if (tok_arrow == tok->tag) {
    result_ast_str(parser, tess_ast_symbol, "->");
    return 0;
  }

  parser->error.tag = tess_err_expected_arrow;
  return 1;
}

static int a_nil(parser_t *parser) {

  if ((0 == a_try(parser, &a_open_round)) && (0 == a_try(parser, &a_close_round))) {
    result_ast(parser, tess_ast_nil);
    return 0;
  }

  parser->error.tag = tess_err_expected_arrow;
  return 1;
}

static int function_declaration(parser_t *parser) {
  // f a b c... = : only symbols allowed, terminated by =.
  // collect identifiers (or a single nil) until an equal sign

  if (a_try(parser, &a_identifier)) return 1;

  size_t const name = parser->result_ast_node_h; // function name

  mos_vector_t parameters;
  mos_vector_init(&parameters, sizeof(size_t));

  // check: f () declares function with no parameters
  if (0 == a_try(parser, &a_nil)) {

    // FIXME this won't work, need interface to add node to the pool

    ast_node_t *result = &parser->result_ast_node;
    ast_node_replace(parser->alloc, result, tess_ast_function_declaration);
    result->function_declaration.name = name;
    memcpy(&result->function_declaration.parameters, &parameters, sizeof parameters);

    return 0;
  }

  // accumulate identifiers as parameters until equal sign is seen
  while (true) {
    if (0 == a_try(parser, &a_identifier)) {
      mos_vector_push_back(parser->alloc, &parameters, &parser->result_ast_node_h);
      continue;
    }

    if (0 == a_try(parser, &a_equal_sign)) {
      ast_node_t *result = &parser->result_ast_node;
      ast_node_replace(parser->alloc, result, tess_ast_function_declaration);

      // FIXME this won't work, need interface to add node to the pool
      result->function_declaration.name = name;
      memcpy(&result->function_declaration.parameters, &parameters, sizeof parameters);

      return 0;
    }

    // anything else is an error
    parser->error.tag = tess_err_expected_argument;
    return 1;
  }
}

static int lambda_declaration(parser_t *parser) {
  // a b c... -> : only symbols allowed, terminated by ->
  // collect identifiers (or a single nil) until an arrow

  mos_vector_t parameters;
  mos_vector_init(&parameters, sizeof(size_t));

  // accumulate identifiers as parameters until an arrow is seen
  while (true) {
    if (0 == a_try(parser, &a_identifier)) {
      mos_vector_push_back(parser->alloc, &parameters, &parser->result_ast_node_h);
      continue;
    }

    if (0 == a_try(parser, &a_arrow)) {

      // FIXME this won't work, need interface to add node to the pool
      ast_node_t *result = &parser->result_ast_node;
      ast_node_replace(parser->alloc, result, tess_ast_lambda_declaration);
      memcpy(&result->lambda_declaration.parameters, &parameters, sizeof parameters);

      return 0;
    }

    // anything else is an error
    parser->error.tag = tess_err_expected_argument;
    return 1;
  }
}

static int function_definition(parser_t *p) {
  return expression(p);
}

static int function_application(parser_t *parser) {
  // f a b c ..., terminated by semicolon or one_newline or two_newline

  if (a_try(parser, &a_identifier)) return 1;

  size_t const name = parser->result_ast_node_h;

  mos_vector_t arguments;
  mos_vector_init(&arguments, sizeof(size_t));

  // must have at least one argument

  if (a_try(parser, &function_argument)) return 1;
  mos_vector_push_back(parser->alloc, &arguments, &parser->result_ast_node_h);

  while (true) {
    if (0 == a_try(parser, &function_argument)) {
      mos_vector_push_back(parser->alloc, &arguments, &parser->result_ast_node_h);
      continue;
    }

    if (0 == a_try(parser, &a_end_of_expression)) {
      // FIXME this won't work, need interface to add node to the pool
      ast_node_t *result = &parser->result_ast_node;
      ast_node_replace(parser->alloc, result, tess_ast_named_function_application);
      result->named_function_application.name = name;
      memcpy(&result->named_function_application.arguments, &arguments, sizeof arguments);
    }

    parser->error.tag = tess_err_expected_argument;
    return 1;
  }
}

//

static int function_argument(parser_t *parser) {

  if (0 == a_try(parser, &lambda_function_application)) return 0;
  if (0 == a_try(parser, &grouped_expression)) return 0;
  if (0 == a_try(parser, &let_in_form)) return 0;
  if (0 == a_try(parser, &if_then_else)) return 0;
  if (0 == a_try(parser, &a_nil)) return 0;
  if (0 == a_try(parser, &a_identifier)) return 0;
  if (0 == a_try(parser, &a_literal)) return 0;
  if (0 == a_try(parser, &a_end_of_expression)) {
    // if looking for a function argument, consider end of expression as an eof error
    parser->error.tag = tess_err_eof;
    return 1;
  }

  return 1;
}

static int infix_operand(parser_t *parser) {
  return function_argument(parser);
}

static int if_then_else(parser_t *parser) {

  size_t cond, yes, no;

  if (a_try_s(parser, &the_symbol, "if")) return 1;
  if (a_try(parser, &expression)) return 1;
  cond = parser->result_ast_node_h;

  if (a_try_s(parser, &the_symbol, "then")) return 1;
  if (a_try(parser, &expression)) return 1;
  yes = parser->result_ast_node_h;

  if (a_try_s(parser, &the_symbol, "else")) return 1;
  if (a_try(parser, &expression)) return 1;
  no = parser->result_ast_node_h;

  // FIXME this won't work, need interface to add node to the pool
  ast_node_deinit(parser->alloc, parser->result_ast_node);
}

static int grouped_expression(parser_t *parser) {
  if (a_try(parser, &a_open_round)) return 1;
  if (a_try(parser, &expression)) return 1;

  // save expression node result
  size_t const out_h = parser->result_ast_node_h;
  ast_node_t   out;
  memcpy(&out, &parser->result_ast_node, sizeof out);

  if (a_try(parser, &a_close_round)) return 1;

  // replace parser result with expression node
  ast_node_deinit(parser->alloc, &parser->result_ast_node);
  memcpy(&parser->result_ast_node, &out, sizeof parser->result_ast_node);
  parser->result_ast_node_h = out_h;
  return 0;
}

static int expression(parser_t *parser) {
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

int parser_next(parser_t *parser) {
  return expression(parser);
}

void parser_result(parser_t *parser, ast_node_t **node, size_t *handle) {
  if (node) {
    *node = &parser->result_ast_node;
  }
  if (handle) {
    *handle = parser->result_ast_node_h;
  }
}
