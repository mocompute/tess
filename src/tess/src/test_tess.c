#include "alloc.h"
#include "tokenizer.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int test_tess_token_string(void) {
  int              error = 0;

  mos_allocator_t *alloc = mos_alloc_default_allocator();

  error += strcmp("comma", tess_token_tag_to_string(comma)) == 0 ? 0 : 1;

  tess_token_t tok;

  {
    tess_token_init(&tok, equal_sign);
    char *s = tess_token_to_string(alloc, &tok);
    error += 0 == strcmp("(equal_sign)", s) ? 0 : 1;
    alloc->free(s);
  }

  {
    tess_token_init_v(&tok, newline_indent, 4);
    char *s = tess_token_to_string(alloc, &tok);
    error += 0 == strcmp("(newline_indent 4)", s) ? 0 : 1;
    alloc->free(s);
  }

  {
    error += 0 == tess_token_init_s(alloc, &tok, number, "123") ? 0 : 1;
    if (error) return error;

    char *s = tess_token_to_string(alloc, &tok);

    error += 0 == strcmp("(number \"123\")", s) ? 0 : 1;
    alloc->free(s);

    tess_token_deinit(alloc, &tok);
  }

  return error;
}

int test_tokenizer(void) {
  int               error = 0;

  mos_allocator_t  *alloc = mos_alloc_default_allocator();
  tess_tokenizer_t *t     = tess_tokenizer_alloc(alloc);

  {
    char const *input = "()";
    tess_tokenizer_init(alloc, t, input, strlen(input));

    tess_token_t           tok;
    tess_tokenizer_error_t err;

    // expect open_round
    error += 0 == tess_tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += open_round == tok.tag ? 0 : 1;

    // expect close round
    error += 0 == tess_tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += close_round == tok.tag ? 0 : 1;

    // expect eof
    error += 1 == tess_tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += eof == err.tag ? 0 : 1;

    // still eof
    error += 1 == tess_tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
    if (error) return error;
    error += eof == err.tag ? 0 : 1;

    tess_tokenizer_deinit(alloc, t);
  }

  tess_tokenizer_dealloc(alloc, t);

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
  T(test_tokenizer);

  return error;
}
