#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "alloc.h"
#include "str.h"
#include "types.h"

void        file_read(allocator *, char const *, char **, u32 *);
char const *file_basename(char const *);

char       *file_current_working_directory(span);

#endif
