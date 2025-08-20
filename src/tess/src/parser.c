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
  mos_allocator         *alloc;
  tokenizer             *tokenizer;
  ast_pool              *ast_pool;

  ast_node_h             result; // pool handle, don't set directly

  struct mos_vector      seen_tokens; // for backtracking

  struct parser_error    error;
  struct tokenizer_error tokenizer_error; // does not require deinit
  struct token           token;           // requires deinit
};

// -- allocation and deallocation --

parser *parser_alloc(mos_allocator *alloc) {
  return alloc->malloc(sizeof(struct parser));
}

void parser_dealloc(mos_allocator *alloc, parser **p) {
  alloc->free(*p);
  *p = 0;
}

int parser_init(mos_allocator *alloc, parser *p, ast_pool *pool, char const *input, size_t input_len) {

  memset(p, 0, sizeof *p);
  p->alloc    = alloc;
  p->ast_pool = pool;

  // tokenizer
  p->tokenizer = tokenizer_alloc(alloc);
  if (!p->tokenizer) return 1;
  if (tokenizer_init(alloc, p->tokenizer, input, input_len)) return 1;

  // good_tokens
  mos_vector_init(&p->seen_tokens, sizeof(struct token));

  // error
  token_init(&p->token, tok_invalid);
  p->error.token     = &p->token;
  p->error.tokenizer = &p->tokenizer_error;

  return 0;
}

void parser_deinit(parser *p) {

  // error token
  token_deinit(p->alloc, &p->token);

  // good_tokens
  mos_vector_deinit(p->alloc, &p->seen_tokens);

  // tokenizer
  if (p->tokenizer) {
    tokenizer_deinit(p->alloc, p->tokenizer);
    tokenizer_dealloc(p->alloc, &p->tokenizer);
  }

  mos_alloc_invalidate(p, sizeof *p);
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

nodiscard static int result_ast(parser *p, ast_tag tag) {
  ast_node node;
  ast_node_init(&node, tag);
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result);
}

nodiscard static int result_ast_i64(parser *p, int64_t val) {
  ast_node node;
  ast_node_init(&node, ast_i64);
  node.i64.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result);
}

nodiscard static int result_ast_u64(parser *p, uint64_t val) {
  ast_node node;
  ast_node_init(&node, ast_u64);
  node.u64.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result);
}

nodiscard static int result_ast_f64(parser *p, double val) {
  ast_node node;
  ast_node_init(&node, ast_f64);
  node.f64.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result);
}

nodiscard static int result_ast_bool(parser *p, bool val) {
  ast_node node;
  ast_node_init(&node, ast_bool);
  node.bool_.val = val;
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result);
}

nodiscard static int result_ast_str(parser *p, ast_tag tag, char const *s) {
  ast_node node;
  ast_node_init(&node, tag);

  // TODO strings
  node.symbol.name = p->alloc->malloc(strlen(s) + 1); // syms and strs use the symbol union
  if (!node.symbol.name) return 1;
  strcpy(node.symbol.name, s);
  return ast_pool_move_back(p->alloc, p->ast_pool, &node, &p->result);
}

nodiscard static int result_ast_node(parser *p, ast_node *node) {
  return ast_pool_move_back(p->alloc, p->ast_pool, node, &p->result);
}

static int result_ast_node_handle(parser *p, ast_node_h handle) {
  p->result = handle;
  return 0;
}

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

