#ifndef TESS_FETCH_H
#define TESS_FETCH_H

#include "alloc.h"
#include "file.h"

// Options for tl_fetch.
typedef struct {
    char const        *package_tl_path; // path to package.tl (required)
    char const        *lock_path;       // path to package.tl.lock (required)
    char const        *work_dir;        // base for relative depend_paths (required)
    file_url_get_opts *url_opts;        // NULL = real curl; non-NULL for mocking
    int                verbose;
} tl_fetch_opts;

// Fetch dependencies declared in package.tl.
// Reads package_tl_path, reads/writes lock_path, saves .tpkg files
// to the depend_path directories (resolved relative to work_dir).
// Returns 0 on success.
int tl_fetch(allocator *alloc, tl_fetch_opts const *opts);

#endif
