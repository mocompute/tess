#ifndef TESS_ERROR_H
#define TESS_ERROR_H

#define TESS_ERROR_TAG_LIST(X)                                                                             \
  /* tokenizer */                                                                                          \
  X(tess_err_eof, "eof")                                                                                   \
  X(tess_err_out_of_memory, "out_of_memory")                                                               \
  X(tess_err_indent_too_long, "indent_too_long")                                                           \
  X(tess_err_invalid_token, "invalid_token")

#endif

#define TESS_ENUM(name, str) name,
typedef enum tess_error_tag { TESS_ERROR_TAG_LIST(TESS_ENUM) } tess_error_tag_t;
#undef TESS_ENUM

// -- utilities --

char const *tess_error_tag_to_string(tess_error_tag_t);
