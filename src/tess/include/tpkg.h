#ifndef TESS_TPKG_H
#define TESS_TPKG_H

#include "alloc.h"
#include "str.h"
#include "types.h"

// Forward declaration
struct import_resolver;

// Package metadata (stored uncompressed in archive header)
typedef struct {
    str  name;    // package name (required)
    str  author;  // author name/email (may be empty)
    str  version; // version string (required)
    str *modules; // array of public module names
    u16  module_count;
    str *depends; // array of required dependencies ("Name=Version")
    u16  depends_count;
} tl_tpkg_metadata;

// Single file entry in the archive
typedef struct {
    char const *name;
    u32         name_len;
    byte const *data;
    u32         data_len;
} tl_tpkg_entry;

// Complete archive structure
typedef struct {
    tl_tpkg_metadata metadata;
    tl_tpkg_entry   *entries;
    u32              entries_count;
} tl_tpkg_archive;

// Write archive to a .tpkg file. Returns 0 on success.
int tl_tpkg_write(allocator *alloc, char const *output_path, tl_tpkg_metadata const *metadata,
                  tl_tpkg_entry const *entries, u32 count);

// Read a .tpkg file. Allocates entries and their data from alloc. Returns 0 on success.
int tl_tpkg_read(allocator *alloc, char const *input_path, tl_tpkg_archive *out);

// Read a .tpkg archive from an in-memory buffer. Same as tl_tpkg_read but without file I/O.
// The buffer is read directly (not copied); it must remain valid until parsing completes.
// Returns 0 on success.
int tl_tpkg_read_from_memory(allocator *alloc, void const *data, u32 size, tl_tpkg_archive *out);

// Validate a filename (no absolute paths, no ".." components). Returns 1 if valid.
int tl_tpkg_valid_filename(char const *name, u32 len);

// -- High-level operations --

// Pack options
typedef struct {
    int verbose;
    // Metadata fields
    char const *name;            // package name (required)
    char const *author;          // package author (optional)
    char const *version;         // package version (required)
    char const *package_tl_path; // path to package.tl (included as entry if non-null)
    str        *modules;         // array of public module names (optional)
    u16         module_count;
    // Dependencies (from package.tl)
    str *depends;
    u16  depends_count;
} tl_tpkg_pack_opts;

// Pack resolved files into a .tpkg archive.
// - files: array of canonical file paths (already resolved via import system)
// - base_dir: directory for computing relative paths (empty = auto from first file)
// - resolver: used for stdlib filtering (files under stdlib paths are excluded)
// Returns 0 on success.
int tl_tpkg_pack(allocator *alloc, char const *output_path, str_sized files, str base_dir,
                 struct import_resolver *resolver, tl_tpkg_pack_opts opts);

// Extract all entries from an already-loaded archive to output_dir.
// Creates subdirectories as needed. Appends each written path to out_files.
// Returns 0 on success, 1 on error.
int tl_tpkg_extract(allocator *alloc, tl_tpkg_archive const *archive, char const *output_dir,
                    str_array *out_files);

// Unpack options
typedef struct {
    int list_only; // print filenames only, don't extract
    int verbose;
} tl_tpkg_unpack_opts;

// Unpack or list a .tpkg archive.
// - output_dir: extraction directory (ignored if list_only)
// Returns 0 on success.
int tl_tpkg_unpack(allocator *alloc, char const *archive_path, char const *output_dir,
                   tl_tpkg_unpack_opts opts);

// Parse "Name=Version" dependency string into components.
// Returns 0 on success, 1 on malformed input.
int tl_tpkg_parse_dep_string(allocator *alloc, str dep_str, str *out_name, str *out_version);

// Build filename for a package archive: "Name-Version.tpkg"
str tl_tpkg_filename(allocator *alloc, str name, str version);

#endif
