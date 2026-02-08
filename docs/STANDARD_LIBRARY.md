# Standard Library Reference

> **Work in Progress.** The standard library is under active development. APIs may change.

The Tess standard library is automatically included when compiling with `tess exe` or `tess lib`. Standard library modules are imported using angle brackets (e.g., `#import <Array.tl>`). To disable default standard paths, pass `--no-standard-includes` and specify custom standard paths with `-S`.

Import a module with:

```tl
#import <Array.tl>
#import <Alloc.tl>
```

---

## builtin

The `builtin` module is always available — no import needed. It provides:

| Symbol | Type | Description |
|--------|------|-------------|
| `sizeof(T)` | `CSize` | Size of type `T` in bytes |
| `alignof(T)` | `CSize` | Alignment requirement of type `T` |
| `Byte` | type alias | Alias for `CUnsignedChar` |
| `CString` | type alias | Alias for `Ptr(CChar)` |

---

## Array

```tl
#import <Array.tl>
```

Generic dynamic array. All functions that allocate memory come in two variants: one taking an explicit `Ptr(Allocator)` argument, and one using the default managed allocator.

### The Array type

```tl
Array(T): {
    v:        Ptr(T),
    size:     Int,
    capacity: Int,
}
```

### Construction

| Function | Signature | Description |
|----------|-----------|-------------|
| `empty` | `(T: Type) -> Array(T)` | Empty array with zero capacity |
| `init` | `(val: T, count: Int) -> Array(T)` | Array of `count` elements, each set to `val` |
| `with_capacity` | `(T: Type, count: Int) -> Array(T)` | Empty array with pre-allocated capacity |
| `with_capacity_undefined` | `(T: Type, count: Int) -> Array(T)` | Array with size set to capacity; contents undefined |
| `from_ptr` | `(ptr: Ptr(T), len: Int) -> Array(T)` | Copy `len` elements from a pointer into a new array |

### Element Access

All access functions are bounds-checked and abort on out-of-bounds access.

| Function | Signature | Description |
|----------|-----------|-------------|
| `get` | `(arr: Array(T), index: Int) -> T` | Element at index |
| `front` | `(arr: Array(T)) -> T` | First element |
| `back` | `(arr: Array(T)) -> T` | Last element |

### Mutation

| Function | Signature | Description |
|----------|-----------|-------------|
| `push` | `(self: Ptr(Array(T)), x: T) -> Void` | Append element, growing if needed |
| `pop` | `(self: Ptr(Array(T))) -> T` | Remove and return last element |
| `insert` | `(self: Ptr(Array(T)), index: Int, val: T) -> Void` | Insert at index, shifting elements right |
| `remove` | `(self: Ptr(Array(T)), index: Int) -> T` | Remove at index, shifting elements left; returns removed element |
| `swap_remove` | `(self: Ptr(Array(T)), index: Int) -> T` | Remove at index by swapping with last element (O(1)); returns removed element |
| `clear` | `(self: Ptr(Array(T))) -> Void` | Set size to zero (does not deallocate) |

### Sizing

| Function | Signature | Description |
|----------|-----------|-------------|
| `reserve` | `(self: Ptr(Array(T)), count: Int) -> Void` | Ensure capacity for at least `count` elements |
| `resize` | `(self: Ptr(Array(T)), count: Int, val: T) -> Void` | Grow or shrink; new elements initialized to `val` |
| `shrink_to_fit` | `(self: Ptr(Array(T))) -> Void` | Reduce capacity to match size |
| `truncate` | `(self: Ptr(Array(T)), count: Int) -> Void` | Reduce size to at most `count` |
| `free` | `(self: Ptr(Array)) -> Void` | Deallocate the array buffer |

### Bulk Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `append` | `(self: Ptr(Array(T)), other: Array(T)) -> Void` | Append all elements from `other` |
| `clone` | `(arr: Array(T)) -> Array(T)` | Shallow copy of the array |
| `slice` | `(arr: Array(T), start: Int, end: Int) -> Array(T)` | Copy of elements in range `[start, end)` |

