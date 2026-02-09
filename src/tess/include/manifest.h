#ifndef TESS_MANIFEST_H
#define TESS_MANIFEST_H

#include "alloc.h"
#include "str.h"
#include "types.h"

typedef struct {
    u32  format;  // from format(), currently 1
    str  name;    // from package()
    str  version; // from version()
    str  author;  // from author(), may be empty
    str *exports; // from export() calls
    u32  export_count;
    str *depend_paths; // from depend_path() calls
    u32  depend_path_count;
} tl_package_info;

typedef struct {
    str name;    // dependency package name
    str version; // required version
    str path;    // optional explicit path override (may be empty)
} tl_package_dep;

typedef struct {
    tl_package_info info;
    tl_package_dep *deps;
    u32             dep_count;
    tl_package_dep *optional_deps;
    u32             optional_dep_count;
} tl_package;

// Parse a package.tl file. Returns 0 on success, 1 on error.
// Errors are printed to stderr with "package.tl:" prefix.
// All strings are allocated from alloc.
int  tl_package_parse_file(allocator *alloc, char const *path, tl_package *out);

void tl_package_deinit(allocator *, tl_package *);

#endif
