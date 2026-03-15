#include "hash.h"

#include "assert.h"

#include <string.h>

u64 hash64(void const *data, size_t len) {

    // https://datatracker.ietf.org/doc/draft-eastlake-fnv/35/

    size_t hash = 0xCBF29CE484222325;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)((byte *)data)[i];
        hash *= 0x00000100000001B3;
    }

    return hash;
}

u64 hash64_combine(u64 seed, void const *data, size_t len) {

    size_t hash = seed;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)((byte *)data)[i];
        hash *= 0x00000100000001B3;
    }

    return hash;
}

u32 hash32(void const *data, size_t len) {

    // https://datatracker.ietf.org/doc/draft-eastlake-fnv/35/

    u32 hash = 0x811C9DC5;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)((byte *)data)[i];
        hash *= 0x01000193;
    }

    // Murmurhash3 finalizer. FNV-1a processes bytes sequentially, so the low
    // bits of the hash (used for hashmap bucket selection via hash & mask) are
    // dominated by the first few key bytes. For pointer-pair keys where most
    // bytes are constant (e.g. heap addresses sharing upper bits), this causes
    // severe clustering. The finalizer mixes all bits so every bit of the hash
    // reflects the full key.
    hash ^= hash >> 16;
    hash *= 0x85EBCA6B;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35;
    hash ^= hash >> 16;

    return hash;
}

u32 hash32_combine(u32 seed, void const *data, size_t len) {

    u32 hash = seed;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)((byte *)data)[i];
        hash *= 0x01000193;
    }

    // See hash32 for rationale
    hash ^= hash >> 16;
    hash *= 0x85EBCA6B;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35;
    hash ^= hash >> 16;

    return hash;
}

u64 hash64_strings(char const **strings, size_t len) {
    u64 hash = 0;
    for (size_t i = 0; i < len; ++i) {
        hash = hash64_combine(hash, (void *)strings[i], strlen(strings[i]));
    }
    return hash;
}
