#include "tokenizer.h"

#include "alloc.h"
#include "vector.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct tess_tokenizer_t {
  char const  *input;
  size_t       input_len;
  size_t       pos;

  mos_vector_t buf;
  mos_vector_t backtrack;
};

//

void *tess_tokenizer_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(tess_tokenizer_t));
}

void tess_tokenizer_dealloc(mos_allocator_t *alloc, tess_tokenizer_t *tok) {
  alloc->free(tok);
}

void tess_tokenizer_init(mos_allocator_t *alloc, tess_tokenizer_t *tok, char const *input, size_t len) {
  tok->input     = input;
  tok->input_len = len;
  tok->pos       = 0;

  mos_vector_init(&tok->buf, sizeof(char));
  mos_vector_reserve(alloc, &tok->buf, 32);
  mos_vector_init(&tok->backtrack, sizeof(tess_token_t));
  mos_vector_reserve(alloc, &tok->backtrack, 8);
}

void tess_tokenizer_deinit(mos_allocator_t *alloc, tess_tokenizer_t *tok) {
  mos_vector_deinit(alloc, &tok->backtrack);
  mos_vector_deinit(alloc, &tok->buf);
}

void tess_tokenizer_put_back(mos_allocator_t *alloc, tess_tokenizer_t *tokenizer, tess_token_t const *toks,
                             size_t n_toks) {
  for (size_t i = n_toks; i != 0; --i) {
    mos_vector_push_back(alloc, &tokenizer->backtrack, &toks[i - 1]);
  }
}

//

char const *tess_tokenizer_error_tag_to_string(tess_tokenizer_error_tag_t tag) {
#define STRING_ITEM(name, str) [name]                    = str,
  static char const *const tokenizer_error_tag_strings[] = {TOKENIZER_ERROR_TAG_LIST(STRING_ITEM)};
#undef STRING_ITEM
  return tokenizer_error_tag_strings[tag];
}
