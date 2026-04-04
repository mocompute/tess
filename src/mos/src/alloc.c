#include "alloc.h"
#include "alloc_internal.h"

#include "platform.h"
#include "types.h"

#include <assert.h>
#include <signal.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <string.h>

#define ARENA_MAX_BUCKET_SIZE (8 * 1024 * 1024) // 8 MB cap on bucket growth

// MSVC may not define max_align_t in stddef.h
#if defined(_MSC_VER)
typedef double max_align_t;
#endif

#include <stdlib.h>

#define ALLOC_DEBUG_PATTERN 0xCD

static allocator *default_budgeted_allocator = null;

typedef struct {
    size_t size;
} arena_block;

typedef struct arena_header {
    struct arena_header *next;
    size_t               capacity;
    size_t               size;
} arena_header;

#define arena_header_data(h) ((arena_block *)(((char *)(h)) + sizeof(arena_header)))

typedef struct arena_allocator {
    struct allocator allocator;
    allocator       *parent;
    arena_header    *head;
    arena_header    *tail; // cached pointer to current/last bucket for O(1) allocation
    size_t           peak_allocated;
    void            *last_alloc; // last allocated pointer for O(1) arena_free fast path
} arena_allocator;

static void *default_malloc(allocator *a, size_t sz, char const *, int) mallocfun;
static void *default_calloc(allocator *a, size_t num, size_t sz, char const *, int) mallocfun;

static void *default_malloc(allocator *a, size_t sz, char const *file, int line) {
    (void)file;
    (void)line;
    (void)a;
    return malloc(sz);
}

static void *default_calloc(allocator *a, size_t num, size_t sz, char const *file, int line) {
    (void)file;
    (void)line;
    (void)a;
    void *ptr = calloc(num, sz); // calloc checks for overflow
    return ptr;
}

static void *default_realloc(allocator *a, void *p, size_t sz, char const *file, int line) {
    (void)file;
    (void)line;
    (void)a;
    return realloc(p, sz);
}

static void default_free(allocator *a, void *p, char const *file, int line) {
    (void)file;
    (void)line;
    (void)a;
    free(p);
}

allocator *default_allocator(void) {
    static allocator allocator = {&default_malloc, &default_calloc, &default_realloc, &default_free};
    return &allocator;
}

allocator *get_budgeted_allocator(void) {
    if (default_budgeted_allocator) return default_budgeted_allocator;
    else return default_allocator();
}

void alloc_default_budgeted_allocator_set(allocator *alloc) {
    default_budgeted_allocator = alloc;
}

void alloc_default_budgeted_allocator_free(void) {
    if (default_budgeted_allocator) {
        budgeted_destroy(&default_budgeted_allocator);
    }
}

// -- allocator malloc and friends --

void *alloc_malloc_i(allocator *alloc, size_t sz, char const *file, int line) {
    if (!sz) return null;
    void *ptr = alloc->malloc(alloc, sz, file, line);
    if (!ptr) fatal("malloc failed\n");
#ifndef NDEBUG
    alloc_invalidate_n(ptr, sz);
#endif
    return ptr;
}

void *alloc_calloc_i(allocator *alloc, size_t count, size_t sz, char const *file, int line) {
    if (!sz || !count) return null;
    void *ptr = alloc->calloc(alloc, count, sz, file, line);
    if (!ptr) fatal("calloc failed\n");
    return ptr;
}

void *alloc_realloc_i(allocator *alloc, void *ptr, size_t sz, char const *file, int line) {
    assert(ptr);
    void *out = alloc->realloc(alloc, ptr, sz, file, line);
    if (!out) fatal("realloc failed: ptr=%p, requested size=%zu\n", ptr, sz);
    return out;
}

void alloc_free_i(allocator *alloc, void *ptr, char const *file, int line) {
    alloc->free(alloc, ptr, file, line);
}

// -- arena --
//
// Each bucket has an arena_header struct. Each allocated block has a
// size_t header to record its size (struct arena_allocation).

static void *bump_alloc_assume_capacity(arena_header *bucket, size_t sz) {
    arena_block *out = (void *)(((byte *)bucket) + bucket->size);

    out->size        = sz;
    bucket->size += sz + sizeof(arena_block);
    assert(bucket->size <= bucket->capacity);

    void *res = ((char *)out) + sizeof(*out);
    return res;
}

