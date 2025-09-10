#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "alloc.h"
#include "types.h"

void        file_read(allocator *, char const *, char **, u32 *);
char const *file_basename(char const *);

#endif
