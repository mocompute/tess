#include "parser.h"
#include "ast.h"
#include "token.h"
#include "tokenizer.h"
#include "util.h"

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

  size_t                 result_ast_node_h; // pool handle, don't set directly

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

int parser_init(mos_allocator_t *alloc, parser_t *parser, ast_pool_t *pool, char const *input,
                size_t input_len) {

  memset(parser, 0, sizeof *parser);
  parser->alloc    = alloc;
  parser->ast_pool = pool;

  // tokenizer
  parser->tokenizer = tokenizer_alloc(alloc);
  if (!parser->tokenizer) return 1;
  if (tokenizer_init(alloc, parser->tokenizer, input, input_len)) return 1;

  // good_tokens
  mos_vector_init(&parser->good_tokens, sizeof(struct token));

  // error
  token_init(&parser->error_token, tok_invalid);
  parser->error.token     = &parser->error_token;
  parser->error.tokenizer = &parser->tokenizer_error;

  return 0;
}

void parser_deinit(parser_t *parser) {

  // error token
  token_deinit(parser->alloc, &parser->error_token);

  // good_tokens
  mos_vector_deinit(parser->alloc, &parser->good_tokens);

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

static int expression(parser_t *);
static int function_argument(parser_t *);
static int grouped_expression(parser_t *);
static int if_then_else(parser_t *);
static int infix_operand(parser_t *);
static int infix_operation(parser_t *);
static int lambda_function(parser_t *);
static int lambda_function_application(parser_t *);
static int let_in_form(parser_t *);
static int let_form(parser_t *);
static int tuple_expression(parser_t *);

// result_* functions construct a new ast_node and add it to the pool.
// The parser then no longer has a valid copy of the actual ast_node,
// it being replaced by a handle to an entry in the pool.

nodiscard static int result_ast(parser_t *p, ast_tag_t tag) {
  ast_node_t node;
  ast_node_init(&node, tag);
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result_ast_node_h);
}

nodiscard static int result_ast_i64(parser_t *p, int64_t val) {
  ast_node_t node;
  ast_node_init(&node, ast_i64);
  node.i64.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result_ast_node_h);
}

nodiscard static int result_ast_u64(parser_t *p, uint64_t val) {
  ast_node_t node;
  ast_node_init(&node, ast_u64);
  node.u64.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result_ast_node_h);
}

nodiscard static int result_ast_f64(parser_t *p, double val) {
  ast_node_t node;
  ast_node_init(&node, ast_f64);
  node.f64.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result_ast_node_h);
}

nodiscard static int result_ast_bool(parser_t *p, bool val) {
  ast_node_t node;
  ast_node_init(&node, ast_bool);
  node.bool_.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result_ast_node_h);
}

nodiscard static int result_ast_str(parser_t *p, ast_tag_t tag, char const *s) {
  ast_node_t node;
  ast_node_init(&node, tag);

  // TODO strings
  node.symbol.name = p->alloc->malloc(strlen(s) + 1); // syms and strs use the symbol union
  if (!node.symbol.name) return 1;
  strcpy(node.symbol.name, s);
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result_ast_node_h);
}

nodiscard static int result_ast_node(parser_t *p, ast_node_t *node) {
  return ast_pool_move_back(p->alloc, p->ast_pool, node, &p->result_ast_node_h);
}

static int result_ast_node_handle(parser_t *p, size_t handle) {
  p->result_ast_node_h = handle;
  return 0;
}

