#ifndef CALLBACKS_H
#define CALLBACKS_H

typedef struct tl_closure {
    void *fn;
    void *ctx;
} tl_closure;

// Non-capturing: accepts a plain C function pointer (qsort-style).
void sort_ints(long long *arr, int len, int (*compare)(const void *, const void *));

// Stack-based capturing: calls closure synchronously with two args.
// Safe for stack-allocated closures because the call happens immediately.
long long apply_op(long long a, long long b, tl_closure *op);

// Heap-allocated capturing: stores a closure for later invocation.
// The closure must use heap-allocated captures ([[alloc, capture(...)]]).
void      store_callback(tl_closure *fn);
long long run_stored_callback(void);

#endif
