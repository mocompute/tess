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
| `sizeof[T]()` | `CSize` | Size of type `T` in bytes |
| `sizeof(x)` | `CSize` | Size of value `x` in bytes |
| `alignof[T]()` | `CSize` | Alignment requirement of type `T` |
| `alignof(x)` | `CSize` | Alignment requirement of value `x` |
| `Byte` | type alias | Alias for `CUnsignedChar` |
| `CString` | type alias | Alias for `Ptr[CChar]` |

---

## Array

```tl
#import <Array.tl>
```

Generic dynamic array. All functions that allocate memory come in two variants: one taking an explicit `Ptr[Allocator]` argument, and one using the default managed allocator.

### The Array type

```tl
Array[T] : {
    v:        Ptr[T],
    size:     CSize,
    capacity: CSise,
}
```

### Construction

| Function | Signature | Description |
|----------|-----------|-------------|
| `empty` | `[T]() -> Array[T]` | Empty array with zero capacity |
| `init` | `(val: T, count: CSise) -> Array[T]` | Array of `count` elements, each set to `val` |
| `with_capacity` | `[T](count: CSize) -> Array[T]` | Empty array with pre-allocated capacity |
| `with_capacity_undefined` | `[T](count: CSize) -> Array[T]` | Array with size set to capacity; contents undefined |
| `from_ptr` | `(ptr: Ptr[T], len: CSize) -> Array[T]` | Copy `len` elements from a pointer into a new array |

### Element Access

All access functions are bounds-checked and abort on out-of-bounds access.

| Function | Signature | Description |
|----------|-----------|-------------|
| `get` | `(arr: Array[T], index: CSize) -> T` | Element at index |
| `front` | `(arr: Array[T]) -> T` | First element |
| `back` | `(arr: Array[T]) -> T` | Last element |

### Mutation

| Function | Signature | Description |
|----------|-----------|-------------|
| `push` | `(self: Ptr[Array[T]], x: T) -> Void` | Append element, growing if needed |
| `pop` | `(self: Ptr[Array[T]]) -> T` | Remove and return last element |
| `insert` | `(self: Ptr[Array[T]], index: CSize, val: T) -> Void` | Insert at index, shifting elements right |
| `remove` | `(self: Ptr[Array[T]], index: CSize) -> T` | Remove at index, shifting elements left; returns removed element |
| `swap_remove` | `(self: Ptr[Array[T]], index: CSize) -> T` | Remove at index by swapping with last element (O(1)); returns removed element |
| `clear` | `(self: Ptr[Array[T]]) -> Void` | Set size to zero (does not deallocate) |

### Sizing

| Function | Signature | Description |
|----------|-----------|-------------|
| `reserve` | `(self: Ptr[Array[T]], count: CSize) -> Void` | Ensure capacity for at least `count` elements |
| `resize` | `(self: Ptr[Array[T]], count: CSize, val: T) -> Void` | Grow or shrink; new elements initialized to `val` |
| `shrink_to_fit` | `(self: Ptr[Array[T]]) -> Void` | Reduce capacity to match size |
| `truncate` | `(self: Ptr[Array[T]], count: CSize) -> Void` | Reduce size to at most `count` |
| `free` | `(self: Ptr[Array[T]]) -> Void` | Deallocate the array buffer |

### Bulk Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `append` | `(self: Ptr[Array[T]], other: Array[T]) -> Void` | Append all elements from `other` |
| `clone` | `(arr: Array[T]) -> Array[T]` | Shallow copy of the array |
| `slice` | `(arr: Array[T], start: CSize, end: CSize) -> Array[T]` | Copy of elements in range `[start, end)` |

