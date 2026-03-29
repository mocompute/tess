# Receiver Blocks

> **Status:** Proposal

## Problem

Tess modules follow a "one module, one type" convention where most functions share a
common first parameter -- the receiver. In practice, the same type expression is repeated
dozens of times per file.

In `String.tl`, `Ptr[Const[String]]` appears 40+ times in the synopsis:

```tl
len        (s: Ptr[Const[String]])                                 -> CSize
is_empty   (s: Ptr[Const[String]])                                 -> Bool
byte_at    (s: Ptr[Const[String]], index: CSize)                   -> Option[Byte]
starts_with(s: Ptr[Const[String]], prefix: Ptr[Const[String]])     -> Bool
ends_with  (s: Ptr[Const[String]], suffix: Ptr[Const[String]])     -> Bool
contains   (haystack: Ptr[Const[String]], needle: Ptr[Const[String]]) -> Bool
// ... 35+ more
```

`Array.tl` repeats `[T]` and `Ptr[Array[T]]` on every generic function:

```tl
push[T]  (self: Ptr[Array[T]], alloc: Ptr[Allocator], x: T) -> Void
push[T]  (self: Ptr[Array[T]], x: T)                         -> Void
pop[T]   (self: Ptr[Array[T]])                                -> T
clear[T] (self: Ptr[Array[T]])                                -> Void
```

`HashMap.tl` repeats both `[K, V]` and `Ptr[HashMap[K, V]]`, plus trait constraints:

```tl
set[K: HashEq, V]     (self: Ptr[HashMap[K, V]], key: K, value: V) -> Void
get[K: HashEq, V]     (self: Ptr[HashMap[K, V]], key: K)           -> Ptr[V]
get_copy[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Option[V]
contains[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Bool
```

The repetition obscures the meaningful differences between function signatures and makes
synopses hard to scan.

## Design

A **receiver block** factors shared parameters out of a group of function declarations
and/or definitions:

```
name: Type : {
    func(remaining_params...) -> ReturnType
    func(remaining_params...) -> ReturnType { body }
    ...
}
```

Multiple parameters can be factored out by separating them with commas:

```
name1: Type1, name2: Type2 : {
    func(remaining_params...) -> ReturnType
    ...
}
```

Each entry inside the braces is desugared into a normal top-level function with the
block's parameters prepended. This is **purely a parser transformation** -- no new
semantics, no runtime overhead, no change to the type system or calling convention.

### Basic Example

Before:

```tl
len      (s: Ptr[Const[String]])               -> CSize
is_empty (s: Ptr[Const[String]])               -> Bool
byte_at  (s: Ptr[Const[String]], index: CSize) -> Option[Byte]
```

After:

```tl
s: Ptr[Const[String]] : {
    len()                 -> CSize
    is_empty()            -> Bool
    byte_at(index: CSize) -> Option[Byte]
}
```

Desugaring produces exactly the "before" form. The two are interchangeable.

### Syntax Rationale

The form `name: Type : { ... }` uses no keywords. It extends Tess's existing binding
syntax -- `name: Type` is already how parameter types are written. The trailing `: { ... }`
says "here are the functions that share this parameter." This preserves Tess's distinctive
property that `#hash` directives are the only top-level keywords.

### Parameter Names

The identifiers before each colon are **parameter names**, used in function bodies and
as parameter names in desugared signatures:

```tl
s: Ptr[Const[String]] : {
    len() -> CSize {
        return if _is_small(s) { _small_len(s) } else { s->big.len }
    }
}
```

There is no implicit `self` or `this`. The programmer chooses the names. Different blocks
can use different names.

### Multiple Parameters

When multiple functions share the same parameter list prefix, all shared parameters can
be factored out:

```tl
a: Ptr[Const[String]], b: Ptr[Const[String]] : {
    eq()  -> Bool
    cmp() -> CInt
}
```

Desugars to:

```tl
eq(a: Ptr[Const[String]], b: Ptr[Const[String]])  -> Bool
cmp(a: Ptr[Const[String]], b: Ptr[Const[String]]) -> CInt
```

All named parameters are available in function bodies:

```tl
a: Ptr[Const[String]], b: Ptr[Const[String]] : {
    eq() -> Bool {
        0 == cmp(a, b)
    }
}
```

