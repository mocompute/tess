#include "error.h"

char const *tess_error_tag_to_string(tess_error_tag tag) {

#define STRING_ITEM(name, str) [name] = str,

  static char const *const strings[]  = {TESS_ERROR_TAG_LIST(STRING_ITEM)};

#undef STRING_ITEM

  return strings[tag];
}
