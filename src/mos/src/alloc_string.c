#include "alloc_string.h"
#include "alloc_internal.h"

#include "types.h"

#include <assert.h>
#include <stdbool.h>

struct arena_header {
    struct arena_header *next;
    size_t               capacity;
    size_t               size;
    char                 data[];
};

struct string_arena {
    struct allocator     allocator;
    allocator           *parent;
    struct arena_header *head;
};

static char *bump_alloc_assume_capacity(struct arena_header *bucket, size_t sz) {
    char *out = (void *)(((byte *)bucket) + sizeof(struct arena_header) + bucket->size);

    bucket->size += sz;
    return out;
}

static size_t block_size(char const *p) {
    return strlen(p);
}

static bool is_last_block(struct arena_header const *bucket, void const *p) {
    size_t sz = block_size(p);
    if (((byte *)p) < ((byte *)bucket)) return false;
    return (size_t)(((byte *)p) - ((byte *)bucket)) + sz == bucket->size;
}

static void maybe_free_block(struct arena_header *bucket, void const *ptr) {
    if (is_last_block(bucket, ptr)) bucket->size -= block_size(ptr);
}

static bool bucket_has_capacity(struct arena_header const *bucket, size_t size) {
    return (bucket->capacity - bucket->size >= size);
}

static struct arena_header *find_bucket(struct string_arena const *arena, void const *ptr) {
    struct arena_header *bucket = arena->head;
    assert(bucket);
    while (bucket) {
        if (((byte *)ptr) > ((byte *)bucket) + sizeof(struct arena_header) &&
            ((byte *)ptr) < ((byte *)bucket + sizeof(struct arena_header) + bucket->size))
            return bucket;

        bucket = bucket->next;
    }

    return null;
}

static void *arena_malloc(allocator *alloc, size_t sz, char const *file, int line) {
    struct string_arena *arena = (struct string_arena *)alloc;

    (void)file;
    (void)line;

    sz                                 = alloc_align_to_word_size(sz);

    struct arena_header *bucket        = arena->head;
    struct arena_header *last          = null;
    size_t               last_capacity = 0;

    if (0 == sz) return null;

    assert(bucket);
    while (bucket) {
        if (bucket_has_capacity(bucket, sz)) {
            return bump_alloc_assume_capacity(bucket, sz);
        }

        last_capacity = bucket->capacity;
        last          = bucket;
        bucket        = bucket->next;
    }

    // need to allocate a new bucket

    size_t new_capacity = last_capacity * 2;
    if (new_capacity < sz) new_capacity = alloc_next_power_of_two(sz);

    last->next = alloc_malloc(arena->parent, new_capacity + sizeof(struct arena_header));
    if (null == last->next) return null;

    bucket = last->next;

    alloc_zero(bucket);
    bucket->capacity = new_capacity;

    return bump_alloc_assume_capacity(bucket, sz);
}

static void *arena_realloc(allocator *a, void *p, size_t sz, char const *file, int line) {
    (void)file;
    (void)line;

    if (null == p) return arena_malloc(a, sz, __FILE__, __LINE__);

    sz                          = alloc_align_to_word_size(sz);

    struct arena_header *bucket = find_bucket((struct string_arena *)a, p);
    if (null == bucket) {
        assert(false);
        return null;
    }

    size_t cur_size = block_size(p);
    if (sz == cur_size) return p;

    if (is_last_block(bucket, p)) {

        // shrink block in place
        if (sz < cur_size) {
            bucket->size -= cur_size - sz;
            return p;
        }

        // grow block if there is room in its bucket
        if (bucket_has_capacity(bucket, sz - cur_size)) {
            bucket->size += sz - cur_size;
            return p;
        }
    }

    if (sz < cur_size) return p; // cannot shrink block

    // need to allocate a new block, copy data and release old block if
    // possible

    void *new_block = arena_malloc(a, sz, __FILE__, __LINE__);
    assert(sz >= cur_size);
    memcpy(new_block, p, cur_size);
    maybe_free_block(bucket, p);
    return new_block;
}

static void *arena_calloc(allocator *alloc, size_t num, size_t sz, char const *file, int line) {
    (void)file;
    (void)line;

    sz        = alloc_align_to_word_size(sz);

    void *out = arena_malloc(alloc, num * sz, __FILE__, __LINE__);
    if (out) memset(out, 0, num * sz);
    return out;
}

static void arena_free(allocator *alloc, void *p, char const *file, int line) {
    (void)file;
    (void)line;
    (void)alloc;
    (void)p;
}

void alloc_string_arena_destroy(allocator *parent, allocator **arena_) {
    struct string_arena *arena = *(struct string_arena **)arena_;
    struct arena_header *next  = arena->head;

    while (next) {
        struct arena_header *next_next = next->next;
        alloc_free(arena->parent, next);
        next = next_next;
    }

    alloc_free(parent, *arena_);
    *arena_ = null;
}

allocator *alloc_string_arena_create(allocator *parent, size_t sz) {
    struct string_arena *arena = alloc_malloc(parent, sizeof *arena);

    arena->parent              = parent;
    sz                         = alloc_next_power_of_two(sz);

    if (0 == sz) sz = 16;
    arena->head = alloc_malloc(parent, sizeof(struct arena_header) + sz);
    alloc_zero(arena->head);
    arena->head->capacity    = sz;

    arena->allocator.malloc  = &arena_malloc;
    arena->allocator.calloc  = &arena_calloc;
    arena->allocator.realloc = &arena_realloc;
    arena->allocator.free    = &arena_free;

    return (allocator *)arena;
}
