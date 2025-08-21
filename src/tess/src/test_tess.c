#include "alloc.h"
#include "ast.h"
#include "parser.h"
#include "tokenizer.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int test_tess_token_string(void) {
  int            error = 0;

  mos_allocator *alloc = mos_alloc_default_allocator();

  error += strcmp("comma", token_tag_to_string(tok_comma)) == 0 ? 0 : 1;

  token tok;

  {
    token_init(&tok, tok_equal_sign);
    char *s = token_to_string(alloc, &tok);
    error += 0 == strcmp("(equal_sign)", s) ? 0 : 1;
    alloc->free(alloc, s);
  }

  {
    token_init_v(&tok, tok_newline_indent, 4);
    char *s = token_to_string(alloc, &tok);
    error += 0 == strcmp("(newline_indent 4)", s) ? 0 : 1;
    alloc->free(alloc, s);
  }

  {
    error += 0 == token_init_s(alloc, &tok, tok_number, "123") ? 0 : 1;
    if (error) return error;

    char *s = token_to_string(alloc, &tok);

    error += 0 == strcmp("(number \"123\")", s) ? 0 : 1;
    alloc->free(alloc, s);

    token_deinit(alloc, &tok);
  }

  return error;
}

int test_tokenizer_basic(void) {
  int            error = 0;

  mos_allocator *alloc = mos_alloc_default_allocator();
  tokenizer     *t     = tokenizer_alloc(alloc);

  {
    char const *input = "  (  )  ";
    if (tokenizer_init(alloc, t, input, strlen(input))) return error + 1;

    token           tok;
    tokenizer_error err;

    // expect open_round
    error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += tok_open_round == tok.tag ? 0 : 1;
    token_deinit(alloc, &tok);

    // expect close round
    error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += tok_close_round == tok.tag ? 0 : 1;
    token_deinit(alloc, &tok);

    // expect eof
    error += 1 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += tess_err_eof == err.tag ? 0 : 1;
    token_deinit(alloc, &tok);

    // still eof
    error += 1 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += tess_err_eof == err.tag ? 0 : 1;
    token_deinit(alloc, &tok);

    tokenizer_deinit(alloc, t);
  }

  tokenizer_dealloc(alloc, &t);

  return error;
}

int test_tokenizer_string(void) {
  int            error = 0;

  mos_allocator *alloc = mos_alloc_default_allocator();
  tokenizer     *t     = tokenizer_alloc(alloc);

  {
    char const *input = " \"abcdef\"  ";
    if (tokenizer_init(alloc, t, input, strlen(input))) return error + 1;

    token           tok;
    tokenizer_error err;

    // expect string
    error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += tok_string == tok.tag ? 0 : 1;
    error += 0 == strcmp("abcdef", tok.s) ? 0 : 1;
    token_deinit(alloc, &tok);

    tokenizer_deinit(alloc, t);
  }

  tokenizer_dealloc(alloc, &t);

  return error;
}

int test_tokenizer_terminal_static_string(void) {
  // regression test for ASAN
  int            error = 0;

  mos_allocator *alloc = mos_alloc_default_allocator();
  tokenizer     *t     = tokenizer_alloc(alloc);

  {
    char const *input = "-";
    if (tokenizer_init(alloc, t, input, strlen(input))) return error + 1;

    token           tok;
    tokenizer_error err;

    // expect string
    error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += tok_symbol == tok.tag ? 0 : 1;
    error += 0 == strcmp("-", tok.s) ? 0 : 1;
    token_deinit(alloc, &tok);

    tokenizer_deinit(alloc, t);
  }

  tokenizer_dealloc(alloc, &t);

  return error;
}

int test_parser_init(void) {
  int            error = 0;

  char const    *input = "()";

  mos_allocator *alloc = mos_alloc_arena_alloci(mos_alloc_default_allocator(), 5096);
  if (!alloc) return error + 1;

  ast_pool *pool = ast_pool_alloci(alloc);
  if (NULL == pool) return error + 1;

  parser *p = parser_alloc(alloc);
  if (parser_init(alloc, p, pool, input, strlen(input))) return error + 1;

  // can skip deinit/dealloc due to arena
  // parser_deinit(p);
  // parser_dealloc(alloc, &p);
  // ast_pool_dealloci(alloc, &pool);

  mos_alloc_arena_deinit(alloc);
  mos_alloc_arena_dealloc(mos_alloc_default_allocator(), &alloc);

  return error;
}

int test_parser_basic(void) {
  int            error = 0;

  char const    *input = "a";

  mos_allocator *alloc = mos_alloc_arena_alloci(mos_alloc_default_allocator(), 5096);
  if (!alloc) return error + 1;

  ast_pool *pool = ast_pool_alloci(alloc);
  if (NULL == pool) return error + 1;

  parser *p = parser_alloc(alloc);
  if (parser_init(alloc, p, pool, input, strlen(input))) return error + 1;

  if (parser_next(p)) return error + 1;

  ast_node_h node_h;
  parser_result(p, &node_h);
  ast_node *node = ast_pool_at(pool, node_h);

  error += ast_symbol == node->tag ? 0 : 1;
  error += 0 == strcmp(node->symbol.name, "a") ? 0 : 1;

  parser_deinit(p);
  parser_dealloc(alloc, &p);
  ast_pool_dealloci(alloc, &pool);

  mos_alloc_arena_deinit(alloc);
  mos_alloc_arena_dealloc(mos_alloc_default_allocator(), &alloc);

  return error;
}

int test_parser_expression(void) {
  int            error = 0;

  char const    *input = "let x = 5 in x + 2";

  mos_allocator *alloc = mos_alloc_arena_alloci(mos_alloc_default_allocator(), 5096);
  if (!alloc) return error + 1;

  ast_pool *pool = ast_pool_alloci(alloc);
  if (NULL == pool) return error + 1;

  parser *p = parser_alloci(alloc, pool, input, strlen(input));
  if (NULL == p) return error + 1;

  if (parser_next(p)) return error + 1;
  ast_node_h node_h;
  parser_result(p, &node_h);
  ast_node *node = ast_pool_at(pool, node_h);

  error += ast_let_in == node->tag ? 0 : 1;

  mos_alloc_arena_dealloci(mos_alloc_default_allocator(), &alloc);

  return error;
}

#define T(name)                                                                                            \
  this_error = name();                                                                                     \
  if (this_error) {                                                                                        \
    fprintf(stderr, "FAILED: %s\n", #name);                                                                \
    error += this_error;                                                                                   \
  }

int main(void) {
  int          error      = 0;
  int          this_error = 0;

  unsigned int seed       = (unsigned int)time(0);

  fprintf(stderr, "Seed = %u\n\n", seed);
  srand(seed);

  T(test_tess_token_string);
  T(test_tokenizer_basic);
  T(test_tokenizer_string);
  T(test_tokenizer_terminal_static_string);
  T(test_parser_init);
  T(test_parser_basic);
  T(test_parser_expression);

  return error;
}
