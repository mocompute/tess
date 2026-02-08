#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "alloc.h"
#include "str.h"
#include "types.h"

int         file_exists(str);
void        file_read(allocator *, char const *, char **, u32 *);
char const *file_basename(char const *);

char       *file_current_working_directory(span);
char       *file_exe_directory(span buf);

// -- path utilities --

// Returns directory portion of path (e.g., "/foo/bar/baz.tl" -> "/foo/bar")
// Returns "." if path has no directory component
str         file_dirname(allocator *, str path);

// Returns 1 if path is absolute, 0 if relative
int         file_is_absolute(str path);

// Join two path components with separator
str         file_path_join(allocator *, str dir, str file);

// Normalize path: resolve "..", ".", remove redundant separators
// Returns empty string if path would escape root (too many "..")
str         file_path_normalize(allocator *, str path);

// Compute relative path from a directory to a target path.
// Returns empty string on error (empty inputs, different Windows drives).
str         file_path_relative(allocator *, str from_dir, str to_path);

#endif
