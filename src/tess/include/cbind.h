#ifndef TESS_CBIND_H
#define TESS_CBIND_H

#include "alloc.h"
#include "str.h"

typedef struct {
    char const *header_path;
    char const *module_name; // NULL = derive from filename
    str         cc;          // C compiler path
    int         verbose;
} tl_cbind_opts;

// Generate .tl bindings from a C header file (runs preprocessor).
str tl_cbind(allocator *alloc, tl_cbind_opts const *opts);

// Generate .tl bindings from already-preprocessed text (for unit testing).
str tl_cbind_from_preprocessed(allocator *alloc, char const *pp_output, u32 pp_len, char const *target_file,
                               char const *module_name);

#endif
