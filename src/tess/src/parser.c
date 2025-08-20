#include "parser.h"
#include "ast.h"
#include "token.h"
#include "tokenizer.h"

#include "alloc.h"
#include "vector.h"

#include <assert.h>
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
  token_init(&parser->error_token, tess_tok_invalid);
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

void result_ast(parser_t *parser, ast_tag_t tag) {
  ast_node_deinit(parser->alloc, &parser->result_ast_node);
  ast_node_init(&parser->result_ast_node, tag);
}

int result_ast_str(parser_t *parser, ast_tag_t tag, char const *s) {

  ast_node_deinit(parser->alloc, &parser->result_ast_node);
  ast_node_init(&parser->result_ast_node, tag);

  // TODO strings
  parser->result_ast_node.name = parser->alloc->malloc(strlen(s) + 1);
  if (!parser->result_ast_node.name) return 1;
  strcpy(parser->result_ast_node.name, s);
  return 0;
}

static int eat_newlines(parser_t *parser) {

  while (true) {
    if (tokenizer_next(parser->alloc, parser->tokenizer, &parser->error_token, &parser->tokenizer_error)) {
      parser->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    token_tag_t const tag = parser->error_token.tag;
    if (tess_tok_comment == tag || tess_tok_one_newline == tag || tess_tok_two_newline == tag ||
        tess_tok_newline_indent == tag) {
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

    if (tess_tok_comment == out_tok->tag) continue;

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

static int a_comma(parser_t *parser) {
  if (next_token(parser, &parser->error_token)) {
    return 1;
  }

  if (tess_tok_comma == parser->error_token.tag) {
    result_ast_str(parser, tess_ast_symbol, ",");
    return 0;
  }

  parser->error.tag = tess_err_expected_comma;
  return 1;
}

static int expression(parser_t *parser) {
  if (eat_newlines(parser)) return 1;

  if (a_try(parser, &a_comma)) return 1;

  return 0;
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