nodiscard static int eat_newlines(parser *p) {

  while (true) {
    if (tokenizer_next(p->alloc, p->tokenizer, &p->token, &p->tokenizer_error)) {
      p->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    token_tag const tag = p->token.tag;
    if (tok_comment == tag || tok_one_newline == tag || tok_two_newline == tag ||
        tok_newline_indent == tag) {
      continue;
    } else {
      if (tokenizer_put_back(p->alloc, p->tokenizer, &p->token, 1)) return 1;
      return result_ast(p, ast_eof);
    }
  }
}

nodiscard static int next_token(parser *p) {
  while (true) {

    if (tokenizer_next(p->alloc, p->tokenizer, &p->token, &p->tokenizer_error)) {
      p->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    if (tok_comment == p->token.tag) continue;

    return mos_vector_push_back(p->alloc, &p->seen_tokens, &p->token);
  }
}

nodiscard static int a_try(parser *p, parse_fun fun) {
  size_t const save_toks = mos_vector_size(&p->seen_tokens);
  if (fun(p)) {
    assert(mos_vector_size(&p->seen_tokens) >= save_toks);
    if (tokenizer_put_back(p->alloc, p->tokenizer,
                           ((token const *)mos_vector_data(&p->seen_tokens)) + save_toks,
                           mos_vector_size(&p->seen_tokens) - save_toks))
      return 2; // TODO handle oom error

    if (mos_vector_resize(p->alloc, &p->seen_tokens, save_toks)) return 1;

    return 1;
  }
  return 0;
}

static int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
  size_t const save_toks = mos_vector_size(&p->seen_tokens);
  if (fun(p, arg)) {
    assert(mos_vector_size(&p->seen_tokens) >= save_toks);
    if (tokenizer_put_back(p->alloc, p->tokenizer,
                           ((token const *)mos_vector_data(&p->seen_tokens)) + save_toks,
                           mos_vector_size(&p->seen_tokens) - save_toks))
      return 2; // TODO handle oom error

    if (mos_vector_resize(p->alloc, &p->seen_tokens, save_toks)) return 1;

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
  if (next_token(p)) return 1;

  if (tok_semicolon == p->token.tag || tok_one_newline == p->token.tag || tok_two_newline == p->token.tag)
    return result_ast_str(p, ast_symbol, ";");

  p->error.tag = tess_err_unfinished_expression;
  return 1;
}

// static int a_symbol(parser *p) {
//   if (next_token(p, &p->error_token)) return 1;
//   token const *const tok = &p->error_token;

//   if (tok_symbol == p->token.tag) return result_ast_str(p, ast_symbol, p->token.s);

//   p->error.tag = tess_err_expected_close_round;
//   return 1;
// }

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

// static int string_to_operator(char const *in, ast_operator *out) {
//   return string_to_ast_operator(in, out);
// }

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

  ast_node_h const name = p->result; // function name

  mos_vector       parameters;
  ast_vector_init(&parameters);

  // check: f () declares function with no parameters
  if (0 == a_try(p, &a_nil)) {

    ast_node node;
    ast_node_init(&node, ast_function_declaration);
    node.function_declaration.name = name;
    mos_vector_move(&node.function_declaration.parameters, &parameters);
    if (result_ast_node(p, &node)) return 1;

    return 0;
  }

  // accumulate identifiers as parameters until equal sign is seen
  while (true) {
    if (0 == a_try(p, &a_identifier)) {
      if (mos_vector_push_back(p->alloc, &parameters, &p->result)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_equal_sign)) {

      ast_node node;
      ast_node_init(&node, ast_function_declaration);
      node.function_declaration.name = name;
      mos_vector_move(&node.function_declaration.parameters, &parameters);
      if (result_ast_node(p, &node)) return 1;

      return 0;
    }

    // anything else is an error
    p->error.tag = tess_err_expected_argument;
    return 1;
  }
}

static int lambda_declaration(parser *p) {
  // a b c... -> : only symbols allowed, terminated by ->

  mos_vector parameters;
  ast_vector_init(&parameters);

  // accumulate identifiers as parameters until an arrow is seen
  while (true) {
    if (0 == a_try(p, &a_identifier)) {
      if (mos_vector_push_back(p->alloc, &parameters, &p->result)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_arrow)) {

      ast_node node;
      ast_node_init(&node, ast_lambda_declaration);
      mos_vector_move(&node.lambda_declaration.parameters, &parameters);
      if (result_ast_node(p, &node)) return 1;

      return 0;
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

  ast_node_h const name = p->result;

  mos_vector       arguments;
  ast_vector_init(&arguments);

  // must have at least one argument
  if (a_try(p, &function_argument)) return 1;
  if (mos_vector_push_back(p->alloc, &arguments, &p->result)) return 1;

  while (true) {
    if (0 == a_try(p, &function_argument)) {
      if (mos_vector_push_back(p->alloc, &arguments, &p->result)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_end_of_expression)) {

      ast_node node;
      ast_node_init(&node, ast_named_function_application);
      node.named_function_application.name = name;
      mos_vector_move(&node.named_function_application.arguments, &arguments);
      if (result_ast_node(p, &node)) return 1;
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

  ast_node_h cond, yes, no;

  if (a_try_s(p, &the_symbol, "if")) return 1;
  if (a_try(p, &expression)) return 1;
  cond = p->result;

  if (a_try_s(p, &the_symbol, "then")) return 1;
  if (a_try(p, &expression)) return 1;
  yes = p->result;

  if (a_try_s(p, &the_symbol, "else")) return 1;
  if (a_try(p, &expression)) return 1;
  no = p->result;

  ast_node node;
  ast_node_init(&node, ast_if_then_else);
  node.if_then_else.condition = cond;
  node.if_then_else.yes       = yes;
  node.if_then_else.no        = no;
  return result_ast_node(p, &node);
}

static int infix_operand(parser *p) {
  return function_argument(p);
}

static int infix_operation(parser *p) {
  // a * b

  if (a_try(p, &infix_operand)) return 1;
  ast_node_h const lhs = p->result;

  if (a_try(p, &a_infix_operator)) return 1;
  ast_node    *op_node = ast_pool_at(p->ast_pool, p->result);

  ast_operator op;
  if (string_to_ast_operator(op_node->symbol.name, &op)) return 1;

  if (a_try(p, &infix_operand)) return 1;
  ast_node_h const rhs = p->result;

  ast_node         node;
  ast_node_init(&node, ast_infix);
  node.infix.left  = lhs;
  node.infix.right = rhs;
  node.infix.op    = op;
  return result_ast_node(p, &node);
}

static int lambda_function(parser *p) {
  // fun a b c... -> rhs

  if (a_try_s(p, &the_symbol, "fun")) return 1;
  if (a_try(p, &lambda_declaration)) return 1;
  ast_node_h decl_h;
  parser_result(p, &decl_h);

  if (a_try(p, &function_definition)) return 1;
  ast_node_h defn_h = p->result;

  ast_node   node;
  ast_node_init(&node, ast_lambda_function);
  node.lambda_function.body = defn_h;

  // move the vector from the function_declaration node to the new ast node
  ast_node *decl = ast_pool_at(p->ast_pool, decl_h);
  mos_vector_move(&node.lambda_function.parameters, &decl->function_declaration.parameters);
  return result_ast_node(p, &node);
}

static int lambda_function_application(parser *p) {

  if (a_try(p, &grouped_expression)) return 1;
  // a lambda application must be a grouped lambda function, i.e.
  // surrouneded by round braces
  ast_node_h lambda_h;
  parser_result(p, &lambda_h);
  if (ast_pool_at(p->ast_pool, lambda_h)->tag != ast_lambda_function) {
    p->error.tag = tess_err_expected_lambda;
    return 1;
  }

  // there must be at least one argument
  mos_vector arguments;
  ast_vector_init(&arguments);

  if (a_try(p, &function_argument)) return 1;
  if (mos_vector_push_back(p->alloc, &arguments, &p->result)) return 1;

  while (true) {
    if (0 == a_try(p, &function_argument)) {
      if (mos_vector_push_back(p->alloc, &arguments, &p->result)) return 1;
      continue;
    }

    if (0 == a_try(p, &a_end_of_expression)) {
      ast_node node;
      ast_node_init(&node, ast_lambda_function_application);
      node.lambda_function_application.lambda = lambda_h;
      mos_vector_move(&node.lambda_function_application.arguments, &arguments);
      return result_ast_node(p, &node);
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
  ast_node_h sym = p->result;

  if (a_try(p, a_equal_sign)) return 1;

  return result_ast_node_handle(p, sym);
}

static int let_in_form(parser *p) {
  // let a = 2 in expression
  if (a_try_s(p, &the_symbol, "let")) return 1;
  if (a_try(p, &simple_declaration)) return 1;
  ast_node_h sym = p->result;

  if (a_try(p, &expression)) return 1;
  ast_node_h defn = p->result;

  if (a_try_s(p, &the_symbol, "in")) return 1;
  if (a_try(p, &expression)) return 1;
  ast_node_h body = p->result;

  ast_node   node;
  ast_node_init(&node, ast_let_in);
  node.let_in.name  = sym;
  node.let_in.value = defn;
  node.let_in.body  = body;
  return result_ast_node(p, &node);
}

static int let_form(parser *p) {
  // let f a b c... = ...
  if (a_try_s(p, &the_symbol, "let")) return 1;
  if (a_try(p, &function_declaration)) return 1;
  ast_node_h decl_h = p->result;

  if (a_try(p, &function_definition)) return 1;
  ast_node_h defn_h = p->result;

  ast_node   node;
  ast_node_init(&node, ast_let);

  // get declaration out of pool to move into new node
  ast_node *decl = ast_pool_at(p->ast_pool, decl_h);
  node.let.name  = decl->function_declaration.name;
  node.let.body  = defn_h;

  // move the vector from the function_declaration node to the new ast node
  mos_vector_move(&node.let.parameters, &decl->function_declaration.parameters);
  return result_ast_node(p, &node);
}

static int tuple_expression(parser *p) {

  if (a_try(p, a_open_round)) return 1;

  mos_vector elements;
  ast_vector_init(&elements);

  // first, expect an expression, which must be followed by a comma
  // then, zero or more expressions before a close round. So (expr,)
  // is a valid tuple.
  if (a_try(p, &expression)) return 1;
  if (mos_vector_push_back(p->alloc, &elements, &p->result)) goto cleanup;
  if (a_try(p, &a_comma)) goto cleanup;

  int count = 0;

  while (true) {

    if (0 == a_try(p, &a_close_round)) {
      ast_node node;
      ast_node_init(&node, ast_tuple);
      mos_vector_move(&node.tuple.elements, &elements);
      return result_ast_node(p, &node);
    }

    // comma required if this is not the first time through the loop
    if (count++ > 0)
      if (a_try(p, &a_comma)) goto cleanup;

    // expression
    if (0 == a_try(p, &expression))
      if (mos_vector_push_back(p->alloc, &elements, &p->result)) goto cleanup;

    // loop to check for close round, or else
  }

cleanup:
  mos_vector_deinit(p->alloc, &elements);
  return 1;
}

static int grouped_expression(parser *parser) {
  if (a_try(parser, &a_open_round)) return 1;
  if (a_try(parser, &expression)) return 1;

  // save expression node result
  ast_node_h const out_h = parser->result;

  if (a_try(parser, &a_close_round)) return 1;

  // replace parser result with expression node
  parser->result = out_h;
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

void parser_result(parser *p, ast_node_h *handle) {
  if (handle) {
    *handle = p->result;
  }
}
