#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "alloc.h"
#include "str.h"
#include "types.h"

int         file_exists(char const *);
void        file_read(allocator *, char const *, char **, u32 *);
char const *file_basename(char const *);

char       *file_current_working_directory(span);
char       *file_exe_directory(span buf);

// -- path utilities --

// Returns directory portion of path (e.g., "/foo/bar/baz.tl" -> "/foo/bar")
// Returns empty string if path has no directory component
str         file_dirname(allocator *, char const *path);

// Returns 1 if path is absolute, 0 if relative
int         file_is_absolute(char const *path);

// Join two path components with separator
str         file_path_join(allocator *, str dir, str file);

// Normalize path: resolve "..", ".", remove redundant separators
// Returns empty string if path would escape root (too many "..")
str         file_path_normalize(allocator *, char const *path);

// Compute relative path from a directory to a file
// Both paths should be absolute. Returns path relative to from_dir that reaches to_path.
// Example: file_path_relative("/a/b", "/a/c/d.tl") -> "../c/d.tl"
// Returns empty string on error (different drives on Windows, etc.)
str         file_path_relative(allocator *, char const *from_dir, char const *to_path);

#endif
