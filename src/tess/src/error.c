#include "error.h"

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *tl_error_tag_to_string(tl_error_tag tag) {
    static char const *const strings[] = {TESS_ERROR_TAG_LIST(MOS_TAG_STRING)};
    return strings[tag];
}
