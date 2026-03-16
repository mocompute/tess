# cbind parser hardening — investigation plan

## Status: In progress

The `tess cbind` command works on simple headers and produces correct output for `string.h`. But on `stdlib.h`, only ~22 of ~100 functions are captured. The parser processes the entire 150KB preprocessed output without hanging, but silently drops most declarations.

## What's been done (uncommitted)

These fixes are in the working tree and should be committed:

1. **Architectural: `next_token` auto-skips `# N "file"` line markers** — makes line markers invisible to all parser code, handling them at the tokenizer level instead of per-site patches
2. **`__builtin_va_list`** — moved from `skip_attribute()` to `parse_type()` as a recognized type
3. **Comma-separated struct fields** — fixed mixed pointer depths (`int *a, b;`)
4. **Extended `skip_attribute()`** — handles `_Alignas`, `_Atomic`, `_Complex`, `__declspec`, `_Pragma`, `__typeof__`, `__auto_type`, etc.
5. **Number tokenizer** — removed `-` prefix (was eating operators), `+`/`-` only after exponent chars
6. **Struct function pointer fields** — `void (*callback)(int)` inside structs now parsed
7. **Forward struct dedup** — O(N^2) scan replaced with hashset
8. **Progress guards** — struct body loop has no-progress fallback
9. **`static` keyword** — consumed before type parsing (handles `static inline`)
10. **`parse_line_directive` removed** — folded into `next_token`
11. **Struct body line-marker loop removed** — redundant after architectural fix

## What remains to investigate

### Primary issue: stdlib.h produces only 22/~100 functions

The parser processes all 150KB of preprocessed `stdlib.h` output (pos reaches 149546/149645), ends with `in_target=1`, but only captures 22 declarations. Functions like `malloc`, `free`, `random`, `exit`, `abs`, `qsort`, `bsearch` are missing.

**Likely causes to investigate:**

1. **Typedef unions**: glibc's `sys/types.h` (included by `stdlib.h`) has `typedef union { ... } pthread_barrier_t;`. The parser may not handle `typedef union` (it checks for `struct` and `union` in the typedef handler, but the union path may have issues).

2. **`volatile` in typedefs**: `typedef volatile int pthread_spinlock_t;` — the `volatile` keyword may confuse `parse_type()`.

3. **Chained `__attribute__` with line markers inside parentheses**: `malloc`'s declaration has `__attribute__((__alloc_size__(1)))` split across line markers. The paren depth tracking in `skip_paren_block` uses `next_token` (which auto-skips line markers), so this should work — but needs verification.

4. **`__extension__` before function declarations**: glibc uses `__extension__ extern long long int atoll(...)`. The `__extension__` is consumed by `skip_all_attributes`, but `extern` is then the first token and gets consumed by the extern handler. Need to trace whether this works correctly.

5. **`parse_toplevel_decl` `first` token stale after `extern`/`static` consume**: After the `extern` handler consumes the token and falls through, `first` still holds the old peeked token. Subsequent `tok_eq(first, ...)` checks use stale data. The `first.kind == CTK_IDENT` check at the function parsing block passes (since "extern" was an IDENT), but `parse_type()` reads from the advanced position. This is intentional but fragile.

### Investigation approach

1. **Add debug tracing**: Temporarily add `fprintf(stderr, ...)` in `parse_toplevel_decl` to log each token it processes and which branch it takes
2. **Feed known-failing subsection**: Extract the `random()` declaration from preprocessed stdlib.h into a unit test
3. **Binary search**: Feed first half / second half of preprocessed output to find where parsing goes wrong
4. **Check union typedefs**: Write test for `typedef union { char s[4]; int a; } myunion;`

## Test results

27 tests pass (19 original + 8 edge-case tests added during hardening). The embedded-line-marker test (simulating `malloc`'s pattern) passes in isolation.