### Search / Query

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_empty` | `(arr: Array[T]) -> Bool` | True if size is zero |
| `contains` | `(arr: Array[T], val: T) -> Bool` | True if `val` is in the array (uses `==`) |
| `index_of` | `(arr: Array[T], val: T) -> CSize` | Index of first occurrence, or `-1` |

### Sorting

| Function | Signature | Description |
|----------|-----------|-------------|
| `sort` | `(self: Ptr[Array[T]], cmp: (T, T) -> Int) -> Void` | In-place stable merge sort |
| `sorted` | `(arr: Array[T], cmp: (T, T) -> Int) -> Array[T]` | Return a sorted copy |

The `cmp` function should return negative if `a < b`, zero if `a == b`, positive if `a > b`.

### Reordering

| Function | Signature | Description |
|----------|-----------|-------------|
| `reverse` | `(self: Ptr[Array[T]]) -> Void` | Reverse elements in place |
| `swap` | `(self: Ptr[Array[T]], i: CSize, j: CSize) -> Void` | Swap elements at indices `i` and `j` |

### Functional Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `map` | `(arr: Array[T], f: (T) -> U) -> Array[U]` | Apply `f` to each element, return new array |
| `filter` | `(arr: Array[T], f: (T) -> Bool) -> Array[T]` | Elements for which `f` returns true |
| `reduce` | `(arr: Array[T], init: U, f: (U, T) -> U) -> U` | Left fold |
| `foreach` | `(arr: Array[T], f: (T) -> Void) -> Void` | Apply `f` to each element for side effects |
| `any_of` | `(arr: Array[T], f: (T) -> Bool) -> Bool` | True if `f` returns true for any element |
| `all` | `(arr: Array[T], f: (T) -> Bool) -> Bool` | True if `f` returns true for all elements |
| `find` | `(arr: Array[T], f: (T) -> Bool) -> CSize` | Index of first match, or `-1` |

### Iterators

Arrays support `for` loop iteration via the iterator protocol:

```tl
arr := Array.init(0, 10)

// Iterate by value
for x in arr { c_printf(c"%d\n", x) }

// Iterate by pointer (allows mutation)
for p.& in arr { p.* = p.* + 1 }
```

The `Array.Indexed` sub-module provides indexed iteration, yielding both the element and its index:

```tl
// By value: it.value and it.index
for it in Array.Indexed arr {
    c_printf(c"arr[%d] = %d\n", it.index, it.value)
}

// By pointer: it.ptr and it.index
for it.& in Array.Indexed arr {
    it.ptr.* = it.index
}
```

### Example

```tl
#import <Array.tl>

