#ifndef MOS_HASHMAP_H
#define MOS_HASHMAP_H

#include "alloc.h"
#include "nodiscard.h"
#include "str.h"
#include "types.h"

#include <assert.h>
#include <stdalign.h>

// -- hash map --

typedef struct hashmap_key {
    u8   size;
    byte data[];
} hashmap_key;

typedef struct {
    struct hashmap_key *key;
    u8                  status;
    alignas(sizeof(void *)) byte data[]; // size: hashmap.value_size
} hashmap_entry;

#define HASHMAP_MAX_KEY_LEN      255
#define HASHMAP_MAX_ELEMENT_SIZE (64 - sizeof(hashmap_entry))

typedef struct hashmap hashmap;

typedef struct {
    void const *key_ptr;
    void       *data;
    u32         index;
    u8          key_size;
} hashmap_iterator;

// -- allocation and deallocation --

nodiscard hashmap *map_create(allocator *alloc, u16 value_size, u32 n_buckets) mallocfun;
nodiscard hashmap *map_create_ptr(allocator *alloc, u32 n_buckets) mallocfun;
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
void   map_set_ptr(hashmap **, void const *key, u8 key_len, void const *data);
void   str_map_set(hashmap **, str key, void const *data);
void   str_map_set_ptr(hashmap **, str key, void const *data);
void   map_set_v(hashmap **, void const *key, u8 key_len,
                 void const *data);                          // value fits in void* TODO use set_ptr?
void   str_map_set_v(hashmap **, str key, void const *data); // value fits in void* TODO use set_ptr?
int    map_contains(hashmap const *, void const *key, u8 key_len);
int    str_map_contains(hashmap const *, str key);
void  *map_get(hashmap *, void const *key, u8 key_len);
void  *map_get_ptr(hashmap *, void const *key, u8 key_len);
void  *str_map_get(hashmap *, str key);
void  *str_map_get_ptr(hashmap *, str key);
void   map_erase(hashmap *, void const *key, u8 key_len);
void   str_map_erase(hashmap *, str key);
void   map_reset(hashmap *);

void   hset_insert(hashmap **, void const *, u8);
void   str_hset_insert(hashmap **, str);
int    hset_contains(hashmap const *, void const *, u8);
int    str_hset_contains(hashmap const *, str);
int    hset_is_subset(hashmap const *super, hashmap const *sub);
void   hset_remove(hashmap *, void const *, u8);
void   str_hset_remove(hashmap *, str);
void   hset_reset(hashmap *);
size_t hset_size(hashmap const *);

// -- utilities --
int       map_iter(hashmap const *, hashmap_iterator *);
int       hset_iter(hashmap const *, hashmap_iterator *);
str_array str_map_sorted_keys(allocator *, hashmap *);

// To document key and value types at create callsite
#define map_new(A, K, V, N) map_create((A), sizeof(V), N)

#endif