static size_t *block_size(byte const *p) {
    arena_block *block = (void *)(p - sizeof(size_t));
    return &block->size;
}

static int is_last_block(arena_header const *bucket, void const *p) {
    size_t sz = *block_size(p);
    if (((byte *)p) < ((byte *)bucket)) return 0;
    // Is the pointer plus its allocated size pointing exactly to the end of the bucket?

    return (size_t)(((byte *)p) - ((byte *)arena_header_data(bucket)) + sz) ==
           bucket->size - sizeof(arena_header);
}

static void maybe_free_block(arena_header *bucket, void const *ptr) {
    if (is_last_block(bucket, ptr)) bucket->size -= *block_size(ptr) + sizeof(arena_block);
}

static int bucket_has_capacity(arena_header const *bucket, size_t size) {
    return (bucket->capacity - bucket->size >= sizeof(arena_block) + size);
}

static arena_header *find_bucket(arena_allocator const *arena, void const *ptr) {
    // find bucket where ptr is allocated
    arena_header *bucket = arena->head;
    assert(bucket);
    while (bucket) {
        if (((byte *)ptr) > ((byte *)bucket) + sizeof(arena_header) &&
            ((byte *)ptr) < ((byte *)bucket + bucket->size))
            return bucket;

        bucket = bucket->next;
    }

    return null;
}

static void arena_header_init(arena_header *self, size_t capacity) {
    alloc_zero(self);
    self->next     = null;
    self->capacity = capacity;
    self->size     = sizeof(arena_header);
}

static arena_header *arena_header_create(allocator *alloc, size_t capacity) {
    assert(capacity > sizeof(arena_header));
    void *out = alloc_malloc(alloc, capacity);
    arena_header_init(out, capacity);
    return out;
}

static void *arena_malloc(allocator *alloc, size_t sz, char const *file, int line) {
    arena_allocator *arena = (arena_allocator *)alloc;

    (void)file;
    (void)line;

    sz = alloc_align_to_pointer_size(sz);
    if (0 == sz) return null;

    // Fast path: check tail bucket first (most allocations succeed here)
    arena_header *tail = arena->tail;
    assert(tail);
    if (bucket_has_capacity(tail, sz)) {
        void *res         = bump_alloc_assume_capacity(tail, sz);
        arena->last_alloc = res;
        return res;
    }

    // Slow path: walk forward from tail looking for a bucket with capacity.
    // Only search forward (never backwards) so that arena_save/arena_restore
    // watermarks remain safe — a later restore cannot rewind past earlier allocations.
    arena_header *bucket = tail->next;
    while (bucket) {
        if (bucket_has_capacity(bucket, sz)) {
            arena->tail       = bucket;
            void *res         = bump_alloc_assume_capacity(bucket, sz);
            arena->last_alloc = res;
            return res;
        }
        tail   = bucket;
        bucket = bucket->next;
    }

    // Need to allocate a new bucket.
    // Cap growth at ARENA_MAX_BUCKET_SIZE to limit capacity waste from doubling.
    // Without the cap, a 342 MB arena ends up with 510 MB of bucket capacity.
    size_t needed       = sz + sizeof(arena_header) + sizeof(arena_block);
    size_t new_capacity = tail->capacity * 2;
    if (new_capacity == 0 || new_capacity < tail->capacity) return null; // overflow
    if (new_capacity > ARENA_MAX_BUCKET_SIZE) new_capacity = ARENA_MAX_BUCKET_SIZE;
    if (new_capacity < needed) new_capacity = needed;

    tail->next = arena_header_create(arena->parent, new_capacity);
    if (null == tail->next) return null;

    arena->tail       = tail->next;
    void *res         = bump_alloc_assume_capacity(arena->tail, sz);
    arena->last_alloc = res;
    return res;
}

