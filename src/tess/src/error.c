#include "error.h"

char const *tess_error_tag_to_string(tess_error_tag_t tag) {

#define STRING_ITEM(name, str) [name]               = str,

  static char const *const tess_error_tag_strings[] = {TESS_ERROR_TAG_LIST(STRING_ITEM)};

#undef STRING_ITEM

  return tess_error_tag_strings[tag];
}
