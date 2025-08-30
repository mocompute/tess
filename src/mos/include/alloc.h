#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include "nodiscard.h"

#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

// -- fatal --

noreturn void fatal_i(char const *file, int line, char const *restrict, ...)
  __attribute__((format(printf, 3, 4)));

#define fatal(...) fatal_i(__FILE__, __LINE__, __VA_ARGS__)

typedef struct allocator allocator;

// -- default allocator --

allocator *alloc_default_allocator();

// -- leak detection --
nodiscard allocator *alloc_leak_detector_create() mallocfun;
void                 alloc_leak_detector_destroy(allocator **);
void                 alloc_leak_detector_report(allocator *);

// -- arena bump allocator --

nodiscard allocator *alloc_arena_create(allocator *alloc, size_t) mallocfun;
void                 alloc_arena_dealloc(allocator *parent, allocator **arena);
void                 alloc_arena_destroy(allocator *, allocator **);
void                 alloc_arena_init(allocator *, allocator *parent, size_t);
void                 alloc_arena_deinit(allocator *);

// -- allocator malloc and friends --
//
// these allocations never fail: failures call fatal() and exit the program.

void *alloc_malloc_i(allocator *, size_t, char const *, int) mallocfun;
void *alloc_calloc_i(allocator *, size_t, size_t, char const *, int) mallocfun;
void *alloc_realloc_i(allocator *, void *, size_t, char const *, int);
void  alloc_free_i(allocator *, void *, char const *, int);

#define alloc_malloc(A, S)     alloc_malloc_i((A), (S), __FILE__, __LINE__)
#define alloc_calloc(A, N, S)  alloc_calloc_i((A), (N), (S), __FILE__, __LINE__)
#define alloc_realloc(A, P, S) alloc_realloc_i((A), (P), (S), __FILE__, __LINE__)
#define alloc_free(A, P)       alloc_free_i((A), (P), __FILE__, __LINE__)

// -- utilities --

void   alloc_invalidate_n(void *, size_t);
void   alloc_assert_invalid_n(void *, size_t);

char  *alloc_strdup(allocator *, char const *) mallocfun;
char  *alloc_strndup(allocator *, char const *, size_t) mallocfun;

size_t alloc_next_power_of_two(size_t);
size_t alloc_align_to_word_size(size_t);

#define alloc_invalidate(P)     alloc_invalidate_n((P), sizeof *(P))
#define alloc_assert_invalid(P) alloc_assert_invalid_n((P), sizeof *(P))
#define alloc_zero(P)           memset((P), 0, sizeof *(P));
#define alloc_copy(DST, SRC)    memcpy((DST), (SRC), sizeof *(DST));
#define alloc_struct(A, NAME)   alloc_calloc((A), 1, sizeof *NAME)
#endif
