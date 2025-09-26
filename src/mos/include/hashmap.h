#ifndef MOS_HASHMAP_H
#define MOS_HASHMAP_H

#include "alloc.h"
#include "nodiscard.h"
#include "str.h"
#include "types.h"

#include <assert.h>
#include <stdalign.h>

// -- hash map --

#define HASHMAP_MAX_KEY_LEN 255

typedef struct hashmap hashmap;

typedef struct {
    void const *key_ptr;
    void       *data;
    u32         index;
    u8          key_size;
} hashmap_iterator;

// -- allocation and deallocation --

nodiscard hashmap *map_create(allocator *alloc, u16 value_size, u32 n_buckets) mallocfun;
void               map_destroy(hashmap **);
nodiscard hashmap *map_copy(hashmap const *) mallocfun;

nodiscard hashmap *hset_create(allocator *, u32 n) mallocfun;
void               hset_destroy(hashmap **);

nodiscard hashmap *hset_of_str(allocator *, str_sized) mallocfun;

// -- read-only access --

size_t map_size(hashmap const *);
int    map_empty(hashmap const *);
f32    map_load_factor(hashmap const *);

// -- data and iterator access --

// -- insertion and removal --

void   map_set(hashmap **, void const *key, u8 key_len, void const *data);
void   map_set_v(hashmap **, void const *key, u8 key_len, void const *data); // value fits in void*
int    map_contains(hashmap const *, void const *key, u8 key_len);
void  *map_get(hashmap *, void const *key, u8 key_len);
void   map_erase(hashmap *, void const *key, u8 key_len);
void   map_reset(hashmap *);

void   hset_insert(hashmap **, void const *, u8);
int    hset_contains(hashmap const *, void const *, u8);
int    hset_is_subset(hashmap const *super, hashmap const *sub);
void   hset_remove(hashmap *, void const *, u8);
void   hset_reset(hashmap *);
size_t hset_size(hashmap const *);

// -- utilities --
int map_iter(hashmap const *, hashmap_iterator *);
int hset_iter(hashmap const *, hashmap_iterator *);

#endif
