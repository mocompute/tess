#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include "nodiscard.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>

typedef struct allocator {
    void *(*malloc)(struct allocator *, size_t, char const *, int);
    void *(*calloc)(struct allocator *, size_t num, size_t size, char const *, int);
    void *(*realloc)(struct allocator *, void *, size_t, char const *, int);
    void (*free)(struct allocator *, void *, char const *, int);
} allocator;

typedef struct {
    size_t size;
    byte   data[];
} arena_block;

typedef struct arena_header {
    struct arena_header *next;
    size_t               capacity;
    size_t               size;
    byte                 data[];
} arena_header;

typedef struct arena_allocator {
    struct allocator allocator;
    allocator       *parent;
    arena_header    *head;
} arena_allocator;

// -- default allocator --

allocator *alloc_default_allocator();

// -- arena bump allocator --

allocator    *alloc_arena_create(allocator *alloc, size_t);
void          alloc_arena_dealloc(allocator *parent, allocator **arena);
void          alloc_arena_destroy(allocator *, allocator **);
nodiscard int alloc_arena_init(allocator *, allocator *parent, size_t);
void          alloc_arena_deinit(allocator *);

// -- allocator malloc and friends --

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

#endif