### Search / Query

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_empty` | `(arr: Array(T)) -> Bool` | True if size is zero |
| `contains` | `(arr: Array(T), val: T) -> Bool` | True if `val` is in the array (uses `==`) |
| `index_of` | `(arr: Array(T), val: T) -> Int` | Index of first occurrence, or `-1` |

### Sorting

| Function | Signature | Description |
|----------|-----------|-------------|
| `sort` | `(self: Ptr(Array(T)), cmp: (T, T) -> Int) -> Void` | In-place stable merge sort |
| `sorted` | `(arr: Array(T), cmp: (T, T) -> Int) -> Array(T)` | Return a sorted copy |

The `cmp` function should return negative if `a < b`, zero if `a == b`, positive if `a > b`.

### Reordering

| Function | Signature | Description |
|----------|-----------|-------------|
| `reverse` | `(self: Ptr(Array(T))) -> Void` | Reverse elements in place |
| `swap` | `(self: Ptr(Array(T)), i: Int, j: Int) -> Void` | Swap elements at indices `i` and `j` |

### Functional Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `map` | `(arr: Array(T), f: (T) -> U) -> Array(U)` | Apply `f` to each element, return new array |
| `filter` | `(arr: Array(T), f: (T) -> Bool) -> Array(T)` | Elements for which `f` returns true |
| `reduce` | `(arr: Array(T), init: U, f: (U, T) -> U) -> U` | Left fold |
| `foreach` | `(arr: Array(T), f: (T) -> Void) -> Void` | Apply `f` to each element for side effects |
| `any_of` | `(arr: Array(T), f: (T) -> Bool) -> Bool` | True if `f` returns true for any element |
| `all` | `(arr: Array(T), f: (T) -> Bool) -> Bool` | True if `f` returns true for all elements |
| `find` | `(arr: Array(T), f: (T) -> Bool) -> Int` | Index of first match, or `-1` |
| `find_value` | `(arr: Array(T), f: (T) -> Bool) -> T` | First matching element (aborts if none) |

### Iterators

Arrays support `for` loop iteration via the iterator protocol:

```tl
arr := Array.init(0, 10)

// Iterate by value
for x in arr { c_printf("%d\n", x) }

// Iterate by pointer (allows mutation)
for p.& in arr { p.* = p.* + 1 }
```

The `Array.Indexed` sub-module provides indexed iteration, yielding both the element and its index:

```tl
// By value: it.value and it.index
for it in Array.Indexed arr {
    c_printf("arr[%d] = %d\n", it.index, it.value)
}

// By pointer: it.ptr and it.index
for it.& in Array.Indexed arr {
    it.ptr.* = it.index
}
```

### Example

```tl
#import <Array.tl>

