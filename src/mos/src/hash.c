#include "hash.h"

#include "assert.h"

u64 hash64(byte const *data, size_t len) {

    // https://datatracker.ietf.org/doc/draft-eastlake-fnv/35/

    size_t hash = 0xCBF29CE484222325;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)data[i];
        hash *= 0x00000100000001B3;
    }

    return hash;
}

u64 hash64_combine(u64 seed, byte const *data, size_t len) {

    size_t hash = seed;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)data[i];
        hash *= 0x00000100000001B3;
    }

    return hash;
}

u32 hash32(byte const *data, size_t len) {

    // https://datatracker.ietf.org/doc/draft-eastlake-fnv/35/

    u32 hash = 0x811C9DC5;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)data[i];
        hash *= 0x01000193;
    }

    return hash;
}

u32 hash32_combine(u32 seed, byte const *data, size_t len) {

    u32 hash = seed;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (size_t)data[i];
        hash *= 0x01000193;
    }

    return hash;
}