nodiscard static int eat_newlines(parser_t *p) {

  while (true) {
    if (tokenizer_next(p->alloc, p->tokenizer, &p->error_token, &p->tokenizer_error)) {
      p->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    token_tag_t const tag = p->error_token.tag;
    if (tok_comment == tag || tok_one_newline == tag || tok_two_newline == tag ||
        tok_newline_indent == tag) {
      continue;
    } else {
      if (tokenizer_put_back(p->alloc, p->tokenizer, &p->error_token, 1)) return 1;
      return result_ast(p, ast_eof);
    }
  }
}

nodiscard static int next_token(parser_t *p, token_t *out_tok) {
  while (true) {

    if (tokenizer_next(p->alloc, p->tokenizer, out_tok, &p->tokenizer_error)) {
      p->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    if (tok_comment == out_tok->tag) continue;

    return mos_vector_push_back(p->alloc, &p->good_tokens, out_tok);
  }
}

nodiscard static int a_try(parser_t *p, parse_fun fun) {
  size_t const save_toks = mos_vector_size(&p->good_tokens);
  if (fun(p)) {
    assert(mos_vector_size(&p->good_tokens) >= save_toks);
    if (tokenizer_put_back(p->alloc, p->tokenizer,
                           ((token_t const *)mos_vector_data(&p->good_tokens)) + save_toks,
                           mos_vector_size(&p->good_tokens) - save_toks))
      return 2; // TODO handle oom error

    if (mos_vector_resize(p->alloc, &p->good_tokens, save_toks)) return 1;

    return 1;
  }
  return 0;
}

static int a_try_s(parser_t *p, parse_fun_s fun, char const *arg) {
  size_t const save_toks = mos_vector_size(&p->good_tokens);
  if (fun(p, arg)) {
    assert(mos_vector_size(&p->good_tokens) >= save_toks);
    if (tokenizer_put_back(p->alloc, p->tokenizer,
                           ((token_t const *)mos_vector_data(&p->good_tokens)) + save_toks,
                           mos_vector_size(&p->good_tokens) - save_toks))
      return 2; // TODO handle oom error

    if (mos_vector_resize(p->alloc, &p->good_tokens, save_toks)) return 1;

    return 1;
  }
  return 0;
}

static int a_comma(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_comma == tok->tag) return result_ast_str(p, ast_symbol, ",");

  p->error.tag = tess_err_expected_comma;
  return 1;
}

static int a_open_round(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_open_round == tok->tag) return result_ast_str(p, ast_symbol, "(");

  p->error.tag = tess_err_expected_open_round;
  return 1;
}

static int a_close_round(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_close_round == tok->tag) return result_ast_str(p, ast_symbol, ")");

  p->error.tag = tess_err_expected_close_round;
  return 1;
}

static int a_end_of_expression(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_semicolon == tok->tag || tok_one_newline == tok->tag || tok_two_newline == tok->tag)
    return result_ast_str(p, ast_symbol, ";");

  p->error.tag = tess_err_unfinished_expression;
  return 1;
}

// static int a_symbol(parser_t *p) {
//   if (next_token(p, &p->error_token)) return 1;
//   token_t const *const tok = &p->error_token;

//   if (tok_symbol == tok->tag) return result_ast_str(p, ast_symbol, tok->s);

//   p->error.tag = tess_err_expected_close_round;
//   return 1;
// }

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

        return result_ast_str(parser, ast_symbol, tok->s);
      }
    }
  }

error:
  parser->error.tag = tess_err_expected_identifier;
  return 1;
}

static int a_infix_operator(parser_t *p) {
  // + - * /, relationals: < <= == <> >= >
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_symbol == tok->tag) {

    if (is_arithmetic_operator(tok->s) || is_relational_operator(tok->s))
      return result_ast_str(p, ast_symbol, tok->s);
  }

  p->error.tag = tess_err_expected_operator;
  return 1;
}

static int the_symbol(parser_t *p, char const *const want) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_symbol == tok->tag) {
    if (0 == strcmp(want, tok->s)) return result_ast_str(p, ast_symbol, tok->s);
  }

  p->error.tag = tess_err_expected_symbol;
  return 1;
}

static int a_string(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_string == tok->tag) return result_ast_str(p, ast_string, tok->s);

  p->error.tag = tess_err_expected_string;
  return 1;
}

static int string_to_number(parser_t *parser, char const *const in) {
  errno                 = 0;
  ptrdiff_t const len   = (ptrdiff_t)strlen(in);

  char           *p_end = 0;
  long long int   i     = strtoll(in, &p_end, 10);
  if (p_end - in == len && !errno) {
    return result_ast_i64(parser, i);

  } else {

    errno                    = 0;
    p_end                    = 0;
    unsigned long long int u = strtoull(in, &p_end, 10);
    if (p_end - in == len && !errno) {
      return result_ast_u64(parser, u);

    } else {

      errno    = 0;
      p_end    = 0;
      double d = strtod(in, &p_end);
      if (p_end - in == len && !errno) {
        return result_ast_f64(parser, d);
      }
    }
  }

  return 1;
}

