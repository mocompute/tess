#ifndef TESS_IMPORT_RESOLVER_H
#define TESS_IMPORT_RESOLVER_H

#include "alloc.h"
#include "hashmap.h"
#include "str.h"

// Import kind determines search behavior
typedef enum {
    IMPORT_QUOTED,        // "file.tl" - search relative first, then -I paths
    IMPORT_ANGLE_BRACKET, // <file.tl> - search only standard paths
} import_kind;

// Forward declaration
typedef struct import_resolver import_resolver;

// Result of resolving an import
typedef struct {
    str canonical_path; // Resolved path (empty if error)
    int is_duplicate;   // 1 if already imported, 0 otherwise
} import_result;

// Create a new import resolver
import_resolver *import_resolver_create(allocator *arena);

// Destroy the import resolver
void import_resolver_destroy(import_resolver **self);

// Add a user include path (-I flag)
void import_resolver_add_user_path(import_resolver *self, str path);

// Add a standard library path
void import_resolver_add_standard_path(import_resolver *self, str path);

// Resolve an import directive
// - import_path: The path as written in #import (with quotes/brackets)
// - importing_file: Path to file containing the #import (for relative resolution)
// Returns: Result with canonical path or error
// On error, prints message to stderr and returns empty path
import_result import_resolver_resolve(import_resolver *self,
                                      str              import_path,
                                      str              importing_file);

// Mark an import as being processed (for cycle detection)
// Returns 1 if cycle detected (already in progress), 0 otherwise
int import_resolver_begin_import(import_resolver *self, str canonical_path);

// Mark an import as finished processing
void import_resolver_end_import(import_resolver *self, str canonical_path);

// Check if a path has already been fully imported
int import_resolver_is_imported(import_resolver *self, str canonical_path);

// Mark a path as fully imported
void import_resolver_mark_imported(import_resolver *self, str canonical_path);

// Get import kind from a quoted path string
import_kind import_resolver_get_kind(str quoted_path);

// Strip quotes or angle brackets from import path
str import_resolver_strip_quotes(allocator *alloc, str quoted_path);

// Print search paths for error messages
void import_resolver_print_paths(import_resolver *self);

#endif // TESS_IMPORT_RESOLVER_H
