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

    hashmap                *modules_seen;           // str module_name -> str file_path
    hashmap                *import_defines;         // hset of defined symbols for conditional compilation
    int                     conditional_skip_depth; // tracks #ifdef nesting depth during scanning
    str                     current_file_module; // transient: module name of file currently being scanned
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
// - conditional_skip_depth and current_file_module are reset at the start of each call
//
// Returns 0 on success.
int tl_source_scanner_scan(tl_source_scanner *self, str file_path, char_csized input, str_array *imports);

// Result of cross-checking scanner state against export module list.
typedef struct {
    int error_count;
    int warning_count;
} tl_source_scanner_validate_result;

// Cross-check discovered modules against the export() module list from package.tl.
// - export_modules: array of public module names from export() calls
// - export_module_count: number of entries in export_modules
// - verbose: if nonzero, print internal module list to stderr
//
// Checks:
// - ERROR if an export module is not found in modules_seen
// - VERBOSE: list internal modules (in modules_seen but not in export list)
//
// Returns result with error_count and warning_count.
tl_source_scanner_validate_result tl_source_scanner_validate(tl_source_scanner *self,
                                                             str const         *export_modules,
                                                             u32 export_module_count, int verbose);

// Extract all #import directives from source text.
// Uses the scanner state machine for correct string/comment handling.
// Does NOT apply conditional compilation — all imports are collected unconditionally.
// Appends to *imports (caller initializes the array).
void tl_source_scanner_collect_imports(allocator *alloc, char_csized input, str_array *imports);

// Extract all #define symbols from text (intended for parsing `cc -dM -E` or `cl /PD /E` output).
// For each `#define NAME ...` line, appends NAME to *defines (value is discarded).
// Does NOT apply conditional compilation — all defines are collected unconditionally.
// Appends to *defines (caller initializes the array).
void tl_source_scanner_collect_defines(allocator *alloc, char_csized input, str_array *defines);

// Extract all #module directives from source text.
// Uses the scanner state machine for correct string/comment handling.
// Does NOT apply conditional compilation — all modules are collected unconditionally.
// Appends module names to *modules (caller initializes the array).
void tl_source_scanner_collect_modules(allocator *alloc, char_csized input, str_array *modules);

#endif // TESS_SOURCE_SCANNER_H