static void *arena_realloc(allocator *a, void *p, size_t sz, char const *file, int line) {
    (void)file;
    (void)line;

    if (null == p) return arena_malloc(a, sz, __FILE__, __LINE__);

    arena_allocator *arena = (arena_allocator *)a;
    sz                     = alloc_align_to_pointer_size(sz);
    arena_header *bucket   = (p == arena->last_alloc) ? arena->tail : find_bucket(arena, p);
    if (null == bucket) {
        assert(0);
        return null;
    }

    size_t *cur_size_p = block_size(p);
    size_t  cur_size   = *cur_size_p;
    if (sz == cur_size) return p;

    if (is_last_block(bucket, p)) {

        // shrink block in place
        if (sz < cur_size) {
            *cur_size_p = sz;
            bucket->size -= cur_size - sz;
            return p;
        }

        // grow block if there is room in its bucket
        if (bucket_has_capacity(bucket, sz - cur_size)) {
            *cur_size_p = sz;
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

    sz        = alloc_align_to_pointer_size(sz);
    void *out = arena_malloc(alloc, num * sz, __FILE__, __LINE__);
    if (out) memset(out, 0, num * sz);
    return out;
}

static void arena_free(allocator *a, void *p, char const *file, int line) {
    (void)file;
    (void)line;

    if (null == p) return;

    arena_allocator *arena = (arena_allocator *)a;
    if (p != arena->last_alloc) return;
    arena->last_alloc = null;
    maybe_free_block(arena->tail, p);
}

static void arena_init(allocator *arena_, allocator *parent, size_t sz) {
    arena_allocator *arena = (arena_allocator *)arena_;
    arena->parent          = parent;
    arena->peak_allocated  = 0;
    arena->last_alloc      = null;
    sz                     = alloc_next_power_of_two(sizeof(arena_header) + sz);
    if (0 == sz) sz = 16; // overflow (TODO)

    arena->head              = arena_header_create(parent, sz);
    arena->tail              = arena->head; // tail tracks current allocation bucket
    arena->allocator.malloc  = &arena_malloc;
    arena->allocator.calloc  = &arena_calloc;
    arena->allocator.realloc = &arena_realloc;
    arena->allocator.free    = &arena_free;
}

allocator *arena_create(allocator *alloc, size_t sz) {
    allocator *out = alloc_malloc(alloc, sizeof(struct arena_allocator));
    arena_init(out, alloc, sz);
    return out;
}

void arena_destroy(allocator **arena_) {
    arena_allocator *arena = *((arena_allocator **)arena_);

    arena_header    *next  = arena->head;

    while (next) {
        arena_header *next_next = next->next;
        alloc_free(arena->parent, next);
        next = next_next;
    }

    alloc_free(arena->parent, *arena_);
    *arena_ = null;
}

void arena_reset(allocator *arena_) {
    // resets every block to size 0 (plus arena_header)
    arena_allocator *arena = (arena_allocator *)arena_;

    // Compute current total and update peak
    size_t current = 0;
    for (arena_header *h = arena->head; h; h = h->next) current += h->size - sizeof(arena_header);
    if (current > arena->peak_allocated) arena->peak_allocated = current;

    // Reset buckets
    arena_header *next = arena->head;
    while (next) {
        next->size = sizeof(arena_header);
        next       = next->next;
    }

    // Reset tail to head so allocations start from beginning
    arena->tail       = arena->head;
    arena->last_alloc = null;
}

arena_watermark arena_save(allocator *arena_) {
    arena_allocator *arena = (arena_allocator *)arena_;
    return (arena_watermark){.bucket = arena->tail, .size = arena->tail->size};
}

void arena_restore(allocator *arena_, arena_watermark wm) {
    arena_allocator *arena = (arena_allocator *)arena_;
    arena_header    *saved = (arena_header *)wm.bucket;

    saved->size            = wm.size;
    for (arena_header *h = saved->next; h; h = h->next) h->size = sizeof(arena_header);
    arena->tail       = saved;
    arena->last_alloc = null;
}

void arena_get_stats(allocator *arena_, arena_stats *out) {
    arena_allocator *arena = (arena_allocator *)arena_;
    out->allocated = out->capacity = out->bucket_count = 0;
    for (arena_header *h = arena->head; h; h = h->next) {
        out->allocated += h->size - sizeof(arena_header);
        out->capacity += h->capacity - sizeof(arena_header);
        out->bucket_count++;
    }
    out->peak_allocated = arena->peak_allocated > out->allocated ? arena->peak_allocated : out->allocated;
}

// -- budgeted allocator --

typedef struct {
    struct allocator allocator;
    allocator       *inner;
    size_t           used;
    size_t           limit;
} budgeted_allocator;

static void budgeted_check(budgeted_allocator *b, size_t sz) {
    if (b->used + sz > b->limit)
        fatal("memory limit exceeded (%zu bytes used, %zu requested, %zu limit)", b->used, sz, b->limit);
}

static void *budgeted_malloc(allocator *a, size_t sz, char const *file, int line) {
    budgeted_allocator *b = (budgeted_allocator *)a;
    budgeted_check(b, sz);
    void *p = b->inner->malloc(b->inner, sz, file, line);
    if (p) b->used += sz;
    return p;
}

static void *budgeted_calloc(allocator *a, size_t num, size_t sz, char const *file, int line) {
    budgeted_allocator *b     = (budgeted_allocator *)a;
    size_t              total = sz && num > SIZE_MAX / sz ? SIZE_MAX : num * sz;
    budgeted_check(b, total);
    void *p = b->inner->calloc(b->inner, num, sz, file, line);
    if (p) b->used += total;
    return p;
}

static void *budgeted_realloc(allocator *a, void *p, size_t sz, char const *file, int line) {
    budgeted_allocator *b = (budgeted_allocator *)a;
    budgeted_check(b, sz);
    void *out = b->inner->realloc(b->inner, p, sz, file, line);
    if (out) b->used += sz;
    return out;
}

static void budgeted_free(allocator *a, void *p, char const *file, int line) {
    budgeted_allocator *b = (budgeted_allocator *)a;
    b->inner->free(b->inner, p, file, line);
}

allocator *budgeted_create(allocator *inner, size_t limit) {
    budgeted_allocator *b = (budgeted_allocator *)malloc(sizeof *b);
    if (!b) fatal("budgeted_create: malloc failed");
    b->allocator.malloc  = &budgeted_malloc;
    b->allocator.calloc  = &budgeted_calloc;
    b->allocator.realloc = &budgeted_realloc;
    b->allocator.free    = &budgeted_free;
    b->inner             = inner;
    b->used              = 0;
    b->limit             = limit;
    return (allocator *)b;
}

void budgeted_destroy(allocator **a) {
    free(*a);
    *a = null;
}

size_t budgeted_get_used(allocator *a) {
    return ((budgeted_allocator *)a)->used;
}

size_t budgeted_get_limit(allocator *a) {
    return ((budgeted_allocator *)a)->limit;
}

// -- leak detector allocator --

enum leak_status {
    leak_action_alloc = 1,
    leak_action_realloc,
    leak_action_free,
    leak_status_reallocated,
    leak_status_alloc_matched,
    leak_status_realloc_matched,
    leak_status_free_matched,
    leak_error_extra_free,
    leak_error_alloc_leak,
};
struct leak_allocation {
    void            *ptr;
    void            *realloc_ptr; // original ptr
    size_t           size;
    char const      *file;
    int              line;
    enum leak_status status;
};

typedef struct {
    struct allocator        allocator;
    struct leak_allocation *data;
    i64                     capacity;
    i64                     size;
    int                     was_reported;
} leak_detector;

static void *leak_detector_malloc(allocator *alloc, size_t sz, char const *file, int line) mallocfun;
static void *leak_detector_calloc(allocator *alloc, size_t num, size_t sz, char const *file,
                                  int line) mallocfun;
static void *leak_detector_realloc(allocator *a, void *p, size_t sz, char const *file, int line);
static void  leak_detector_free(allocator *alloc, void *p, char const *file, int line);

allocator   *leak_detector_create(void) {

    leak_detector *self = malloc(sizeof *self);
    assert(self);
    if (!self) fatal("malloc failed\n");

    self->allocator.malloc  = &leak_detector_malloc;
    self->allocator.calloc  = &leak_detector_calloc;
    self->allocator.realloc = &leak_detector_realloc;
    self->allocator.free    = &leak_detector_free;

    self->capacity          = 1024;
    self->size              = 0;
    self->data              = calloc((size_t)self->capacity, sizeof(struct leak_allocation));
    self->was_reported      = 0;

    return (allocator *)self;
}

void leak_detector_destroy(allocator **alloc) {
    leak_detector *self = (leak_detector *)*alloc;

    if (!self->was_reported) leak_detector_report(*alloc);

    free(self->data);

    free(*alloc);
    *alloc = null;
}

void leak_detector_report(allocator *alloc) {
    leak_detector          *self    = (leak_detector *)alloc;

    struct leak_allocation *records = malloc(sizeof *records * (size_t)self->size);
    if (!records) {
        fprintf(stderr, "alloc_leak_detector_report: out of memory\n");
        goto cleanup;
    }
    memcpy(records, self->data, (size_t)self->size * sizeof *records);

    self->was_reported = 1;

    // match reallocs with previous allocs
    for (i64 i = 0; i < self->size; ++i) {
        if (leak_action_realloc != records[i].status) continue;

        if (i == 0) {
            fprintf(stderr, "alloc_leak_detector_report: invariant violated.\n");
            goto cleanup;
        }

        // realloc_ptr is original pointer; go back to prior
        // allocations and mark it as reallocated
        void *const ptr = records[i].realloc_ptr;
        for (i64 j = i - 1; j >= 0; --j) {
            if (ptr == records[j].ptr) {
                records[j].status = leak_status_reallocated;
                break;
            }
        }
    }

    // try to match frees with prior allocs
    for (i64 i = 0; i < self->size; ++i) {
        if (leak_action_free != records[i].status) continue;

        if (i == 0) {
            fprintf(stderr, "alloc_leak_detector_report: invariant violated.\n");
            goto cleanup;
        }

        void *const ptr = records[i].ptr;
        for (i64 j = i - 1; j >= 0; --j) {
            if (ptr == records[j].ptr) {

                if (records[j].status == leak_action_alloc) {
                    records[j].status = leak_status_alloc_matched;
                    records[i].status = leak_status_free_matched;

                } else if (records[j].status == leak_action_realloc) {
                    records[j].status = leak_status_realloc_matched;
                    records[i].status = leak_status_free_matched;
                } else {
                    // double-free
                    records[i].status = leak_error_extra_free;
                }
            }
        }
    }

    // mark remaining unmatched allocs
    for (i64 i = 0; i < self->size; ++i) {
        if (leak_action_alloc == records[i].status) records[i].status = leak_error_alloc_leak;
    }

    // report
    for (i64 i = 0; i < self->size; ++i) {
        if (leak_error_alloc_leak != records[i].status) continue;

        if (records[i].realloc_ptr) {
            int found = 0;
            for (i64 j = i - 1; j >= 0; --j) {
                if (records[i].realloc_ptr == records[j].ptr) {
                    fprintf(stderr,
                            "leak_detector: leak of %zu bytes at %p reallocated by %s:%i, original "
                            "allocation at %p %s:%i\n",
                            records[i].size, records[i].ptr, records[i].file, records[i].line,
                            records[j].ptr, records[j].file, records[j].line);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "leak_detector: leak of %zu bytes at %p reallocated by %s:%i\n",
                        records[i].size, records[i].ptr, records[i].file, records[i].line);
            }

        } else {
            fprintf(stderr, "leak_detector: leak of %zu bytes at %p allocated by %s:%i\n", records[i].size,
                    records[i].ptr, records[i].file, records[i].line);
        }
    }

cleanup:
    free(records);
}

static size_t leak_detector_ensure_good_free(leak_detector *self, void *ptr, char const *file, int line) {
    // ensure the attempted free of ptr is valid.

    if (null == ptr) return 0; // always valid

    if (0 == self->size) {
        fatal("leak_detector: attempt to free %p before any malloc\n", ptr);
    }

    for (i64 i = self->size - 1; i >= 0; --i) {
        if (ptr == self->data[i].ptr &&
            (leak_action_alloc == self->data[i].status || leak_action_realloc == self->data[i].status))
            return self->data[i].size;
    }

    fatal("leak_detector: attempt to free unknown pointer %p: %s:%i\n", ptr, file, line);
}

static void leak_detector_reserve_one(leak_detector *self) {
    if (self->size < self->capacity) return;

    i64   new_capacity = self->capacity * 2;
    void *resized      = realloc(self->data, (size_t)new_capacity * sizeof(struct leak_allocation));
    if (!resized) fatal("leak_detector: out of memory: %zu\n", (size_t)new_capacity);

    self->data     = resized;
    self->capacity = new_capacity;
}

static void *leak_detector_malloc(allocator *alloc, size_t sz, char const *file, int line) {
    leak_detector *self = (leak_detector *)alloc;
    leak_detector_reserve_one(self);

    void *ptr = malloc(sz);
    alloc_invalidate_n(ptr, sz);
    self->data[self->size++] = (struct leak_allocation){
      .ptr = ptr, .realloc_ptr = null, .size = sz, .file = file, .line = line, .status = leak_action_alloc};
    return ptr;
}

static void *leak_detector_calloc(allocator *alloc, size_t num, size_t sz, char const *file, int line) {
    leak_detector *self = (leak_detector *)alloc;
    leak_detector_reserve_one(self);

    void *ptr                = calloc(num, sz);
    self->data[self->size++] = (struct leak_allocation){
      .ptr = ptr, .realloc_ptr = null, .size = sz, .file = file, .line = line, .status = leak_action_alloc};
    return ptr;
}

static void *leak_detector_realloc(allocator *alloc, void *p, size_t sz, char const *file, int line) {
    leak_detector *self = (leak_detector *)alloc;
    leak_detector_reserve_one(self);

    struct leak_allocation leak_record = {
      .realloc_ptr = p, .size = sz, .file = file, .line = line, .status = leak_action_realloc};

    self->data[self->size]     = leak_record;
    void *ptr                  = realloc(p, sz);
    self->data[self->size].ptr = ptr;
    self->size++;
    return ptr;
}

static void leak_detector_free(allocator *alloc, void *ptr, char const *file, int line) {
    leak_detector *self = (leak_detector *)alloc;
    size_t         size = leak_detector_ensure_good_free(self, ptr, file, line);
    leak_detector_reserve_one(self);

    self->data[self->size++] = (struct leak_allocation){
      .ptr = ptr, .realloc_ptr = null, .size = 0, .file = file, .line = line, .status = leak_action_free};

    alloc_invalidate_n(ptr, size);

    free(ptr);
}

// -- utilities --

char *alloc_strdup(allocator *alloc, char const *src) {
    size_t len = strlen(src);
    char  *out = alloc_malloc(alloc, len + 1);
    if (out) {
        memcpy(out, src, len);
        out[len] = '\0';
    }
    return out;
}

char *alloc_strndup(allocator *alloc, char const *src, size_t max) {

    size_t      len = 0;
    char const *ch  = src;
    while (len < max && *ch++) len++; // don't use strlen

    char *out = alloc_malloc(alloc, len + 1);
    if (out) {
        memcpy(out, src, len);
        out[len] = '\0';
    }
    return out;
}

void alloc_invalidate_n(void *p, size_t len) {
    if (!p) return;
    memset(p, ALLOC_DEBUG_PATTERN, len);
}

void alloc_assert_invalid_n(void *p, size_t len) {
#ifndef NDEBUG
    byte *bp = (byte *)p;
    while (len--) {
        assert(*bp == ALLOC_DEBUG_PATTERN);
        bp++;
    };
#else
    (void)p;
    (void)len;
#endif
}

// Returns: input if already a power of two, or else the next higher
// power of two.
size_t alloc_next_power_of_two(size_t n) {

    if (n > (SIZE_MAX / 2) + 1) return 0; // overflow
    if (n == 0) return 1;

    // set all bits to the right of the highest set bit by masking.
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

size_t alloc_align(size_t n, size_t align) {
    assert(align != 0);
    size_t mask = align - 1;
    return (n + mask) & ~mask;
}

size_t alloc_align_to_max(size_t n) {
    return alloc_align(n, sizeof(max_align_t));
}

size_t alloc_align_to_pointer_size(size_t n) {
    return alloc_align(n, sizeof(void *));
}

noreturn void fatal_i(char const *file, int line, char const *restrict fmt, ...) {
#define BUF_SIZE 256
    char    buf[BUF_SIZE];
    va_list args;

    //

    int len = snprintf(buf, BUF_SIZE, "%s:%i: fatal: ", file, line);
    if (len < 0) exit(1);

    va_start(args, fmt);
    vsnprintf(buf + len, BUF_SIZE - (size_t)len, fmt, args);
    va_end(args);

    fprintf(stderr, "%s\n", buf);

#ifdef MOS_WINDOWS
    if (IsDebuggerPresent()) {
        __debugbreak();
    }
#elif defined(__has_builtin) && __has_builtin(__builtin_debugtrap)
    __builtin_debugtrap();
#elif defined(SIGTRAP)
    raise(SIGTRAP);
#endif
    exit(1);
}
