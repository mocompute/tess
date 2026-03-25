#ifndef TESS_LOCKFILE_H
#define TESS_LOCKFILE_H

#include "alloc.h"
#include "str.h"
#include "types.h"

typedef struct {
    str name;     // package name
    str version;  // exact version
    str base_url; // base URL (full URL = base_url + Name-Version.tpkg)
    str hash;     // "sha256:<64 hex chars>"
} tl_locked_dep;

typedef struct {
    str name;        // source package name
    str version;     // source package version
    str dep_name;    // dependency package name
    str dep_version; // dependency version
} tl_lock_edge;

typedef struct {
    u32            format; // lock_format version
    tl_locked_dep *deps;
    u32            dep_count;
    tl_lock_edge  *edges;
    u32            edge_count;
} tl_lockfile;

// Parse a package.tl.lock file. Returns 0 on success.
int tl_lockfile_parse_file(allocator *alloc, char const *path, tl_lockfile *out);

// Parse lock file content from a string (for testing). Returns 0 on success.
int tl_lockfile_parse(allocator *alloc, char const *content, u32 content_len, tl_lockfile *out);

// Write a package.tl.lock file. Returns 0 on success.
// Deps and edges should already be sorted.
int tl_lockfile_write(char const *path, tl_locked_dep const *deps, u32 dep_count, tl_lock_edge const *edges,
                      u32 edge_count);

#endif
