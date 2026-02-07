#ifndef TESS_SOURCE_SCANNER_H
#define TESS_SOURCE_SCANNER_H

#include "alloc.h"
#include "hashmap.h"
#include "str.h"

// Forward declaration
struct import_resolver;

// Scanner state for extracting #import and #module directives from source files.
// Tracks conditional compilation (#ifdef/#ifndef/#endif), discovered modules,
// and the defines that control conditional compilation.
typedef struct {
    allocator              *arena;
    struct import_resolver *resolver;

    hashmap *modules_seen;         // str module_name -> str file_path
    hashmap *import_defines;       // hset of defined symbols for conditional compilation
    int      conditional_skip_depth; // tracks #ifdef nesting depth during scanning
} tl_source_scanner;

// Create and initialize a source scanner.
tl_source_scanner tl_source_scanner_create(allocator *arena, struct import_resolver *resolver);

// Add a define symbol (equivalent to -D flag).
void tl_source_scanner_define(tl_source_scanner *self, str symbol);

// Scan a source file for #import and #module directives.
// - file_path: canonical path of the file being scanned
// - input: raw file content
// - imports: output array; discovered #import paths are appended here
//
// Side effects:
// - Discovered #module directives are recorded in modules_seen (module_name -> file_path)
// - Conditional compilation state (import_defines, conditional_skip_depth) is updated
// - conditional_skip_depth is reset to 0 at the start of each call
//
// Errors:
// - Duplicate module name (same module defined in two files): prints error and returns 1
//
// Returns 0 on success, 1 on error.
int tl_source_scanner_scan(tl_source_scanner *self, str file_path, char_csized input, str_array *imports);

#endif // TESS_SOURCE_SCANNER_H
