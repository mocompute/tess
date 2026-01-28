#ifndef TESS_FORMAT_H
#define TESS_FORMAT_H

#include "alloc.h"
#include "str.h"
#include "types.h"

// Format a Tess source file. Returns the formatted source as a str.
// Currently a stub that returns the input unchanged.
str tl_format(allocator *alloc, char const *data, u32 size, char const *filename);

#endif
