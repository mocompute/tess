#ifndef MOS_HASH_H
#define MOS_HASH_H

#include <stddef.h>

// Returns: FNV-1a hash of data
size_t mos_hash64(char const *, size_t);
size_t mos_hash32(char const *, size_t);

#endif
