#ifndef TESS_MANIFEST_H
#define TESS_MANIFEST_H

#include "alloc.h"
#include "str.h"
#include "types.h"

typedef struct {
    str  name;    // package name (required)
    str  version; // version string (required)
    str  author;  // author name/email (may be empty)
    str *modules; // array of public module names
    u32  module_count;
    str *lib_path; // array of library search paths
    u32  lib_path_count;
} tl_manifest_package;

typedef struct {
    str name;    // dependency package name (from section header)
    str version; // required version
    str path;    // optional explicit path override
} tl_manifest_dep;

typedef struct {
    tl_manifest_package package;
    tl_manifest_dep    *deps;
    u32                 dep_count;
    tl_manifest_dep    *optional_deps;
    u32                 optional_dep_count;
} tl_manifest;

// Parse a manifest from a buffer. Returns 0 on success, 1 on error.
// Errors are printed to stderr with "manifest:" prefix.
// All strings are allocated from alloc.
int tl_manifest_parse(allocator *alloc, str data, tl_manifest *out);

// Parse a manifest file. Reads file then calls tl_manifest_parse.
int tl_manifest_parse_file(allocator *alloc, char const *path, tl_manifest *out);

#endif