// static int string_to_operator(char const *in, ast_operator_t *out) {
//   return string_to_ast_operator(in, out);
// }

static int a_number(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_number == tok->tag) {

    if (string_to_number(p, tok->s)) goto error;
    // sets parser result

    return 0;
  }

error:
  p->error.tag = tess_err_expected_number;
  return 1;
}

static int a_bool(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_symbol == tok->tag) {
    if (0 == strcmp("true", tok->s)) return result_ast_bool(p, true);
    if (0 == strcmp("false", tok->s)) return result_ast_bool(p, false);
  }

  p->error.tag = tess_err_expected_bool;
  return 1;
}

static int a_literal(parser_t *p) {
  if (0 == a_try(p, &a_string)) return 0;
  if (0 == a_try(p, &a_number)) return 0;
  if (0 == a_try(p, &a_bool)) return 0;
  p->error.tag = tess_err_expected_literal;
  return 1;
}

static int a_equal_sign(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_equal_sign == tok->tag) return result_ast_str(p, ast_symbol, "=");

  p->error.tag = tess_err_expected_equal_sign;
  return 1;
}

static int a_arrow(parser_t *p) {
  if (next_token(p, &p->error_token)) return 1;
  token_t const *const tok = &p->error_token;

  if (tok_arrow == tok->tag) return result_ast_str(p, ast_symbol, "->");

  p->error.tag = tess_err_expected_arrow;
  return 1;
}

static int a_nil(parser_t *p) {

  if ((0 == a_try(p, &a_open_round)) && (0 == a_try(p, &a_close_round))) return result_ast(p, ast_nil);

  p->error.tag = tess_err_expected_arrow;
  return 1;
}

static int function_declaration(parser_t *p) {
  // f a b c... = : only symbols allowed, terminated by =.
  // collect identifiers (or a single nil) until an equal sign

  if (a_try(p, &a_identifier)) return 1;

  size_t const name = p->result_ast_node_h; // function name

  mos_vector_t parameters;
  mos_vector_init(&parameters, sizeof(size_t));

  // check: f () declares function with no parameters
  if (0 == a_try(p, &a_nil)) {

    ast_node_t node;
    ast_node_init(&node, ast_function_declaration);
    node.function_declaration.name = name;
    memcpy(&node.function_declaration.parameters, &parameters, sizeof parameters);
    if (result_ast_node(p, &node)) return 1;

    return 0;
  }

  // accumulate identifiers as parameters until equal sign is seen
  while (true) {
    if (0 == a_try(p, &a_identifier)) {
      if (mos_vector_push_back(p->alloc, &parameters, &p->result_ast_node_h)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_equal_sign)) {

      ast_node_t node;
      ast_node_init(&node, ast_function_declaration);
      node.function_declaration.name = name;
      memcpy(&node.function_declaration.parameters, &parameters, sizeof parameters);
      if (result_ast_node(p, &node)) return 1;

      return 0;
    }

    // anything else is an error
    p->error.tag = tess_err_expected_argument;
    return 1;
  }
}

static int lambda_declaration(parser_t *p) {
  // a b c... -> : only symbols allowed, terminated by ->
  // collect identifiers (or a single nil) until an arrow

  mos_vector_t parameters;
  mos_vector_init(&parameters, sizeof(size_t));

  // accumulate identifiers as parameters until an arrow is seen
  while (true) {
    if (0 == a_try(p, &a_identifier)) {
      if (mos_vector_push_back(p->alloc, &parameters, &p->result_ast_node_h)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_arrow)) {

      ast_node_t node;
      ast_node_init(&node, ast_lambda_declaration);
      memcpy(&node.lambda_declaration.parameters, &parameters, sizeof parameters);
      if (result_ast_node(p, &node)) return 1;

      return 0;
    }

    // anything else is an error
    p->error.tag = tess_err_expected_argument;
    return 1;
  }
}

static int function_definition(parser_t *p) {
  return expression(p);
}

