# Str API: Gap Analysis and Roadmap

## Current API Summary

The `Str` type is a 16-byte SSO (Small String Optimization) union:
- Strings ≤14 bytes stored inline (no heap allocation)
- Larger strings use a heap-allocated buffer with explicit length
- Binary-safe (embedded nulls supported via explicit length tracking)
- Allocator-aware: every allocating function has both explicit-allocator and default-allocator overloads

### Existing Functions

| Category       | Functions                                                        |
|----------------|------------------------------------------------------------------|
| Construction   | `empty`, `from_cstr`, `from_bytes`, `copy`, `from_int`, `from_float` |
| Queries        | `len`, `is_empty`, `byte_at`, `cstr`                            |
| Comparison     | `eq`, `cmp`, `starts_with`, `ends_with`, `contains`, `contains_char` |
| Concatenation  | `cat`, `cat_3`, `cat_4`, `push`                                 |
| Slicing        | `slice`                                                          |
| Search         | `index_of_char`                                                  |
| Mutation       | `replace_byte`                                                   |
| Memory         | `free`                                                           |

## Missing Functionality

### Priority 1: Core text processing

These are used constantly in real programs. Without them, users must drop to raw C or write manual byte-scanning loops.

**`index_of(s, needle) -> Int`** — Substring search returning position (-1 if not found). The existing `contains` already does the linear scan; this returns the position instead of a bool. One of the most commonly called string functions in any language.

**`split(s, delimiter) -> Array[Str]`** — Split string by delimiter. Nearly every text-processing program needs this (parsing paths, config values, CSV, command args). Depends on `index_of`.

**`trim(s) -> Str` / `trim_left(s) -> Str` / `trim_right(s) -> Str`** — Strip leading/trailing whitespace. Essential for any input processing: reading lines from files, user input, etc.

**`replace(s, old, new) -> Str`** — Substring replacement. `replace_byte` covers single-byte substitution but replacing `"foo"` with `"bar"` is far more common. Depends on `index_of`.

**`to_upper(s) -> Str` / `to_lower(s) -> Str`** — ASCII case conversion. Used for case-insensitive comparison, normalizing identifiers, formatting output.

### Priority 2: Commonly needed utilities

**`index_of_char_from(s, b, start) -> Int`** — Byte search starting from an offset. Needed to iterate through all occurrences without re-scanning from the beginning.

**`last_index_of_char(s, b) -> Int` / `last_index_of(s, needle) -> Int`** — Reverse search. Common for finding file extensions (`last_index_of_char(path, '.')`) or directory separators.

**`repeat(s, count) -> Str`** — Repeat a string N times. Useful for formatting/padding. Every major language has this.

**`join(sep, arr) -> Str`** — Join an `Array[Str]` with a separator. The natural complement to `split`. Building comma-separated lists, paths, etc.

**`from_byte(b: Byte) -> Str`** — Single byte to string.

**`eq_cstr(s: Str, cs: CString) -> Bool`** — Direct comparison with a C string without constructing a temporary `Str`.

### Priority 3: Nice to have

**`count(s, needle) -> Int` / `count_byte(s, b) -> Int`** — Count occurrences.

**`strip_prefix(s, prefix) -> Str` / `strip_suffix(s, suffix) -> Str`** — Remove a known prefix/suffix if present. Currently requires `starts_with` + `slice` + `len`.

**Iterator support** — `for b in str { ... }` byte-level iteration. Currently the only access is `byte_at(s, i)` in a manual while loop.

**`from_bool(b: Bool) -> Str`** — Converts `Bool` to `"true"` / `"false"`.

## Design Notes

### StringBuilder pattern

The `cat_3`/`cat_4` naming works around the lack of variadic functions but doesn't scale. `push` exists but potentially reallocates on every call. A `StringBuilder` backed by `Array[Byte]` that converts to `Str` at the end would handle the common case of building strings from many pieces more efficiently.

### Search algorithm

`contains` currently uses O(nm) brute-force search. Fine for small strings. If substring search becomes performance-critical, Two-Way (what glibc's `strstr` uses) or Boyer-Moore would be better. Worth considering when implementing `index_of`.

### `cstr` mutation semantics

`cstr` writes the null terminator lazily and requires `Ptr[Str]` not `Str`. This is a smart optimization but may surprise users. Should be documented clearly.

## Suggested Implementation Order

1. `index_of` — foundation for `split`, `replace`, and general text processing
2. `trim` / `trim_left` / `trim_right` — unblocks input processing
3. `split` — depends on `index_of`
4. `replace` — depends on `index_of`
5. `to_upper` / `to_lower` — standalone, no dependencies
6. `join` — complement to `split`
7. `last_index_of_char` / `last_index_of` — standalone
8. Remaining priority 2 and 3 items as needed
