#include "hash.h"

#include "assert.h"

size_t mos_hash64(char const *data, size_t len) {

  // https://datatracker.ietf.org/doc/draft-eastlake-fnv/35/

  size_t hash = 0xCBF29CE484222325;

  for (size_t i = 0; i < len; ++i) {
    hash ^= (size_t)data[i];
    hash *= 0x00000100000001B3;
  }

  return hash;
}

size_t mos_hash32(char const *data, size_t len) {

  // https://datatracker.ietf.org/doc/draft-eastlake-fnv/35/

  size_t hash = 0x811C9DC5;

  for (size_t i = 0; i < len; ++i) {
    hash ^= (size_t)data[i];
    hash *= 0x01000193;
  }

  return hash;
}
