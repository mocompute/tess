#include "callbacks.h"
#include <stdlib.h>

void sort_ints(long long *arr, int len, int (*compare)(const void *, const void *)) {
    qsort(arr, len, sizeof(long long), compare);
}

long long apply_op(long long a, long long b, tl_closure *op) {
    // Call through the closure: fn(ctx, a, b)
    // For non-capturing closures, ctx is NULL (becomes tl_ctx_raw).
    // For capturing closures, ctx points to the captured variables.
    return ((long long (*)(void *, long long, long long))op->fn)(op->ctx, a, b);
}

static tl_closure stored;

void              store_callback(tl_closure *fn) {
    // Copy the closure struct.  The fn pointer and ctx pointer are
    // preserved — ctx must point to heap-allocated memory that
    // outlives this call (i.e., [[alloc, capture(...)]]).
    stored = *fn;
}

long long run_stored_callback(void) {
    // Invoke the previously stored closure: fn(ctx) -> long long
    return ((long long (*)(void *))stored.fn)(stored.ctx);
}
