#ifndef MOS_HASH_H
#define MOS_HASH_H

#include "nodiscard.h"
#include "types.h"

#include <stddef.h>

// Returns: FNV-1a hash of data
size_t mos_hash64(byte const *, size_t) purefun;
size_t mos_hash32(byte const *, size_t) purefun;

#endif
