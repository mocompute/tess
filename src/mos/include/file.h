#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "alloc.h"

void        file_read(allocator *, char const *, char **, size_t *);
char const *file_basename(char const *);

#endif
