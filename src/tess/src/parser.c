#include "parser.h"
#include "ast.h"
#include "token.h"
#include "tokenizer.h"

#include "alloc.h"
#include "vector.h"

#include <assert.h>
#include <string.h>

struct tess_parser {
  mos_allocator_t            *alloc;
  tess_tokenizer_t           *tokenizer;
  tess_ast_pool_t            *ast_pool;

  struct tess_ast_node        result_ast_node;   // parse result
  size_t                      result_ast_node_h; // pool handle

  struct mos_vector           good_tokens; // for backtracking

  struct tess_parser_error    error;
  struct tess_tokenizer_error tokenizer_error; // does not require deinit
  struct tess_token           error_token;     // requires deinit
};

// -- statics --

// -- allocation and deallocation --

tess_parser_t *tess_parser_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(struct tess_parser));
}

void tess_parser_dealloc(mos_allocator_t *alloc, tess_parser_t **parser) {
  alloc->free(*parser);
  *parser = 0;
}

int tess_parser_init(mos_allocator_t *alloc, tess_parser_t *parser, char const *input, size_t input_len) {

  memset(parser, 0, sizeof *parser);
  parser->alloc = alloc;

  // tokenizer
  parser->tokenizer = tess_tokenizer_alloc(alloc);
  if (!parser->tokenizer) return 1;
  tess_tokenizer_init(alloc, parser->tokenizer, input, input_len);

  // ast pool
  parser->ast_pool = tess_ast_pool_alloc(alloc);
  if (!parser->ast_pool) return 1;
  tess_ast_pool_init(alloc, parser->ast_pool);

  // result_ast_node
  tess_ast_node_init(&parser->result_ast_node, tess_ast_eof);

  // good_tokens
  mos_vector_init(&parser->good_tokens, sizeof(struct tess_token));

  // error
  tess_token_init(&parser->error_token, tess_tok_invalid);
  parser->error.token     = &parser->error_token;
  parser->error.tokenizer = &parser->tokenizer_error;

  return 0;
}

void tess_parser_deinit(tess_parser_t *parser) {

  // error
  tess_token_deinit(parser->alloc, &parser->error_token);

  // good_tokens
  mos_vector_deinit(parser->alloc, &parser->good_tokens);

  // result_ast_node
  tess_ast_node_deinit(parser->alloc, &parser->result_ast_node);

  // ast_pool
  if (parser->ast_pool) {
    tess_ast_pool_deinit(parser->alloc, parser->ast_pool);
    tess_ast_pool_dealloc(parser->alloc, &parser->ast_pool);
  }

  // tokenizer
  if (parser->tokenizer) {
    tess_tokenizer_deinit(parser->alloc, parser->tokenizer);
    tess_tokenizer_dealloc(parser->alloc, &parser->tokenizer);
  }

  mos_alloc_invalidate(parser, sizeof *parser);
}

// -- parser --

typedef int (*parse_fun)(tess_parser_t *);

void result_ast(tess_parser_t *parser, tess_ast_tag_t tag) {
  tess_ast_node_deinit(parser->alloc, &parser->result_ast_node);
  tess_ast_node_init(&parser->result_ast_node, tag);
}

int result_ast_str(tess_parser_t *parser, tess_ast_tag_t tag, char const *s) {

  tess_ast_node_deinit(parser->alloc, &parser->result_ast_node);
  tess_ast_node_init(&parser->result_ast_node, tag);

  // TODO strings
  parser->result_ast_node.name = parser->alloc->malloc(strlen(s) + 1);
  if (!parser->result_ast_node.name) return 1;
  strcpy(parser->result_ast_node.name, s);
  return 0;
}

static int eat_newlines(tess_parser_t *parser) {

  while (true) {
    if (tess_tokenizer_next(parser->alloc, parser->tokenizer, &parser->error_token,
                            &parser->tokenizer_error)) {
      parser->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    tess_token_tag_t const tag = parser->error_token.tag;
    if (tess_tok_comment == tag || tess_tok_one_newline == tag || tess_tok_two_newline == tag ||
        tess_tok_newline_indent == tag) {
      continue;
    } else {
      tess_tokenizer_put_back(parser->alloc, parser->tokenizer, &parser->error_token, 1);
      result_ast(parser, tess_ast_eof);
      return 0;
    }
  }
}

static int next_token(tess_parser_t *parser, tess_token_t *out_tok) {
  while (true) {

    if (tess_tokenizer_next(parser->alloc, parser->tokenizer, out_tok, &parser->tokenizer_error)) {
      parser->error.tag = tess_err_tokenizer_error;
      return 1;
    }

    if (tess_tok_comment == out_tok->tag) continue;

    mos_vector_push_back(parser->alloc, &parser->good_tokens, out_tok);
    return 0;
  }
}

static int a_try(tess_parser_t *parser, parse_fun fun) {

  size_t const save_toks = mos_vector_size(&parser->good_tokens);

  if (fun(parser)) {
    assert(mos_vector_size(&parser->good_tokens) >= save_toks);
    tess_tokenizer_put_back(parser->alloc, parser->tokenizer,
                            (tess_token_t const *)mos_vector_data(&parser->good_tokens) + save_toks,
                            mos_vector_size(&parser->good_tokens) - save_toks);

    return 1;
  }

  return 0;
}

static int a_comma(tess_parser_t *parser) {
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

static int expression(tess_parser_t *parser) {
  if (eat_newlines(parser)) return 1;

  if (a_try(parser, &a_comma)) return 1;

  return 0;
}

int tess_parser_next(tess_parser_t *parser) {
  return expression(parser);
}

void tess_parser_result(tess_parser_t *parser, tess_ast_node_t **node, size_t *handle) {
  if (node) {
    *node = &parser->result_ast_node;
  }
  if (handle) {
    *handle = parser->result_ast_node_h;
  }
}