The parameters need not be the same type:

```tl
dst: Ptr[String], src: Ptr[Const[String]] : {
    copy_into()                 -> Void
    copy_into_at(offset: CSize) -> Void
}
```

Functions in a multi-parameter block can still have additional parameters of their own.

### Multiple Receiver Types

A module may have multiple receiver blocks for different receiver types:

```tl
// Immutable access.
s: Ptr[Const[String]] : {
    len()      -> CSize
    is_empty() -> Bool
    hash()     -> CSize
}

// Mutation.
self: Ptr[String] : {
    cstr()                                     -> CString
    push(other: Ptr[Const[String]])            -> Void
    replace_byte_in_place(find: CChar, replace: CChar) -> Void
    free()                                     -> Void
}
```

The three common receiver types and their meanings:

| Receiver type    | Meaning                      |
|------------------|------------------------------|
| `Ptr[Const[T]]`  | Immutable (read-only) access |
| `Ptr[T]`         | Mutable access               |
| `T`              | By-value (consumes or copies) |

### Mixed Declarations and Definitions

A block can contain forward declarations, full definitions, or both:

```tl
s: Ptr[Const[String]] : {
    // Forward declaration.
    len() -> CSize

    // Full definition.
    is_empty() -> Bool {
        len(s) == 0
    }
}
```

A forward declaration in a receiver block is matched the same way as any other forward
declaration -- the implementation may appear later as a standalone function or in another
receiver block.


## Generics

### Type Parameter Inference

The parser infers type parameters from the receiver type by checking which identifiers
are known types and which are not. In `Ptr[Array[T]]`:

- `Ptr` -- known built-in type
- `Array` -- known module type
- `T` -- unknown, therefore inferred as a type parameter

The inferred type parameters are prepended to each function's type parameter list:

```tl
self: Ptr[Array[T]] : {
    push(x: T)        -> Void
    pop()              -> T
    get(index: CSize)  -> T
}
```

Desugars to:

```tl
push[T](self: Ptr[Array[T]], x: T)        -> Void
pop[T](self: Ptr[Array[T]])               -> T
get[T](self: Ptr[Array[T]], index: CSize)  -> T
```

### Multiple Type Parameters

Multiple unknown identifiers each become type parameters, in the order they first appear:

```tl
self: Ptr[HashMap[K, V]] : {
    size()     -> CSize
    is_empty() -> Bool
}
```

Desugars to:

```tl
size[K, V](self: Ptr[HashMap[K, V]])     -> CSize
is_empty[K, V](self: Ptr[HashMap[K, V]]) -> Bool
```

### Trait Constraints

Type parameters in the receiver type may carry trait constraints using the existing
`Name: Trait` constraint syntax:

```tl
self: Ptr[HashMap[K: HashEq, V]] : {
    set(key: K, value: V) -> Void
    get(key: K)           -> Ptr[V]
    contains(key: K)      -> Bool
}
```

Desugars to:

```tl
set[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K, value: V) -> Void
get[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Ptr[V]
contains[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)      -> Bool
```

The constraint is specified once on the block and threaded to every function inside.
Functions that don't need the constraint go in a separate block:

```tl
// Unconstrained.
self: Ptr[HashMap[K, V]] : {
    size()     -> CSize
    is_empty() -> Bool
    clear()    -> Void
    destroy()  -> Void
}

// Requires HashEq on K.
self: Ptr[HashMap[K: HashEq, V]] : {
    set(key: K, value: V) -> Void
    get(key: K)           -> Ptr[V]
    contains(key: K)      -> Bool
    remove(key: K)        -> Bool
}
```

### Multiple Parameters with Generics

Type parameter inference works across all block parameters. Unknown identifiers are
unified -- if the same name appears in multiple parameter types, it becomes a single
type parameter:

```tl
a: Array[T], b: Array[T] : {
    eq()    -> Bool
    concat() -> Array[T]
}
```

Desugars to:

```tl
eq[T](a: Array[T], b: Array[T])     -> Bool
concat[T](a: Array[T], b: Array[T]) -> Array[T]
```

Parameters may also have different types:

```tl
self: Ptr[Array[T]], s: Slice[T] : {
    append_slice() -> Void
}
// desugars to: append_slice[T](self: Ptr[Array[T]], s: Slice[T]) -> Void
```