main() -> Int {
    arr := Array.empty(Int)
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)

    Array.sort(arr.&, (a, b) { a - b })

    sum := Array.reduce(arr, 0, (acc, x) { acc + x })
    c_printf("sum = %d\n", sum)  // sum = 6
    0
}
```

---

## Alloc

```tl
#import <Alloc.tl>
```

Memory allocation framework with pluggable allocators and three allocation contexts.

### The Allocator Interface

```tl
Allocator: {
    malloc:  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any),
    calloc:  (a: Ptr(Allocator), num: CSize, sz: CSize)  -> Ptr(any),
    realloc: (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any),
    free:    (a: Ptr(Allocator), p: Ptr(any))            -> Void,
}
```

Any struct conforming to this layout can serve as an allocator.

### Allocation Contexts

The `Alloc.context` global holds three allocator pointers:

| Context | Purpose | Default Implementation |
|---------|---------|----------------------|
| `transient` | Short-lived scratch allocations (e.g. sort temporaries) | Bump allocator (4 KB default) |
| `managed` | Long-lived allocations freed individually | C `malloc`/`free` |
| `default` | Pointer to the allocator to be used by default in standard library | either |

Array functions that omit the allocator argument use `context.default` by default.

Override default bump allocator sizes by defining `TL_ALLOC_TRANSIENT_SIZE` via `-D` in CFLAGS before compilation.

### Bump Allocator

```tl
bump_create  (alloc: Ptr(Allocator), size: CSize) -> Ptr(Allocator)
bump_destroy (alloc: Ptr(Allocator)) -> Void
```

Creates a bump (arena) allocator backed by a parent allocator. Buckets grow geometrically. Freeing only reclaims the most recently allocated block.

### Utility Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `align` | `(n: CSize, alignment: CSize) -> CSize` | Round `n` up to `alignment` boundary |
| `align_max` | `(n: CSize) -> CSize` | Round `n` up to `max_align_t` alignment |
| `next_power_of_two` | `(n: CSize) -> CSize` | Next power of two >= `n` |

### Convenience Functions

Each context has direct wrappers (e.g. `Alloc.managed_alloc`, `Alloc.transient_alloc`):

```tl
managed_alloc   (sz: CSize)              -> Ptr(any)
managed_clear   (num: CSize, sz: CSize)  -> Ptr(any)
managed_realloc (p: Ptr(any), sz: CSize) -> Ptr(any)
managed_free    (p: Ptr(any))            -> Void
```

The same pattern exists for `transient_*`.

---

## Unsafe

```tl
#import <Unsafe.tl>
```

Low-level pointer operations that bypass the type system. Use with care.

| Function | Signature | Description |
|----------|-----------|-------------|
| `pointer_add` | `(ptr: Ptr(any), n: CPtrDiff) -> Ptr(any)` | Advance pointer by `n` bytes |
| `pointer_subtract` | `(ptr: Ptr(any), n: CPtrDiff) -> Ptr(any)` | Move pointer back by `n` bytes |
| `pointer_difference` | `(lhs: Ptr(any), rhs: Ptr(any)) -> CPtrDiff` | Byte distance between two pointers |
| `pointer_compare` | `(lhs: Ptr(any), rhs: Ptr(any)) -> CInt` | Returns -1, 0, or 1 |
| `float_to_int` | `(d: CDouble) -> CLongLong` | Truncating cast from float to integer |

---

## C Library Bindings

Thin wrappers around C standard library functions. All functions use the `c_` prefix.

### stdlib

```tl
#import <stdlib.tl>
```

| Function | C Equivalent | Description |
|----------|-------------|-------------|
| `c_malloc(size)` | `malloc` | Allocate memory |
| `c_calloc(num, size)` | `calloc` | Allocate zeroed memory |
| `c_realloc(ptr, size)` | `realloc` | Resize allocation |
| `c_free(ptr)` | `free` | Free allocation |
| `c_aligned_alloc(align, size)` | `aligned_alloc` | Aligned allocation |
| `c_exit(status)` | `exit` | Terminate program |
| `c_abort()` | `abort` | Abnormal termination |
| `c_atoi(str)` | `atoi` | String to int |
| `c_atof(str)` | `atof` | String to float |
| `c_atol(str)` | `atol` | String to long |
| `c_atoll(str)` | `atoll` | String to long long |
| `c_strtod(str, endptr)` | `strtod` | String to double |
| `c_strtof(str, endptr)` | `strtof` | String to float |
| `c_strtol(str, endptr, base)` | `strtol` | String to long |
| `c_strtoll(str, endptr, base)` | `strtoll` | String to long long |
| `c_strtoul(str, endptr, base)` | `strtoul` | String to unsigned long |
| `c_strtoull(str, endptr, base)` | `strtoull` | String to unsigned long long |
| `c_getenv(name)` | `getenv` | Get environment variable |
| `c_system(cmd)` | `system` | Execute shell command |
| `c_rand()` | `rand` | Random integer |
| `c_srand(seed)` | `srand` | Seed random generator |
| `c_abs(n)` | `abs` | Absolute value (int) |
| `c_labs(n)` | `labs` | Absolute value (long) |
| `c_llabs(n)` | `llabs` | Absolute value (long long) |
| `c_div(n, d)` | `div` | Integer division with remainder |
| `c_ldiv(n, d)` | `ldiv` | Long division with remainder |
| `c_lldiv(n, d)` | `lldiv` | Long long division with remainder |
| `c_bsearch(...)` | `bsearch` | Binary search |
| `c_qsort(...)` | `qsort` | Quick sort |
| `c_atexit(f)` | `atexit` | Register exit handler |
| `c__Exit(status)` | `_Exit` | Immediate termination |
| `c_assert(cond)` | `assert` | Assertion (from `assert.h`) |

### stdio

```tl
#import <stdio.tl>
```

| Function | C Equivalent | Description |
|----------|-------------|-------------|
| `c_printf(fmt, ...)` | `printf` | Formatted output to stdout |

### string

```tl
#import <string.tl>
```

| Function | C Equivalent | Description |
|----------|-------------|-------------|
| `c_memcpy(dst, src, n)` | `memcpy` | Copy memory |
| `c_memmove(dst, src, n)` | `memmove` | Copy overlapping memory |
| `c_memcmp(p1, p2)` | `memcmp` | Compare memory |
| `c_memchr(p, c, n)` | `memchr` | Find byte in memory |
| `c_memset(s, c, n)` | `memset` | Fill memory |
| `c_strcpy(dst, src)` | `strcpy` | Copy string |
| `c_strncpy(dst, src, n)` | `strncpy` | Copy string (bounded) |
| `c_strcat(s1, s2)` | `strcat` | Concatenate strings |
| `c_strncat(s1, s2, n)` | `strncat` | Concatenate strings (bounded) |
| `c_strcmp(s1, s2)` | `strcmp` | Compare strings |
| `c_strncmp(s1, s2, n)` | `strncmp` | Compare strings (bounded) |
| `c_strcoll(s1, s2)` | `strcoll` | Locale-aware string compare |
| `c_strchr(s, c)` | `strchr` | Find character in string |
| `c_strrchr(s, c)` | `strrchr` | Find last occurrence of character |
| `c_strstr(s1, s2)` | `strstr` | Find substring |
| `c_strcspn(s1, s2)` | `strcspn` | Length of prefix not matching |
| `c_strspn(s1, s2)` | `strspn` | Length of prefix matching |
| `c_strpbrk(s1, s2)` | `strpbrk` | Find first matching character |
| `c_strtok(s1, s2)` | `strtok` | Tokenize string |
| `c_strerror(errnum)` | `strerror` | Error string |
| `c_strlen(s)` | `strlen` | String length |

### stdint

```tl
#import <stdint.tl>
```

Provides access to C fixed-width integer types and limits from `<stdint.h>` and `<limits.h>`. No functions are defined; the module makes the C types available to the Tess type system.
