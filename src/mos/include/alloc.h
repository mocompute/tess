#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include "nodiscard.h"
#include "platform.h"

#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

// -- fatal --

// Important: Allocations via this alloc.h API never fail. If the underlying allocator fails, the program
// exits with fatal().

#ifndef MOS_WINDOWS
noreturn void fatal_i(char const *file, int line, char const *restrict, ...)
  __attribute__((format(printf, 3, 4)));
#else
noreturn void fatal_i(char const *file, int line, char const *restrict, ...);
#endif

#define fatal(...) fatal_i(__FILE__, __LINE__, __VA_ARGS__)

typedef struct allocator allocator;

// -- default allocator --

allocator *default_allocator(void);
allocator *get_budgeted_allocator(void);
void       alloc_default_budgeted_allocator_set(allocator *);
void       alloc_default_budgeted_allocator_free(void);

// -- leak detection --
nodiscard allocator *leak_detector_create(void) mallocfun;
void                 leak_detector_destroy(allocator **);
void                 leak_detector_report(allocator *);

// -- arena bump allocator --

nodiscard allocator *arena_create(allocator *alloc, size_t) mallocfun;
void                 arena_destroy(allocator **);

// reset arena to empty but retain all capacity
void arena_reset(allocator *);

// -- arena watermark (save/restore) --

typedef struct {
    void  *bucket; // opaque: arena_header*
    size_t size;   // bucket->size at save time
} arena_watermark;

nodiscard arena_watermark arena_save(allocator *arena);
void                      arena_restore(allocator *arena, arena_watermark wm);

// -- budgeted allocator --

nodiscard allocator *budgeted_create(allocator *inner, size_t limit) mallocfun;
void                 budgeted_destroy(allocator **);
size_t               budgeted_get_used(allocator *);
size_t               budgeted_get_limit(allocator *);

// -- arena statistics --

typedef struct {
    size_t allocated;      // Current bytes in use
    size_t capacity;       // Total capacity across all buckets
    size_t peak_allocated; // High-water mark
    size_t bucket_count;   // Number of buckets
} arena_stats;

void arena_get_stats(allocator *arena, arena_stats *out);

// -- allocator malloc and friends --
//
// these allocations never fail: failures call fatal() and exit the program.

nodiscard void *alloc_malloc_i(allocator *, size_t, char const *, int) mallocfun;
nodiscard void *alloc_calloc_i(allocator *, size_t, size_t, char const *, int) mallocfun;
nodiscard void *alloc_realloc_i(allocator *, void *, size_t, char const *, int);
void            alloc_free_i(allocator *, void *, char const *, int);

#define alloc_malloc(A, S)     alloc_malloc_i((A), (S), __FILE__, __LINE__)
#define alloc_calloc(A, N, S)  alloc_calloc_i((A), (N), (S), __FILE__, __LINE__)
#define alloc_realloc(A, P, S) alloc_realloc_i((A), (P), (S), __FILE__, __LINE__)
#define alloc_free(A, P)       alloc_free_i((A), (P), __FILE__, __LINE__)

// -- utilities --

void            alloc_invalidate_n(void *, size_t);
void            alloc_assert_invalid_n(void *, size_t);

nodiscard char *alloc_strdup(allocator *, char const *) mallocfun;
nodiscard char *alloc_strndup(allocator *, char const *, size_t) mallocfun;

size_t          alloc_next_power_of_two(size_t);
size_t          alloc_align_to_max(size_t);
size_t          alloc_align_to_pointer_size(size_t);
size_t          alloc_align(size_t n, size_t align);

#define alloc_assert_invalid(P) alloc_assert_invalid_n((P), sizeof *(P))
#define alloc_zero(P)           memset((P), 0, sizeof *(P));
#define alloc_invalidate(P)     memset((P), 0xCD, sizeof *(P));
#define alloc_copy(DST, SRC)    memcpy((DST), (SRC), sizeof *(DST));

#define new(A, TYPE)            (TYPE *)alloc_malloc((A), sizeof(TYPE))

#endif
