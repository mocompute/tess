#ifndef MOS_HASHMAP_H
#define MOS_HASHMAP_H

#include "alloc.h"
#include "nodiscard.h"
#include "string_t.h"
#include "types.h"

#include <assert.h>
#include <stdalign.h>

// -- hash map --

typedef struct hashmap hashmap;

typedef struct {
    struct hashmap_key *key;
    u8                  status;
    alignas(sizeof(void *)) byte data[]; // size: hashmap.value_size
} hashmap_entry;

typedef struct {
    void *key_ptr;
    void *data;
    u32   index;
    u16   key_size;
} hashmap_iterator;

// -- allocation and deallocation --

nodiscard hashmap *map_create(allocator *, u16 value_size) mallocfun;
nodiscard hashmap *map_create_n(allocator *alloc, u16 value_size, u32 n_buckets) mallocfun;
void               map_destroy(hashmap **);
nodiscard hashmap *map_copy(hashmap const *) mallocfun;

nodiscard hashmap *hset_create(allocator *) mallocfun;
void               hset_destroy(hashmap **);

nodiscard hashmap *hset_of_string(allocator *, string_sized) mallocfun;

// -- read-only access --

size_t map_size(hashmap const *);
int    map_empty(hashmap const *);
f32    map_load_factor(hashmap const *);

// -- data and iterator access --

// -- insertion and removal --

void   map_set(hashmap **, void const *key, u16 key_len, void const *data);
void   map_set_v(hashmap **, void const *key, u16 key_len, void const *data); // value fits in void*
int    map_contains(hashmap const *, void const *key, u16 key_len);
void  *map_get(hashmap *, void const *key, u16 key_len);
void   map_erase(hashmap *, void const *key, u16 key_len);
void   map_reset(hashmap *);

void   hset_insert(hashmap **, void const *, u16);
int    hset_contains(hashmap const *, void const *, u16);
int    hset_is_subset(hashmap const *super, hashmap const *sub);
void   hset_remove(hashmap *, void const *, u16);
void   hset_reset(hashmap *);
size_t hset_size(hashmap const *);

// -- utilities --
int map_iter(hashmap const *, hashmap_iterator *);
int hset_iter(hashmap const *, hashmap_iterator *);

#endif
