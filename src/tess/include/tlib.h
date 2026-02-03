#ifndef TESS_TLIB_H
#define TESS_TLIB_H

#include "alloc.h"
#include "types.h"

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

#endif