### Additional Function-Level Type Parameters

A function inside a receiver block can introduce additional type parameters beyond those
inferred from the receiver:

```tl
self: Ptr[Array[T]] : {
    map[U](f: fn/1(T) -> U) -> Array[U]
}
```

Desugars to:

```tl
map[T, U](self: Ptr[Array[T]], f: fn/1(T) -> U) -> Array[U]
```

Block-level type parameters come first, then function-level ones.


## Desugaring Rules

Given a receiver block:

```
name1 : Type1, name2 : Type2, ... : {
    func_name(params...) -> ReturnType
}
```

(The single-parameter form `name : Type : { ... }` is just the one-element case.)

The parser performs these steps:

1. **Discover type parameters.** Scan all block parameter types for identifiers that do
   not resolve to a known type or module. Each unknown identifier becomes a type parameter.
   If it has a constraint annotation (e.g., `K: HashEq`), the constraint is preserved.
   Duplicates across parameters are unified (e.g., if both `Array[T]` and `Slice[T]`
   appear, `T` is introduced once).

2. **For each function in the block:**
   a. Prepend all block parameters (`name1: Type1, name2: Type2, ...`) to the function's
      parameter list, in order. Constraint annotations are stripped from the type
      expressions (constraints only appear in the type parameter list).
   b. Merge type parameters: block-level params first, then any function-level params.
      If a function redeclares a block-level param with a tighter constraint, the
      function's constraint wins.
   c. The result is a normal top-level function declaration or definition.

3. **Emit the desugared functions** into the module as if they had been written directly.

The rest of the compiler pipeline (type inference, specialization, transpilation) sees
only ordinary free functions.


## Parsing

### Disambiguation

The single-parameter receiver block introduces the token sequence:

```
identifier : TypeExpr : {
```

The multi-parameter form introduces:

```
identifier : TypeExpr , identifier : TypeExpr , ... : {
```

Existing top-level forms after `identifier :` are:

| After `identifier :`     | Meaning                  |
|--------------------------|--------------------------|
| `{ ... }` or `\| ...`   | Struct or tagged union    |
| `Identifier { ... }`     | Trait with parent         |
| `TypeExpr :=`            | Variable binding          |
| `TypeExpr =`             | Typed assignment          |
| **`TypeExpr : { ... }`** | **Receiver block (new)**  |
| **`TypeExpr , ...`**     | **Multi-param block (new)** |

For the single-parameter form, the second `:` before `{` is the disambiguator. No
existing form produces this token sequence.

For the multi-parameter form, the `,` after a complete type expression is the early
disambiguator -- no existing top-level form has `identifier : TypeExpr ,`. The parser
can collect comma-separated `name : Type` pairs until it sees `: {`.

Colons inside bracketed type expressions (e.g., `HashMap[K: HashEq, V]`) are consumed
by bracket parsing and do not interfere with the top-level `:` or `,` detection.

### Scope

Receiver blocks are only valid at the module top level. They cannot appear inside function
bodies, struct definitions, or other nested contexts.


## Guidelines

### Allocator Overload Pairs

The common allocator-pair pattern works naturally inside a receiver block:

```tl
s: Ptr[Const[String]] : {
    to_upper(alloc: Ptr[Allocator]) -> String
    to_upper()                      -> String
    trim(alloc: Ptr[Allocator])     -> String
    trim()                          -> String
}
```

This desugars with the receiver as the first parameter and the allocator second. Note that
some existing standard library functions use allocator-first ordering:

```tl
// Current: alloc first, receiver second
to_upper(alloc: Ptr[Allocator], s: Ptr[Const[String]]) -> String

// With receiver block: receiver first, alloc second
to_upper(s: Ptr[Const[String]], alloc: Ptr[Allocator]) -> String
```

Receiver-first ordering is arguably more consistent -- it enables UFCS for both overloads
(`s.to_upper()` and `s.to_upper(alloc)`) rather than requiring module-qualified calls for
the allocator version. Mutable methods like `push` already use receiver-first ordering.
Adopting receiver blocks would be a natural time to standardize.

### Constructors Stay Standalone