main() {
    arr := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)

    Array.sort(arr.&, (a, b) { a - b })

    sum := Array.reduce(arr, 0, (acc, x) { acc + x })
    c_printf(c"sum = %d\n", sum)  // sum = 6
    0
}
```

---

## String

The `String` type is always available — no import needed.

`String` is a 16-byte value type with **small string optimization (SSO)**. Strings of 14 bytes or fewer are stored inline (no heap allocation). Longer strings store a length and a pointer to a heap-allocated buffer.

### Value Semantics and Aliasing

Binding with `:=` copies the 16-byte struct, **not** the underlying heap buffer. For small strings (≤14 bytes) this is safe because the data is inline. For big strings (>14 bytes) both copies share the same heap buffer:

```tl
a := String.from_cstr("a long string that exceeds 14 bytes")
b := a            // b and a share the same heap buffer
String.free(a.&)  // frees the buffer — b is now dangling
```

Use `String.copy` to create an independent deep copy:

```tl
b := String.copy(a) // b has its own buffer — safe to free independently
```

The same aliasing applies to `Array` and any struct containing pointers. See [Value Semantics](LANGUAGE_REFERENCE.md#value-semantics) in the Language Reference.

### The `cstr` function

`String.cstr` takes `Ptr[String]` (not `String` by value) because it may write a null terminator into the buffer:

```tl
s := String.from_cstr("hello")
c_printf(c"%s\n", String.cstr(s.&))
```

### Allocator-aware overloads

Most functions that allocate memory come in two variants: one taking an explicit `Ptr[Allocator]` argument, and one using the default allocator (`Alloc.context.default`). For example:

```tl
String.copy(alloc, s)   // explicit allocator
String.copy(s)          // uses default allocator
```

This pattern applies to `from_cstr`, `from_bytes`, `from_int`, `from_float`, `copy`, `cat`, `slice`, `trim`, `replace`, `split`, `join`, `free`, and others.

### API overview

See the synopsis at the top of `src/tl/std/String.tl` (lines 40–130) for the full function listing, organized by category: construction, queries, comparison, concatenation, slicing, search, transformation, split/join, memory, and iteration.

---

## Alloc

```tl
#import <Alloc.tl>
```

Memory allocation framework with pluggable allocators and three allocation contexts.

### The Allocator Interface

```tl
Allocator : {
    malloc:  (a: Ptr[Allocator], sz: CSize)              -> Ptr[any],
    calloc:  (a: Ptr[Allocator], num: CSize, sz: CSize)  -> Ptr[any],
    realloc: (a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any],
    free:    (a: Ptr[Allocator], p: Ptr[any])            -> Void,
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

### Bump Allocator (`Alloc.BumpAllocator`)

```tl
create  (alloc: Ptr[Allocator], size: CSize) -> Ptr[Allocator]
destroy (alloc: Ptr[Allocator]) -> Void
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
managed_alloc   (sz: CSize)              -> Ptr[any]
managed_clear   (num: CSize, sz: CSize)  -> Ptr[any]
managed_realloc (p: Ptr[any], sz: CSize) -> Ptr[any]
managed_free    (p: Ptr[any])            -> Void
```

The same pattern exists for `transient_*`.

---

## Hash

```tl
#import <Hash.tl>
```

Hashing functions using the FNV-1a algorithm.

| Function | Signature | Description |
|----------|-----------|-------------|
| `fnv1a` | `(p: Ptr[any], n: CSize) -> CSize` | FNV-1a hash of `n` bytes starting at `p` |
| `fnv1a_cstring` | `(s: CString) -> CSize` | FNV-1a hash of a null-terminated C string |

### Example

```tl
#import <Hash.tl>

main() {
    s := "hello"
    h := Hash.fnv1a_cstring(s)
    c_printf(c"hash = %zu\n", h)
    0
}
```

---

## Unsafe

```tl
#import <Unsafe.tl>
```

Low-level pointer operations and numeric conversions that bypass the type system. Use with care.

| Function | Signature | Description |
|----------|-----------|-------------|
| `pointer_add` | `(ptr: Ptr[any], n: UInt) -> Ptr[any]` | Advance pointer by `n` bytes |
| `pointer_subtract` | `(ptr: Ptr[any], n: UInt) -> Ptr[any]` | Move pointer back by `n` bytes |
| `pointer_difference` | `(lhs: Ptr[any], rhs: Ptr[any]) -> CPtrDiff` | Byte distance between two pointers |
| `pointer_compare` | `(lhs: Ptr[any], rhs: Ptr[any]) -> CInt` | Returns -1, 0, or 1 |
| `float_to_int` | `(d: CDouble) -> CLongLong` | Truncating cast from float to integer |
| `int_to_float` | `(i: CLongLong) -> CDouble` | Cast from integer to float |

**Note:** Integer signedness conversions (signed ↔ unsigned) no longer require `Unsafe` functions. Use a declaration type annotation instead: `unsigned: UInt := signed_value`. See [Integer Type Conversions](LANGUAGE_REFERENCE.md#integer-type-conversions).

---

## C Library Bindings

Thin wrappers around C standard library functions. All functions use the `c_` prefix.

### stdlib

```tl
#import <cstdlib.tl>
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
#import <cstdio.tl>
```

| Function | C Equivalent | Description |
|----------|-------------|-------------|
| `c_printf(fmt, ...)` | `printf` | Formatted output to stdout |

### string

```tl
#import <cstring.tl>
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
#import <cstdint.tl>
```

Provides access to C fixed-width integer types and limits from `<stdint.h>` and `<limits.h>`. No functions are defined; the module makes the C types available to the Tess type system.

---

## File

```tl
#import <File.tl>
```

Streaming file I/O, convenience read/write, and path utilities. Wraps C stdio for cross-platform file access. All operations use binary mode to avoid Windows CR/LF translation. Error handling via `Result[T, IOError]`.

### Types

```tl
Mode: | Read          // "rb"
      | Write         // "wb"
      | Append        // "ab"
      | ReadWrite     // "r+b"
      | WriteRead     // "w+b"
      | AppendRead    // "a+b"

IOError: | NotFound         { path: String }
         | PermissionDenied { path: String }
         | AlreadyExists    { path: String }
         | IOFailed         { path: String, message: String, errno_code: CInt }

SeekFrom: | Start | Current | End

File: {
    _handle: Ptr[any],
    _alloc:  Ptr[Allocator],
    _path:   String,
}
```

### Handle Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `open` | `(path: String, mode: Mode) -> Result[Ptr[File], IOError]` | Open a file; returns a heap-allocated handle |
| `close` | `(self: Ptr[File]) -> Void` | Close the file and free the handle |

### Streaming Read

| Function | Signature | Description |
|----------|-----------|-------------|
| `read` | `(self: Ptr[File], buf: Ptr[Byte], count: CSize) -> Result[CSize, IOError]` | Read up to `count` bytes into `buf` |
| `read_line` | `(self: Ptr[File]) -> Result[Option[String], IOError]` | Read one line (strips `\r\n`); `None` at EOF |
| `read_all` | `(self: Ptr[File]) -> Result[String, IOError]` | Read remaining contents as a string |
| `read_bytes` | `(self: Ptr[File]) -> Result[Array[Byte], IOError]` | Read remaining contents as a byte array |

### Streaming Write

| Function | Signature | Description |
|----------|-----------|-------------|
| `write` | `(self: Ptr[File], buf: Ptr[Const[Byte]], count: CSize) -> Result[CSize, IOError]` | Write `count` bytes from `buf` |
| `write_str` | `(self: Ptr[File], data: String) -> Result[Void, IOError]` | Write a string |
| `write_bytes` | `(self: Ptr[File], data: Array[Byte]) -> Result[Void, IOError]` | Write a byte array |

### Positioning

| Function | Signature | Description |
|----------|-----------|-------------|
| `seek` | `(self: Ptr[File], offset: Int, from: SeekFrom) -> Result[Void, IOError]` | Seek to position |
| `tell` | `(self: Ptr[File]) -> Result[Int, IOError]` | Current position in file |
| `rewind` | `(self: Ptr[File]) -> Void` | Seek back to start |

### Query

| Function | Signature | Description |
|----------|-----------|-------------|
| `path` | `(self: Ptr[Const[File]]) -> String` | Path the file was opened with |
| `is_eof` | `(self: Ptr[Const[File]]) -> Bool` | True if at end of file |

### Convenience (no handle needed)

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `(path: String) -> Result[String, IOError]` | Read entire file as a string |
| `read_file_bytes` | `(path: String) -> Result[Array[Byte], IOError]` | Read entire file as bytes |
| `write_file` | `(path: String, content: String) -> Result[Void, IOError]` | Write string to file (creates/truncates) |
| `write_file_bytes` | `(path: String, data: Array[Byte]) -> Result[Void, IOError]` | Write bytes to file (creates/truncates) |
| `append_file` | `(path: String, content: String) -> Result[Void, IOError]` | Append string to file |
| `read_lines` | `(path: String) -> Result[Array[String], IOError]` | Read file as array of lines |

### File Metadata

| Function | Signature | Description |
|----------|-----------|-------------|
| `exists` | `(path: String) -> Bool` | True if path exists |
| `is_directory` | `(path: String) -> Bool` | True if path is a directory |
| `size` | `(path: String) -> Result[Int, IOError]` | File size in bytes |
| `temp_dir` | `() -> String` | Platform temp directory (with trailing separator) |

### Directory Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `create_dir` | `(path: String) -> Result[Void, IOError]` | Create a directory |
| `scan_dir` | `(path: String, ext: Option[String]) -> Result[Array[String], IOError]` | Recursively list files; optionally filter by extension |

### Allocator-aware Overloads

Most functions that allocate memory come in two variants: one taking an explicit `Ptr[Allocator]` argument, and one using the default allocator. This applies to `open`, `read_line`, `read_all`, `read_bytes`, `read_file`, `read_file_bytes`, `read_lines`, `scan_dir`, and the `File.Path` functions below.

### Example

```tl
#import <File.tl>

main() {
    content := try File.read_file(String.from_cstr("data.txt"))
    try File.write_file(String.from_cstr("out.txt"), content)
    0
}
```

---

## File.Path

`File.Path` is a submodule of `File` providing pure path string manipulation (no filesystem access).

```tl
#import <File.tl>
```

### Components

| Function | Signature | Description |
|----------|-----------|-------------|
| `dirname` | `(path: String) -> String` | Directory portion of path |
| `basename` | `(path: String) -> String` | Final component of path |
| `extension` | `(path: String) -> Option[String]` | File extension including dot, or `None` |
| `stem` | `(path: String) -> String` | Basename without extension |

### Manipulation

| Function | Signature | Description |
|----------|-----------|-------------|
| `join` | `(dir: String, file: String) -> String` | Join directory and filename with separator |
| `normalize` | `(path: String) -> String` | Resolve `.`, `..`, collapse separators |
| `relative` | `(from_dir: String, to_path: String) -> Result[String, File.IOError]` | Compute relative path between two paths |

### Queries

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_absolute` | `(path: String) -> Bool` | True if path is absolute |
| `is_relative` | `(path: String) -> Bool` | True if path is relative |
| `separator` | `() -> CChar` | Platform path separator (`/` or `\\`) |

### Example

```tl
#import <File.tl>

main() {
    path := String.from_cstr("/home/user/docs/file.txt")
    dir  := File.Path.dirname(path)       // "/home/user/docs"
    base := File.Path.basename(path)      // "file.txt"
    ext  := File.Path.extension(path)     // Some(".txt")
    stem := File.Path.stem(path)          // "file"

    joined := File.Path.join(dir, String.from_cstr("other.txt"))
    // "/home/user/docs/other.txt"
    0
}
```
