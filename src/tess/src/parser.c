#include "parser.h"

#include "alloc.h"
#include "token.h"
#include "tokenizer.h"
#include "vector.h"
#include <string.h>

struct tess_parser {
  tess_tokenizer_t        *tokenizer;
  struct mos_vector        good_tokens;
  struct tess_parser_error error;
};

// -- statics --

// typedef struct tess_parser_error {
//   tess_tokenizer_error_t *tokenizer;
//   tess_token_t           *token;
//   tess_error_tag_t        tag;
// } tess_parser_error_t;

static void parser_error_init(tess_parser_error_t *err) {
  memset(err, 0, sizeof *err);
}

static void parser_error_deinit(mos_allocator_t *alloc, tess_parser_error_t *err) {

  if (err->tokenizer) {
    tess_tokenizer_error_deinit(err->tokenizer);
    alloc->free(err->tokenizer);
  }

  if (err->token) {
    tess_token_deinit(alloc, err->token);
    alloc->free(err->token);
  }

  mos_alloc_invalidate(err, sizeof *err);
}

// -- allocation and deallocation --

tess_parser_t *tess_parser_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(struct tess_parser));
}

void tess_parser_dealloc(mos_allocator_t *alloc, tess_parser_t *parser) {
  alloc->free(parser);
}

int tess_parser_init(mos_allocator_t *alloc, tess_parser_t *parser, char const *input, size_t input_len) {
  // alloc and init tokenizer
  parser->tokenizer = tess_tokenizer_alloc(alloc);
  if (!parser->tokenizer) return 1;
  tess_tokenizer_init(alloc, parser->tokenizer, input, input_len);

  mos_vector_init(&parser->good_tokens, sizeof(struct tess_token));
  parser_error_init(&parser->error);

  return 0;
}

void tess_parser_deinit(mos_allocator_t *alloc, tess_parser_t *parser) {
  parser_error_deinit(alloc, &parser->error);
  mos_vector_deinit(alloc, &parser->good_tokens);

  // deinit and dealloc tokenizer
  tess_tokenizer_deinit(alloc, parser->tokenizer);
  tess_tokenizer_dealloc(alloc, parser->tokenizer);

  mos_alloc_invalidate(parser, sizeof *parser);
}

// -- parser --
