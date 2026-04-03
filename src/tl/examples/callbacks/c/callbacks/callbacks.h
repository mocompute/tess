#ifndef CALLBACKS_H
#define CALLBACKS_H

// Forward declaration only — the full definition lives in callbacks.c
// (or is provided by the Tess runtime when included from generated code).
struct tl_closure;
typedef struct tl_closure tl_closure;

// Non-capturing: accepts a plain C function pointer (qsort-style).
void sort_ints(long long *arr, int len, int (*compare)(const void *, const void *));

// Non-capturing: applies a unary function pointer to a long long value.
long long apply_int_fn(long long x, long long (*f)(long long));

// Non-capturing: applies a unary function pointer to a double value.
double apply_float_fn(double x, double (*f)(double));

// Stack-based capturing: calls closure synchronously with two args.
// Safe for stack-allocated closures because the call happens immediately.
long long apply_op(long long a, long long b, tl_closure *op);

// Heap-allocated capturing: stores a closure for later invocation.
// The closure must use heap-allocated captures ([[alloc, capture(...)]]).
void      store_callback(tl_closure *fn);
long long run_stored_callback(void);

#endif
