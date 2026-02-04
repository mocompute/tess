#ifndef TESS_TLIB_H
#define TESS_TLIB_H

#include "alloc.h"
#include "str.h"
#include "types.h"

// Forward declaration
struct import_resolver;

typedef struct {
	char const *name;
	u32         name_len;
	byte const *data;
	u32         data_len;
} tl_tlib_entry;

typedef struct {
	tl_tlib_entry *entries;
	u32            count;
} tl_tlib_archive;

// Write entries to a .tlib file. Returns 0 on success.
int tl_tlib_write(allocator *alloc, char const *output_path,
                  tl_tlib_entry const *entries, u32 count);

// Read a .tlib file. Allocates entries and their data from alloc. Returns 0 on success.
int tl_tlib_read(allocator *alloc, char const *input_path,
                 tl_tlib_archive *out);

// Validate a filename (no absolute paths, no ".." components). Returns 1 if valid.
int tl_tlib_valid_filename(char const *name, u32 len);

// -- High-level operations --

// Pack options
typedef struct {
	int verbose;
} tl_tlib_pack_opts;

// Pack resolved files into a .tlib archive.
// - files: array of canonical file paths (already resolved via import system)
// - base_dir: directory for computing relative paths (empty = auto from first file)
// - resolver: used for stdlib filtering (files under stdlib paths are excluded)
// Returns 0 on success.
int tl_tlib_pack(allocator *alloc, char const *output_path,
                 str_sized files, str base_dir,
                 struct import_resolver *resolver,
                 tl_tlib_pack_opts opts);

// Unpack options
typedef struct {
	int list_only; // print filenames only, don't extract
	int verbose;
} tl_tlib_unpack_opts;

// Unpack or list a .tlib archive.
// - output_dir: extraction directory (ignored if list_only)
// Returns 0 on success.
int tl_tlib_unpack(allocator *alloc, char const *archive_path,
                   char const *output_dir, tl_tlib_unpack_opts opts);

#endif
