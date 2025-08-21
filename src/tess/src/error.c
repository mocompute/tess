#include "error.h"

char const *tess_error_tag_to_string(tess_error_tag tag) {
  static char const *const strings[] = {TESS_ERROR_TAG_LIST(MOS_TAG_STRING)};
  return strings[tag];
}