static int function_application(parser_t *p) {
  // f a b c ..., terminated by semicolon or one_newline or two_newline

  if (a_try(p, &a_identifier)) return 1;

  size_t const name = p->result_ast_node_h;

  mos_vector_t arguments;
  mos_vector_init(&arguments, sizeof(size_t));

  // must have at least one argument
  if (a_try(p, &function_argument)) return 1;
  if (mos_vector_push_back(p->alloc, &arguments, &p->result_ast_node_h)) return 1;

  while (true) {
    if (0 == a_try(p, &function_argument)) {
      if (mos_vector_push_back(p->alloc, &arguments, &p->result_ast_node_h)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_end_of_expression)) {

      ast_node_t node;
      ast_node_init(&node, ast_named_function_application);
      node.named_function_application.name = name;
      memcpy(&node.named_function_application.arguments, &arguments, sizeof arguments);
      if (result_ast_node(p, &node)) return 1;
    }

    p->error.tag = tess_err_expected_argument;
    return 1;
  }
}

//

static int function_argument(parser_t *p) {

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

static int if_then_else(parser_t *p) {

  size_t cond, yes, no;

  if (a_try_s(p, &the_symbol, "if")) return 1;
  if (a_try(p, &expression)) return 1;
  cond = p->result_ast_node_h;

  if (a_try_s(p, &the_symbol, "then")) return 1;
  if (a_try(p, &expression)) return 1;
  yes = p->result_ast_node_h;

  if (a_try_s(p, &the_symbol, "else")) return 1;
  if (a_try(p, &expression)) return 1;
  no = p->result_ast_node_h;

  ast_node_t node;
  ast_node_init(&node, ast_if_then_else);
  node.if_then_else.condition = cond;
  node.if_then_else.yes       = yes;
  node.if_then_else.no        = no;
  return result_ast_node(p, &node);
}

static int infix_operand(parser_t *p) {
  return function_argument(p);
}

static int infix_operation(parser_t *p) {
  // a * b

  if (a_try(p, &infix_operand)) return 1;
  size_t const lhs = p->result_ast_node_h;

  if (a_try(p, &a_infix_operator)) return 1;
  ast_node_t    *op_node = ast_pool_at(p->ast_pool, p->result_ast_node_h);

  ast_operator_t op;
  if (string_to_ast_operator(op_node->symbol.name, &op)) return 1;

  if (a_try(p, &infix_operand)) return 1;
  size_t const rhs = p->result_ast_node_h;

  ast_node_t   node;
  ast_node_init(&node, ast_infix);
  node.infix.left  = lhs;
  node.infix.right = rhs;
  node.infix.op    = op;
  return result_ast_node(p, &node);
}

static int lambda_function(parser_t *p) {
  // fun a b c... -> rhs

  if (a_try_s(p, &the_symbol, "fun")) return 1;
  if (a_try(p, &lambda_declaration)) return 1;
  size_t decl_h;
  parser_result(p, &decl_h);

  if (a_try(p, &function_definition)) return 1;
  size_t     defn_h = p->result_ast_node_h;

  ast_node_t node;
  ast_node_init(&node, ast_lambda_function);
  node.lambda_function.body = defn_h;

  // move the vector from the function_declaration node to the new ast node
  ast_node_t *decl = ast_pool_at(p->ast_pool, decl_h);
  memcpy(&node.lambda_function.parameters, &decl->function_declaration.parameters,
         sizeof node.lambda_function.parameters);
  decl->function_declaration.parameters.data = 0;
  mos_vector_clear(&decl->function_declaration.parameters);

  return result_ast_node(p, &node);
}

static int lambda_function_application(parser_t *p) {

  if (a_try(p, &grouped_expression)) return 1;
  // a lambda application must be a grouped lambda function, i.e.
  // surrouneded by round braces
  size_t lambda_h;
  parser_result(p, &lambda_h);
  if (ast_pool_at(p->ast_pool, lambda_h)->tag != ast_lambda_function) {
    p->error.tag = tess_err_expected_lambda;
    return 1;
  }

  // there must be at least one argument
  mos_vector_t arguments;
  mos_vector_init(&arguments, sizeof(size_t));

  if (a_try(p, &function_argument)) return 1;
  if (mos_vector_push_back(p->alloc, &arguments, &p->result_ast_node_h)) return 1;

  while (true) {
    if (0 == a_try(p, &function_argument)) {
      if (mos_vector_push_back(p->alloc, &arguments, &p->result_ast_node_h)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_end_of_expression)) {
      ast_node_t node;
      ast_node_init(&node, ast_lambda_function_application);
      node.lambda_function_application.lambda = lambda_h;
      memcpy(&node.lambda_function_application.arguments, &arguments, sizeof arguments);
      return result_ast_node(p, &node);
    }

    // anything else is an error
    p->error.tag = tess_err_expected_argument;
    return 1;
  }
}

static int simple_declaration(parser_t *p) {
  // a = ...
  // a single identifier followed by an equal sign
  if (a_try(p, a_identifier)) return 1;
  size_t sym = p->result_ast_node_h;

  if (a_try(p, a_equal_sign)) return 1;

  return result_ast_node_handle(p, sym);
}

static int let_in_form(parser_t *p) {
  // let a = 2 in expression
  if (a_try_s(p, &the_symbol, "let")) return 1;
  if (a_try(p, &simple_declaration)) return 1;
  size_t sym = p->result_ast_node_h;

  if (a_try(p, &expression)) return 1;
  size_t defn = p->result_ast_node_h;

  if (a_try_s(p, &the_symbol, "in")) return 1;
  if (a_try(p, &expression)) return 1;
  size_t     body = p->result_ast_node_h;

  ast_node_t node;
  ast_node_init(&node, ast_let_in);
  node.let_in.name  = sym;
  node.let_in.value = defn;
  node.let_in.body  = body;
  return result_ast_node(p, &node);
}

static int let_form(parser_t *p) {
  // let f a b c... = ...
  if (a_try_s(p, &the_symbol, "let")) return 1;
  if (a_try(p, &function_declaration)) return 1;
  size_t decl_h = p->result_ast_node_h;

  if (a_try(p, &function_definition)) return 1;
  size_t     defn_h = p->result_ast_node_h;

  ast_node_t node;
  ast_node_init(&node, ast_let);

  // get declaration out of pool to move into new node
  ast_node_t *decl = ast_pool_at(p->ast_pool, decl_h);
  node.let.name    = decl->function_declaration.name;
  node.let.body    = defn_h;

  // move the vector from the function_declaration node to the new ast node
  memcpy(&node.let.parameters, &decl->function_declaration.parameters, sizeof node.let.parameters);
  decl->function_declaration.parameters.data = 0;
  mos_vector_clear(&decl->function_declaration.parameters);

  return result_ast_node(p, &node);
}

static int tuple_expression(parser_t *p) {

  if (a_try(p, a_open_round)) return 1;

  mos_vector_t elements;
  mos_vector_init(&elements, sizeof(size_t));

  // first, expect an expression followed by a comma
  if (a_try(p, &expression)) return 1;
  if (mos_vector_push_back(p->alloc, &elements, &p->result_ast_node_h)) goto cleanup;
  if (a_try(p, &a_comma)) goto cleanup;

  // then, zero or more expressions before a close round. So (expr,)
  // is a valid tuple.
  while (true) {
    if (0 == a_try(p, &a_close_round)) {
      ast_node_t node;
      ast_node_init(&node, ast_tuple);
      memcpy(&node.tuple.elements, &elements, sizeof elements);
      return result_ast_node(p, &node);
    }

    // expression
    if (0 == a_try(p, &expression))
      if (mos_vector_push_back(p->alloc, &elements, &p->result_ast_node_h)) goto cleanup;

    // optional comma
    // TODO this parser accepts (expr,,) as a valid tuple equivalent to (expr,)
    if (0 == a_try(p, &a_comma))
      ;

    // loop to check for close round
  }

cleanup:
  mos_vector_deinit(p->alloc, &elements);
  return 1;
}

static int grouped_expression(parser_t *parser) {
  if (a_try(parser, &a_open_round)) return 1;
  if (a_try(parser, &expression)) return 1;

  // save expression node result
  size_t const out_h = parser->result_ast_node_h;

  if (a_try(parser, &a_close_round)) return 1;

  // replace parser result with expression node
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

void parser_result(parser_t *p, size_t *handle) {
  if (handle) {
    *handle = p->result_ast_node_h;
  }
}
