#ifndef MOS_HASH_H
#define MOS_HASH_H

#include "nodiscard.h"
#include "types.h"

#include <stddef.h>

// Returns: FNV-1a hash of data

u64           hash64(byte const *, size_t) purefun;
nodiscard u64 hash64_combine(u64, byte const *, size_t) purefun;
u64           hash64_strings(char const **, size_t) purefun;
u32           hash32(byte const *, size_t) purefun;
nodiscard u32 hash32_combine(u32, byte const *, size_t) purefun;

#endif