Functions that construct a value without an existing receiver don't benefit from receiver
blocks and remain as standalone free functions:

```tl
// These stay at the top level, outside any receiver block.
empty()                                                    -> String
from_cstr(alloc: Ptr[Allocator], s: Ptr[Const[CChar]])    -> String
from_cstr(s: Ptr[Const[CChar]])                            -> String
create[K, V]()                                             -> Ptr[HashMap[K, V]]
```

### `Const` with Generic Receivers

`Ptr[Const[Container[T]]]` works as a receiver type for generic modules. This allows
generic types to distinguish read-only from mutating methods, just like non-generic types:

```tl
// Read-only access to a generic array.
s: Ptr[Const[Array[T]]] : {
    is_empty() -> Bool
    get(index: CSize) -> T
}

// Mutating access.
self: Ptr[Array[T]] : {
    push(x: T) -> Void
    pop()       -> T
}
```

### Private Helpers

Private helpers (prefixed with `_`) can use receiver blocks if they share a common
receiver:

```tl
s: Ptr[Const[String]] : {
    _is_small() -> Bool
    _small_len() -> CSize
    _buf()       -> Ptr[CChar]
}
```

Whether this improves clarity depends on the module. Many private helpers have unique
signatures that don't benefit from grouping.


## What Doesn't Change

- **Call sites.** `s.len()`, `arr.push(x)` -- UFCS dispatch is unchanged.
- **Constructors.** Functions with no receiver remain standalone.
- **Existing code.** All current `.tl` files compile without modification. Standalone
  functions and receiver blocks are freely intermixable within a module.
- **C output.** The transpiled C code is identical regardless of source form.
- **Semantics.** No new calling convention, no vtables, no method resolution changes.
  Receiver blocks are syntax, not semantics.


## Full Example: String.tl Synopsis

Current (~80 lines):

```tl
len      (s: Ptr[Const[String]])               -> CSize
is_empty (s: Ptr[Const[String]])               -> Bool
byte_at  (s: Ptr[Const[String]], index: CSize) -> Option[Byte]
char_at  (s: Ptr[Const[String]], index: CSize) -> Option[CChar]
cstr     (s: Ptr[String])                      -> CString
hash     (s: Ptr[Const[String]])               -> CSize
eq       (a: Ptr[Const[String]], b: Ptr[Const[String]])                -> Bool
cmp      (a: Ptr[Const[String]], b: Ptr[Const[String]])                -> CInt
starts_with(s: Ptr[Const[String]], prefix: Ptr[Const[String]])         -> Bool
// ... and so on for ~70 more lines
```

Proposed:

```tl
// Construction (no receiver).
empty      ()                                                      -> String
from_cstr  (alloc: Ptr[Allocator], s: Ptr[Const[CChar]])           -> String
from_cstr  (s: Ptr[Const[CChar]])                                  -> String
from_bytes (alloc: Ptr[Allocator], s: Ptr[Const[CChar]], n: CSize) -> String
from_bytes (s: Ptr[Const[CChar]], n: CSize)                        -> String
copy       (alloc: Ptr[Allocator], s: Ptr[Const[String]])          -> String
copy       (s: Ptr[Const[String]])                                 -> String
from_int   (alloc: Ptr[Allocator], n: Int)                         -> String
from_int   (n: Int)                                                -> String
from_float (alloc: Ptr[Allocator], f: Float)                       -> String
from_float (f: Float)                                              -> String
from_byte  (b: Byte)                                               -> String
from_bool  (b: Bool)                                               -> String

// Comparison (symmetric).
a: Ptr[Const[String]], b: Ptr[Const[String]] : {
    eq()  -> Bool
    cmp() -> CInt
}

// Immutable operations.
s: Ptr[Const[String]] : {
    len()                                    -> CSize
    is_empty()                               -> Bool
    byte_at(index: CSize)                    -> Option[Byte]
    char_at(index: CSize)                    -> Option[CChar]
    hash()                                   -> CSize

    starts_with(prefix: Ptr[Const[String]])  -> Bool
    ends_with(suffix: Ptr[Const[String]])    -> Bool
    contains(needle: Ptr[Const[String]])     -> Bool
    contains_char(b: CChar)                  -> Bool
    eq_cstr(cs: CString)                     -> Bool

    cat(alloc: Ptr[Allocator], b: Ptr[Const[String]])                          -> String
    cat(b: Ptr[Const[String]])                                                 -> String
    cat_3(alloc: Ptr[Allocator], b: Ptr[Const[String]], c: Ptr[Const[String]]) -> String
    cat_3(b: Ptr[Const[String]], c: Ptr[Const[String]])                        -> String

    index_of_char(b: CChar)                          -> Option[CSize]
    index_of_char_from(b: CChar, start: CSize)       -> Option[CSize]
    last_index_of_char(b: CChar)                     -> Option[CSize]
    index_of(needle: Ptr[Const[String]])             -> Option[CSize]
    last_index_of(needle: Ptr[Const[String]])        -> Option[CSize]
    count_byte(b: CChar)                             -> CSize
    count(needle: Ptr[Const[String]])                -> CSize

    replace_byte(alloc: Ptr[Allocator], find: CChar, replace: CChar) -> String
    replace_byte(find: CChar, replace: CChar)                        -> String
    to_upper(alloc: Ptr[Allocator])                                  -> String
    to_upper()                                                       -> String
    to_lower(alloc: Ptr[Allocator])                                  -> String
    to_lower()                                                       -> String
    trim_left(alloc: Ptr[Allocator])                                 -> String
    trim_left()                                                      -> String
    trim_right(alloc: Ptr[Allocator])                                -> String
    trim_right()                                                     -> String
    trim(alloc: Ptr[Allocator])                                      -> String
    trim()                                                           -> String
    repeat(alloc: Ptr[Allocator], count: CSize)                      -> String
    repeat(count: CSize)                                             -> String
    replace(alloc: Ptr[Allocator], old: Ptr[Const[String]], new_s: Ptr[Const[String]]) -> String
    replace(old: Ptr[Const[String]], new_s: Ptr[Const[String]])                        -> String
    strip_prefix(alloc: Ptr[Allocator], prefix: Ptr[Const[String]]) -> String
    strip_prefix(prefix: Ptr[Const[String]])                        -> String
    strip_suffix(alloc: Ptr[Allocator], suffix: Ptr[Const[String]]) -> String
    strip_suffix(suffix: Ptr[Const[String]])                        -> String

    copy(alloc: Ptr[Allocator], start: CSize, end: CSize) -> String
    copy(start: CSize, end: CSize)                         -> String

    split(alloc: Ptr[Allocator], delimiter: Ptr[Const[String]]) -> Array[String]
    split(delimiter: Ptr[Const[String]])                        -> Array[String]
    join(alloc: Ptr[Allocator], arr: Array[String])             -> String
    join(arr: Array[String])                                    -> String
}

// Mutating operations.
self: Ptr[String] : {
    cstr()                                                  -> CString
    push(alloc: Ptr[Allocator], other: Ptr[Const[String]])  -> Void
    push(other: Ptr[Const[String]])                         -> Void
    replace_byte_in_place(find: CChar, replace: CChar)      -> Void
    to_upper_in_place()                                     -> Void
    to_lower_in_place()                                     -> Void
    slice(start: CSize, end: CSize)                         -> Slice[CChar]
    free(alloc: Ptr[Allocator])                             -> Void
    free()                                                  -> Void
}
```

## Full Example: Array.tl Synopsis

Current:

```tl
push[T]     (self: Ptr[Array[T]], alloc: Ptr[Allocator], x: T)  -> Void
push[T]     (self: Ptr[Array[T]], x: T)                          -> Void
pop[T]      (self: Ptr[Array[T]])                                -> T
clear[T]    (self: Ptr[Array[T]])                                -> Void
get[T]      (arr: Array[T], index: CSize)                        -> T
is_empty[T] (arr: Array[T])                                      -> Bool
map[T, U]   (arr: Array[T], f: (T) -> U)                        -> Array[U]
reduce[T, U](arr: Array[T], init: U, f: (U, T) -> U)            -> U
```

Proposed:

```tl
// Construction (no receiver).
empty[T]()                                          -> Array[T]
init[T](alloc: Ptr[Allocator], val: T, count: CSize) -> Array[T]
init[T](val: T, count: CSize)                        -> Array[T]
with_capacity[T](alloc: Ptr[Allocator], count: CSize) -> Array[T]
with_capacity[T](count: CSize)                        -> Array[T]
from_ptr[T](alloc: Ptr[Allocator], ptr: Ptr[T], len: CSize) -> Array[T]
from_ptr[T](ptr: Ptr[T], len: CSize)                        -> Array[T]

// Mutable operations.
self: Ptr[Array[T]] : {
    push(alloc: Ptr[Allocator], x: T) -> Void
    push(x: T)                        -> Void
    reserve(alloc: Ptr[Allocator], count: CSize) -> Void
    reserve(count: CSize)                        -> Void
    pop()                              -> T
    clear()                            -> Void
    insert(alloc: Ptr[Allocator], index: CSize, val: T) -> Void
    insert(index: CSize, val: T)                        -> Void
    remove(index: CSize)               -> T
    swap_remove(index: CSize)          -> T
    resize(alloc: Ptr[Allocator], count: CSize, val: T) -> Void
    resize(count: CSize, val: T)                        -> Void
    shrink_to_fit(alloc: Ptr[Allocator]) -> Void
    shrink_to_fit()                      -> Void
    truncate(count: CSize)               -> Void
    append(alloc: Ptr[Allocator], other: Array[T]) -> Void
    append(other: Array[T])                        -> Void
    reverse()                            -> Void
    swap(i: CSize, j: CSize)            -> Void
    sort(tmp_alloc: Ptr[Allocator], cmp: (T, T) -> Int) -> Void
    sort(cmp: (T, T) -> Int)                            -> Void
    free(alloc: Ptr[Allocator])          -> Void
    free()                               -> Void
}

// By-value operations.
arr: Array[T] : {
    get(index: CSize)              -> T
    front()                        -> T
    back()                         -> T
    is_empty()                     -> Bool
    contains(val: T)               -> Bool
    index_of(val: T)               -> Option[CSize]
    clone(alloc: Ptr[Allocator])   -> Array[T]
    clone()                        -> Array[T]
    copy(alloc: Ptr[Allocator], start: CSize, end: CSize) -> Array[T]
    copy(start: CSize, end: CSize)                        -> Array[T]
    slice(start: CSize, end: CSize) -> Slice[T]
    sorted(alloc: Ptr[Allocator], tmp_alloc: Ptr[Allocator], cmp: (T, T) -> Int) -> Array[T]
    sorted(cmp: (T, T) -> Int)                                                   -> Array[T]
    map[U](alloc: Ptr[Allocator], f: (T) -> U) -> Array[U]
    map[U](f: (T) -> U)                        -> Array[U]
    filter(alloc: Ptr[Allocator], f: (T) -> Bool) -> Array[T]
    filter(f: (T) -> Bool)                        -> Array[T]
    reduce[U](init: U, f: (U, T) -> U)            -> U
    foreach(f: (T) -> Void)                        -> Void
    any_of(f: (T) -> Bool)                         -> Bool
    all(f: (T) -> Bool)                            -> Bool
    find(f: (T) -> Bool)                           -> Option[CSize]
}
```


## Full Example: HashMap.tl Synopsis

Current:

```tl
create[K, V] ()                                         -> Ptr[HashMap[K, V]]
create[K, V] (alloc: Ptr[Allocator])                    -> Ptr[HashMap[K, V]]
create[K, V] (alloc: Ptr[Allocator], opts: HashMapOpts) -> Ptr[HashMap[K, V]]
destroy[K, V](self: Ptr[HashMap[K, V]])                 -> Void

set[K: HashEq, V]     (self: Ptr[HashMap[K, V]], key: K, value: V) -> Void
get[K: HashEq, V]     (self: Ptr[HashMap[K, V]], key: K)           -> Ptr[V]
get_copy[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Option[V]
contains[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Bool
remove[K: HashEq, V]  (self: Ptr[HashMap[K, V]], key: K)           -> Bool

size[K, V]    (self: Ptr[HashMap[K, V]]) -> CSize
is_empty[K, V](self: Ptr[HashMap[K, V]]) -> Bool
clear[K, V]   (self: Ptr[HashMap[K, V]]) -> Void

keys[K, V]  (self: Ptr[HashMap[K, V]])                        -> Array[K]
keys[K, V]  (self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[K]
values[K, V](self: Ptr[HashMap[K, V]])                        -> Array[V]
values[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[V]
```

Proposed:

```tl
// Construction.
create[K, V]()                                         -> Ptr[HashMap[K, V]]
create[K, V](alloc: Ptr[Allocator])                    -> Ptr[HashMap[K, V]]
create[K, V](alloc: Ptr[Allocator], opts: HashMapOpts) -> Ptr[HashMap[K, V]]

// Unconstrained methods.
self: Ptr[HashMap[K, V]] : {
    size()                        -> CSize
    is_empty()                    -> Bool
    clear()                       -> Void
    destroy()                     -> Void
    keys(alloc: Ptr[Allocator])   -> Array[K]
    keys()                        -> Array[K]
    values(alloc: Ptr[Allocator]) -> Array[V]
    values()                      -> Array[V]
}

// Methods requiring HashEq on K.
self: Ptr[HashMap[K: HashEq, V]] : {
    set(key: K, value: V) -> Void
    get(key: K)           -> Ptr[V]
    get_copy(key: K)      -> Option[V]
    contains(key: K)      -> Bool
    remove(key: K)        -> Bool
}
```


## Implementation Notes

### Parser Integration

The Tess parser is two-pass: pass 1 collects module symbols, pass 2 builds the AST.
Receiver block expansion must happen during or before pass 1, because the desugared
functions need to be registered as module symbols (for overload resolution, UFCS lookup,
etc.). The type parameter inference step -- determining which identifiers in the receiver
type are unknown and therefore type parameters -- depends on knowing which names are
types or modules, which is available from the source scanner and previously parsed modules.

### Test Cases

Minimum set of integration tests (`src/tess/tl/test/pass/`):

- **Single-parameter block**, non-generic: forward declarations and definitions, verify
  UFCS dispatch works on the desugared functions.
- **Single-parameter block**, generic (`Ptr[Container[T]]`): verify type parameter
  inference and that `T` is usable in function signatures and bodies.
- **Multi-parameter block**: two parameters, verify both are prepended in order.
- **Mixed types in multi-parameter block**: parameters with different types.
- **Trait constraints** on inferred type parameters (`K: HashEq`).
- **Additional function-level type parameters** (`map[U]` inside a `[T]` block).
- **`Ptr[Const[Container[T]]]` receiver**: verify const-correctness with generic receivers.
- **Multiple blocks in one module**: different receiver types coexisting.
- **Mixed declarations and definitions** in one block.
- **Implementation block**: synopsis block followed by a matching implementation block.

Expected-failure tests (`src/tess/tl/test/fail/`):

- Receiver block inside a function body (not at module top level).
- Malformed block (missing closing brace, missing colon before brace).


## Alternatives Considered

### `methods` keyword

```tl
methods(s: Ptr[Const[String]]) {
    len() -> CSize
}
```

Rejected: introduces a top-level keyword, breaking Tess's convention that only `#hash`
directives serve as keywords at the top level.

### `#with` / `#endwith` directive

```tl
#with(s: Ptr[Const[String]])
len() -> CSize
is_empty() -> Bool
#endwith
```

Region-based, consistent with `#ifc`/`#endc`. Rejected because braces make scope visually
unambiguous, and the binding-like `name: Type :` form is more consistent with existing
Tess syntax.

### `impl` block

```tl
impl String {
    len(s: Ptr[Const[String]]) -> CSize
}
```

Rejected: introduces a keyword, and doesn't reduce repetition since the receiver type
must still be written on each function.


## Decisions

1. **Formatter.** `tess fmt` will treat functions inside a receiver block the same as
   top-level functions for alignment purposes.

2. **Implementation blocks.** Receiver blocks are expected to appear in both the synopsis
   and the implementation section. A synopsis block establishes the signatures; a matching
   implementation block encloses the definitions. This is especially valuable for generic
   types where standalone implementations would otherwise require repeating type arguments
   on every function.

3. **Parameter ordering.** Receiver blocks standardize parameter ordering: the receiver
   is always first, with the allocator second. Constructor functions (no receiver) keep
   the allocator first. Standard library migration to receiver blocks is deferred —
   the feature will be verified via new tests, not by rewriting existing `.tl` files.
