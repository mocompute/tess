# Tess Language Reference

This is the complete syntax guide for Tess. For the design rationale behind the syntax, see the [Language Model](LANGUAGE_MODEL.md).

If you're coming from C, most of Tess will look familiar: braces for blocks, parentheses for calls, the same operators. The main differences:

- `:=` introduces a binding, `=` assigns to an existing one
- Type annotations are usually optional: the compiler infers them
- Functions return their last expression: no `return` needed
- `if`, `case`, and `when` produce values

## Program Structure

A Tess program consists of one or more **modules**. Each module begins with a `#module` directive:

```tl
#module main
main() { 0 }
```

The entry point is the `main` function in the `main` module.

A source file may contain one or more modules.

## Modules and Imports

```tl
#module ModuleName      // Declare a module
#import <filename.tl>   // Import another Tess file
#include <header.h>     // Include a C header
```

Access module members with dot notation: `ModuleName.function()` or `ModuleName.Type`

During compilation, all input source files are scanned to collect their imports. Imported files are then
processed in dependency order before processing any of the source files.

### Submodules

A dotted module name declares a submodule:

```tl
#module Outer
T: { value: Int }

#module Outer.Inner
U: { name: CString }
```

`Outer.Inner` is a submodule of `Outer`. Access its members as `Outer.Inner.U(...)`. Submodules are
independent namespaces — the parent-child relationship is purely organizational. However, parent modules
must be declared before their children:

```tl
// Error: Outer.Nested has not been declared
#module Outer
#module Outer.Nested.Inner
```

Each level must be declared in order:

```tl
#module Outer
#module Outer.Nested
#module Outer.Nested.Inner    // OK
```

### One Module, One Type

A module typically defines a single type and its associated functions. Operators like `+` are resolved by
looking up `add` in the operand's module, so two types with the same operator signatures in one module would
conflict. Give each type its own module:

```tl
// Good: separate modules for separate types
#module Vec2
T : { x: Int, y: Int }
add(a: T, b: T) -> T { T(x = a.x + b.x, y = a.y + b.y) }

#module Vec3
T : { x: Int, y: Int, z: Int }
add(a: T, b: T) -> T { T(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z) }
```

This convention also makes [auto-collapse](#auto-collapse-for-same-name-types) work naturally: naming the
type `T` (or the same as the module, e.g. Vec2, Vec3) lets users write `Vec2.T(...)` or just `Vec2(...)`.

This is only a convention. Modules may declare many types.

### Packages

Modules can be distributed as `.tpkg` packages. When a package is declared as a dependency via `depend()` in
`package.tl`, all its modules are loaded automatically -- no `#import` needed. Consumer code accesses
package modules with the same qualified syntax: `Module.function()`.

See [PACKAGES.md](PACKAGES.md) for the full package system reference.

### Module Initialization

Modules can define an `__init()` function that runs automatically at
the start of the program:

```tl
#module Config

default_value := 0

__init() {
  default_value = load_from_env()
}
```

Module initialization functions are called in dependency order before `main()` executes.

**Note:** `__init()` only works in named modules (those with a `#module` declaration other than `main`).

### Module Aliases

The `#alias` directive creates a shorthand name for an imported module:

```tl
#alias OI Outer.Inner        // OI is now shorthand for Outer.Inner
#unalias OI                  // Remove the alias
```

Aliases substitute the leftmost segment of a dotted reference. All references to `OI.foo` are parsed as if written `Outer.Inner.foo`:

```tl
#import <Collections/HashMap.tl>
#alias HM Collections.HashMap

HM.create()                  // Same as Collections.HashMap.create()
HM.insert(map, key, value)   // Same as Collections.HashMap.insert(...)
```

**Scope:** Aliases are effective from the `#alias` directive through the end of the current file, or until `#unalias` is used.

**Leftmost only:** Aliases only apply when the alias name is the first segment in a dotted reference. `Root.OI.foo` does not trigger the alias.

**Restrictions:**

- The source module must already be imported
- The alias name must not conflict with an existing module or alias
- Self-aliases (`#alias Foo Foo`) are not allowed
- The alias name cannot use reserved prefixes (`c_*`, `_tl_*`) or contain `__`
- The `main` module cannot be aliased

### Re-opening Modules

A module can be re-opened after other modules have been defined. This is useful when defining a submodule
requires interrupting the parent module:

```tl
#module Foo
FooData: { x: CInt }
create_data(x: CInt) -> FooData { FooData(x = x) }

#module Foo.Bar
BarData: { y: CInt }
make_bar(y: CInt) -> BarData { BarData(y = y) }

#module Foo
// Back in Foo — all previously defined symbols are available
create_pair(x: CInt, y: CInt) -> FooData {
    bar := Foo.Bar.make_bar(y)
    create_data(x + bar.y)
}
```

When a module is re-opened, all symbols from the original definition are restored. New definitions are added
normally. If a symbol is defined more than once (e.g., when the same file is parsed twice through different
import paths), the first definition wins and duplicates are silently skipped.

A module may be reopened from a different source file. However, the compiler only dependency-orders imported
files (via #import); the source files themselves are processed in the order they appear on the command line
or in the source() list in package.tl. When source() points to a directory, the order is
filesystem-dependent and not guaranteed. If a reopening in one source file depends on declarations from
another source file's opening of the same module, the result depends on which file is processed first. To
avoid this:

1. Only reopen modules within the same file that defines them, or
2. Only reopen standard library modules (e.g., to add trait implementations for your types).


### Auto-Collapse for Same-Name Types

When a module defines a type with the same name as the module itself, or a type named `T`, the compiler
automatically registers the bare module name as a type alias. This means you can use the short form in
type positions without an explicit `#alias`:

```tl
#import <Array.tl>

// Instead of writing the fully qualified type:
a: Array.Array[Int]

// You can use the bare module name:
a: Array[Int]
```

Both forms are interchangeable — they refer to the same type and can be mixed freely in function signatures:

```tl
convert(a: Array[Int]) -> Array.Array[Int] { a }   // OK, but discouraged
```

This applies to any module whose primary type shares its name, including standard library modules like `Array` and `HashMap`:

```tl
#import <HashMap.tl>

make_map() -> Ptr[HashMap[Int, Int]] {
    HashMap.create[Int, Int]()
}
```

Both conventions work:

```tl
#module Vec2
Vec2[T]: { x: T, y: T }      // Same name as module

#module Point
T[a]: { x: a, y: a }         // Named T
```

In both cases, callers write `Vec2(x = 1, y = 2)` or `Point(x = 1, y = 2)`. Defining both a same-named type
and a `T` type in the same module is an error.

**Note:** Auto-collapse only affects type positions. Functions must still be called with the module prefix
(`Array.push(...)`, `HashMap.set(...)`), or more commonly with method syntax on the first argument:
`arr.push(x)`, `map.set(k, v)`. See [Dot-Call Syntax](#dot-call-syntax).

**Precedence:** If the bare name is already registered as a type (e.g., by another module or an explicit
type alias), auto-collapse does not override it.

## Types

### Primitive Types

- `Int` - 64-bit signed integer
- `Float` - 64-bit floating point
- `Bool` - Boolean (`true` or `false`)
- `Void` - No value

### C-compatible Types

**Integers:**
- `CChar`, `CShort`, `CInt`, `CLong`, `CLongLong`
- Unsigned variants: `CUnsignedChar`, `CUnsignedShort`, `CUnsignedInt`, `CUnsignedLong`, `CUnsignedLongLong`

**Fixed-width integers:**
- `CInt8`, `CInt16`, `CInt32`, `CInt64`
- `CUInt8`, `CUInt16`, `CUInt32`, `CUInt64`

**Floating point:**
- `CFloat`, `CDouble`, `CLongDouble`

**Size types:**
- `CSize`, `CPtrDiff`

`Int` is an alias for `CLongLong` and `UInt` is an alias for `CUnsignedLongLong`.

### Integer Type Conversions

Integer types are organized into sub-chains with width ordering:

**C-Named Signed:** `CSignedChar < CShort < CInt < CLong < CLongLong (= Int)`

**C-Named Unsigned:** `CUnsignedChar < CUnsignedShort < CUnsignedInt < CUnsignedLong < CUnsignedLongLong (= UInt)`

**Fixed-Width Signed:** `CInt8 < CInt16 < CInt32 < CInt64`

**Fixed-Width Unsigned:** `CUInt8 < CUInt16 < CUInt32 < CUInt64`

**Standalone Types** (no implicit conversion): `CSize`, `CPtrDiff`, `CChar`

Conversions follow strict rules:

**Implicit widening** (narrow → wide, same sub-chain) is automatic in all directed contexts:
```tl
// Variable bindings
x: CShort := 1
y: CInt := x             // OK: CShort → CInt (widening)
z: Int := y              // OK: CInt → Int (widening)

// Reassignment
w: Int := 0
w = x                    // OK: CShort widens to Int

// Function arguments
take_int(n: Int) { n }
take_int(x)               // OK: CShort widens to Int

// Return values
to_int(n: CShort) -> Int { n }  // OK: CShort return widens to Int
```

**Narrowing** (wide → narrow), **cross-family** (signed ↔ unsigned), **cross-chain** (C-named ↔ fixed-width), and **standalone** (`CSize`, `CPtrDiff`, `CChar`) conversions require an explicit binding type annotation:
```tl
narrow:   CInt   := some_int_value      // Narrowing: Int → CInt
unsigned: UInt   := some_int_value      // Cross-family: signed → unsigned
fixed:    CInt32 := some_cint_value     // Cross-chain: C-named → fixed-width
size:     CSize  := some_uint_value     // Standalone: UInt → CSize
```

The binding annotation is the only cast syntax — there is no `as` keyword.

**Operators and generics require exact type match:**
```tl
a: CInt := 1
b: CShort := 2
c := a + b               // Error: CInt != CShort
wide: CInt := b
c := a + wide            // OK: CInt + CInt
c := a + 2               // OK: literal 2 adapts to CInt
```

In debug builds, narrowing conversions emit a runtime bounds check that aborts if the value does not fit in the target type.

See [TYPE_SYSTEM.md](TYPE_SYSTEM.md) for how the constraint solver handles these conversions.

### Float/Integer Conversion

`Int` and `Float` are not implicitly convertible. Conversion requires an explicit binding type annotation (the same cast syntax used for integer narrowing and pointer casts):

```tl
x: Float := 3.7
y: Int := x          // OK: float-to-integer cast (truncates)

n: Int := 42
f: Float := n        // OK: integer-to-float cast
```

Integer literals are always integer-typed, not `Float`:
```tl
x: Float := 0       // Error: 0 is an integer literal, not Float
x: Float := 0.0     // OK: 0.0 is Float
```

### Pointer Types

```tl
Ptr[T]               // Mutable pointer to type T
Ptr[Const[T]]        // Const pointer to type T (read-only)
Const[Ptr[T]]        // Immutable pointer to mutable data
Const[Ptr[Const[T]]] // Immutable pointer to read-only data
CArray[T, N]         // C-style fixed-size array
```

See [Pointers](#pointers) for operations, const semantics, array decay, and a C-to-Tess reference table.

### Generic Types

Types can have type parameters:

```tl
Point[T] : { x: T, y: T }
```

### Type Aliases

```tl
Pt = Point[Int]    // Creates a type alias
```

An alias can refer to a fully specialized generic or to the unspecialized generic itself. Partial specialization is not supported:

```tl
Pt = Point[Int]                    // OK: fully specialized
Pt = Point                         // OK: alias for the unspecialized generic
StringMap[V] = HashMap[String, V]  // Error: partial specialization
```

### Function Aliases

Function aliases create a local shorthand for a module-qualified function:

```tl
print   = Print.print
println = Print.println
my_add  = Math.add
```

An alias can be called directly — all arity overloads of the target function are accessible:

```tl
my_add(1, 2)       // calls Math.add/2
my_add(1, 2, 3)    // calls Math.add/3
```

Function references via the `/N` syntax also work through aliases:

```tl
apply(my_add/2, 10, 20)   // passes Math.add/2 as a function pointer
```

Function aliases are a parse-time name rewrite — they are not first-class values and cannot be used in UFCS
position (`value.alias(args)` does not resolve aliases).

The compiler distinguishes function aliases from type aliases automatically: if the target has arity-mangled
entries in the module's symbol table, it is a function; otherwise it is a type.

### Explicit Type Parameters

Generic functions declare type parameters using square brackets. Square brackets always denote type
arguments; parentheses always denote value arguments or constructors:

```tl
// Function with explicit type parameter
empty[T]() -> Array[T] {
  with_capacity[T](16)
}

// Call with explicit type argument
arr := empty[Int]()           // Creates Array[Int]
floats := empty[Float]()      // Creates Array[Float]
```

This pattern is commonly used in the standard library for functions that need to create values of a generic
type.

## Pointers

### Address-of and Dereference

```tl
ptr := obj.&          // Address-of (get pointer to obj)
val := ptr.*          // Dereference (get value at pointer)
```

### Arrow Operator

```tl
ptr->field            // Equivalent to ptr.*.field
```

### Pointer Indexing

```tl
ptr.[i]               // Index into pointer (pointer arithmetic)
ptr.[i] = value       // Write through pointer index
```

### Type Casts via Binding Annotation

The binding type annotation is the universal cast syntax in Tess. It covers both pointer casts and integer conversions:

```tl
// Pointer casts
p: Ptr[Int]  := c_malloc(sizeof[Int]() * 10zu)
b: Ptr[Byte] := p     // Cast to different pointer type

// Integer conversions (narrowing, cross-family, cross-chain, standalone)
narrow: CInt := some_int_value
size: CSize  := some_uint_value
```

See [Integer Type Conversions](#integer-type-conversions) for the full conversion rules.

### Const Values

`Const[T]` declares a binding that cannot be reassigned. It transpiles to `const T` in C.

```tl
x: Const[Int] := 42         // Cannot reassign x
x: Const      := 42         // Same, with type inference
```

**Reassignment and compound assignment are rejected:**

```tl
x: Const[Int] := 5
x = 10                       // Error: const violation
x += 1                       // Error: const violation
```

**Shadowing with `:=` is allowed** — it creates a new binding, not a reassignment:

```tl
x: Const := 5
x := 10                      // OK: new binding shadows the const one
```

**Struct field mutation is rejected:**

```tl
p: Const[Point] := Point(x = 1, y = 2)
p.x = 10                     // Error: const violation
```

**Function parameters** can be const:

```tl
add_one(x: Const[Int]) {
    x + 1                     // OK: reading
}

add_one(42)                   // OK: Int unifies with Const[Int]
```

**For-loop variables** are implicitly const. Value iterators produce `Const[T]`, pointer iterators produce `Const[Ptr[T]]` (cannot reassign the pointer, but can mutate through it):

```tl
for x in arr {
    x = 99                    // Error: const violation
}

for p.& in arr {
    p.* = p.* + 1             // OK: can mutate through Const[Ptr[T]]
}
```

**Address-of a const binding** produces `Ptr[Const[T]]`, preserving const safety:

```tl
x: Const[Int] := 42
mutate(x.&)                   // Error if mutate expects Ptr[Int]
```

#### Const and Pointers

`Const` on a binding and `Const` inside `Ptr` are orthogonal, matching C semantics:

| Tess | C | Reassign | Mutate pointee |
|------|---|:---:|:---:|
| `Ptr[Int]` | `int*` | yes | yes |
| `Ptr[Const[Int]]` | `const int*` | yes | no |
| `Const[Ptr[Int]]` | `int* const` | no | yes |
| `Const[Ptr[Const[Int]]]` | `const int* const` | no | no |

### Const Pointers

`Ptr[Const[T]]` declares a pointer through which the target cannot be modified.
It transpiles to `const T*` in C.

```tl
read_value(p: Ptr[Const[Int]]) {
    p.*                       // OK: reading through const pointer
}

read_point(p: Ptr[Const[Point]]) {
    p->x + p->y              // OK: reading struct fields through const pointer
}
```

**Implicit coercion:** A mutable pointer can be passed where a const pointer is expected:

```tl
p: Ptr[Int] := c_malloc(8)
p.* = 42
val := read_value(p)         // OK: Ptr[Int] -> Ptr[Const[Int]]
```

**Mutation through const pointers is rejected:**

```tl
mutate(p: Ptr[Const[Int]]) {
    p.* = 10                  // Error: const violation
}
```

**Stripping const is rejected.** A const pointer cannot be passed where a mutable pointer is expected:

```tl
write(p: Ptr[Int]) { p.* = 10  0 }

pass(p: Ptr[Const[Int]]) {
    write(p)                  // Error: const violation
}
```

This also applies to struct constructor fields and return statements — any implicit context where a mutable pointer is expected will reject a const pointer.

This applies at any pointer nesting level: `Ptr[Ptr[Const[T]]]` cannot be passed where `Ptr[Ptr[T]]` is expected.

**Casting away const:** When necessary, const can be explicitly stripped using an annotated binding (the language's general cast mechanism):

```tl
unsafe_strip(p: Ptr[Const[Int]]) -> Ptr[Int] {
    mp: Ptr[Int] := p            // OK: explicit cast strips const
    mp
}
```

`Const` works with generic type parameters. Both `Ptr[Const[T]]` (const pointer to a generic type) and `Ptr[Const[Container[T]]]` (const pointer to a generic container) are valid:

```tl
read_only[T](p: Ptr[Const[T]]) -> T { p.* }
first[T](arr: Ptr[Const[Array[T]]]) -> T { arr->data.[0] }
copy_val[T](dst: Ptr[T], src: Ptr[Const[T]]) { dst.* = src.* }
```

### Fixed-Size Arrays (CArray)

`CArray[T, N]` declares a fixed-size C array. It can be used as a local variable or a struct field.

```tl
// Local variable
arr: CArray[Int, 5] := void
arr.[0] = 42                          // Direct indexing

// Struct field
Buffer: { data: CArray[CChar, 256], len: CInt }
b := Buffer(data = void, len = 0)
```

**Static array initialization:** Not supported. The binding expression must initialize the array to `void`,
which leaves the underlying memory uninitialized. Use direct indexing (or C library functions like `c_memset`)
to initialize the array.

**Decay to pointer:** CArrays decay to pointers automatically at function call sites (matching C semantics)
and in struct field access. Explicit decay via binding annotation is also supported:

```tl
// Function call: automatic decay
buffer: CArray[CChar, 256] := void
c_strcpy(buffer, "hello")             // CArray[CChar, 256] decays to Ptr[CChar]

// Struct field: automatic decay
data_ptr: Ptr[CChar] := b.data        // Decays automatically

// Explicit decay via binding
ptr: Ptr[CChar] := buffer             // Also works
```

**sizeof:** `sizeof(arr)` and `sizeof[CArray[T, N]]()` return the full array size (`N * sizeof(T)`),
not the pointer size. This matches C semantics for arrays that have not decayed:

```tl
arr: CArray[CInt, 16] := void
sizeof(arr)                // 64 (= 16 * 4)
sizeof[CArray[CInt, 16]]() // 64
```

### Pointers and Arrays for C Programmers

Quick reference for common C patterns and their Tess equivalents:

| C                           | Tess                             |
|-----------------------------|----------------------------------|
| `ptr[n]`                    | `ptr.[n]`                        |
| `ptr[n] = x`                | `ptr.[n] = x`                    |
| `*ptr`                      | `ptr.*`                          |
| `&obj`                      | `obj.&`                          |
| `ptr->field`                | `ptr->field`                     |
| `NULL`                      | `null`                           |
| `if (ptr != NULL)`          | `if ptr != null { ... }`         |
| `void*`                     | `Ptr[any]`                       |
| `int arr[5]`                | `arr: CArray[Int, 5] := void`    |
| `struct { char buf[256]; }` | `T: { buf: CArray[CChar, 256] }` |
| `malloc(n)` / `free(p)`     | `c_malloc(n)` / `c_free(p)`      |

**Pointer arithmetic** goes through the `Unsafe` module, which operates in bytes (not elements):

```tl
#import <Unsafe.tl>

base: Ptr[Int] := c_malloc(sizeof[Int]() * 10zu)

// Byte-level arithmetic
next := Unsafe.pointer_add(base, sizeof[Int]())       // advance by one Int
dist := Unsafe.pointer_difference(next, base)          // byte distance
cmp  := Unsafe.pointer_compare(base, next)             // -1, 0, or 1
```

For element-level access, use pointer indexing instead:

```tl
base.[0] = 10       // first element
base.[1] = 20       // second element (advances by sizeof[Int](), not 1 byte)
```

**Allocating memory** uses `c_malloc` with a pointer cast:

```tl
p: Ptr[Int] := c_malloc(sizeof[Int]() * 10zu)
defer c_free(p)
p.[0] = 42
```

## Type Annotations

The Tess compiler uses Hindley-Milner style type inference. Annotations are optional in most cases.

### When Type Annotations Can Be Omitted

#### Variable Bindings with Inferrable RHS

| Pattern                                              | Example                        | Reason                                      |
|------------------------------------------------------|--------------------------------|---------------------------------------------|
| Integer literals                                     | `x := 42`                      | Weak signed, defaults to `Int`              |
| Float literals                                       | `x := 3.14`                    | Literal type is `Float`                     |
| Struct constructors                                  | `p := Point(x = 1, y = 2)`     | Type inferred from constructor              |
| Tagged union constructors (with constraining fields) | `opt := Some(42)`              | Type parameter inferred from argument value |
| CArray binding                                       | `arr: CArray[Int, 10] := void` | CArray is a type annotation                 |
| Function calls                                       | `result := add(1, 2)`          | Return type inferred from function          |

#### Function Parameters and Return Types

Functions that are **called directly** can have types inferred:

```tl
// All annotations can be omitted:
add(a, b) { a + b }           // Inferred from usage
apply(f, x) { f(x) }          // Inferred from how arguments are used
map(f, arr) { ... }           // Inferred from call sites
```

### When Type Annotations ARE Required

#### C FFI Declarations

```tl
c_malloc(size: CSize) -> Ptr[any]    // C functions need full signatures
c_printf(fmt: CString, ...) -> CInt
```

#### Pointer Casts

See [Type Casts via Binding Annotation](#type-casts-via-binding-annotation) in the Pointers section.

#### C Type Disambiguation

Integer literals are polymorphic — they adapt to context. A bare literal like `42` can become any signed integer type. Use a type annotation when there is no context to determine the type, or to perform an explicit conversion:

```tl
x: CInt := 42                   // Literal adapts to CInt
f: CFloat := 3.14               // Force CFloat instead of Float
sz := 1024zu                    // CSize literal (zu suffix)
```

See [Integer Literals](#integer-literals) for the full set of literal suffixes.

#### c_malloc Return Type

See [Pointers and Arrays for C Programmers](#pointers-and-arrays-for-c-programmers) for `c_malloc` usage.

#### Functions Only Used as Function Pointers

When a function is **never called directly** but only stored/passed as a pointer:

```tl
// Required - only referenced as `double/1`, never called directly
double(x: Int) -> Int { x * 2 }

// Later used as:
holder := FnHolder(fn = double/1)
```

#### Functions with Struct Parameters Containing Function Pointers

When a function takes a struct that has function pointer fields, annotations are required:

```tl
Handler : { fn: (Int) -> Int }

// Required - even though called directly, the struct has a function pointer field
apply_handler(h: Handler, x: Int) -> Int {
  h.fn(x)
}
```

#### Tagged Union Variants Without Type-Constraining Fields

When a constructor doesn't constrain all type parameters:

```tl
T[a] : | Some { v: a }
       | None

good: T[Int] := None                  // Required - None has no field with type `a`
opt := Some(42)                       // Not required - argument constrains `a = Int`
```

```tl
Either[a, b] : | Left  { v: a }
               | Right { v: b }

x: Either[Int, Bool] := Left(42)     // Required - Left only constrains `a`
```

#### Functions Returning Untyped Values

```tl
foo() -> Ptr[any] { return null }     // Required - null has no type
```

### Quick Reference Table

| Scenario                                         | Annotation Required?                   |
|--------------------------------------------------|----------------------------------------|
| Integer literal `42`                             | No (weak signed, defaults to `Int`)    |
| Float literal `3.14`                             | No (inferred as `Float`)               |
| Struct constructor                               | No (inferred from constructor)         |
| Tagged union with constraining field             | No                                     |
| Tagged union without constraining field          | **Yes** (at binding site)              |
| Tagged union `when` expression                   | No (type inferred from scrutinee)      |
| Tagged union `case` expression                   | **Yes** (when type not inferrable)     |
| CArray binding                                   | **Yes** (type annotation required)     |
| CArray decay to pointer                          | No (automatic at call sites)           |
| Pointer cast to different type                   | **Yes**                                |
| C type via literal suffix (`42u`, `42zu`)        | No (suffix determines type)            |
| C type via narrowing/cross-chain cast            | **Yes** (binding annotation)           |
| C float type (CFloat, etc.)                      | **Yes**                                |
| c_malloc result                                  | **Yes**                                |
| C FFI function declaration                       | **Yes**                                |
| Function called directly                         | No (params and return inferred)        |
| Function only used as pointer                    | **Yes**                                |
| Function with struct param containing fn pointer | **Yes**                                |
| Return null                                      | **Yes**                                |

## Variables and Assignment

> See [Language Model: Bindings](LANGUAGE_MODEL.md#bindings-the-binding-expression) for the conceptual foundation of binding expressions and scoping.

Tess distinguishes between **binding** (`:=`) and **mutation** (`=`). All bindings are reassignable by default; use `Const[T]` or `Const` to prevent reassignment (see [Const Values](#const-values)).

### Binding with `:=`

The `:=` operator introduces a new name. Each use of `:=` creates a fresh binding that shadows any previous use of the same name in an outer scope. The binding's scope extends to the end of the enclosing block, and the entire construct is an expression:

```tl
x := 42              // Introduce new binding 'x'
x: Int := 42         // With explicit type annotation

x := 1
x := x + 1           // New binding shadows the previous one; x is now 2
x := x * 2           // Another new binding; x is now 4
```

Since bindings are expressions, you can capture the result of a binding block using parentheses:

```tl
result := (
  a := 10
  b := 20
  a + b              // Block evaluates to 30
)
// result is 30; a and b are not visible here
```

This is equivalent to ML's `let a = 10 in let b = 20 in a + b`.

Shadowing is lexically scoped—inner bindings don't affect outer ones:

```tl
val := 1
if true {
  val := 2           // New binding, shadows outer val within this block
  // val is 2 here
}
// val is 1 here (outer binding unchanged)
```

### Mutation with `=`

The `=` operator mutates an existing binding in place. It requires that the name already exists:

```tl
x := 0               // Create binding
x = 10               // Mutate existing binding (x is now 10)
x += 2               // Compound assignment (also -=, *=, /=)
```

**Assignment is a statement, not an expression.** Unlike `:=` which produces a value, `=` has no value. This means functions that end with an assignment implicitly return `Void`:

```tl
set_value(v) {
  global_val = v     // Assignment is last - function returns Void
}
```

No explicit `void` is needed after an assignment statement.

Mutation affects the binding in its original scope:

```tl
val := 1
if true {
  val = 2            // Mutates the outer binding
}
// val is 2 here (outer binding was modified)
```

### Why Two Operators?

This distinction makes code intent explicit:
- `:=` signals "I'm introducing a new name" (expression with a value)
- `=` signals "I'm changing an existing value" (statement with no value)

It also enables functional patterns where rebinding is preferred over mutation, while still allowing
imperative style when needed.

## Functions

### Function Definition

```tl
add(a, b) { a + b }                    // Types inferred
add(a: Int, b: Int) -> Int { a + b }   // Explicit type annotations
log(msg) { c_printf(c"%s\n", msg), void }  // void needed after expression
update(v) { value = v }                // No void needed - assignment has no value
```

The last expression in the function body is the return value. Use `void` as the final expression when the
last statement is an expression (like a function call) but the function should return nothing. If the
function ends with an assignment (`=`), no `void` is needed.

### Lambdas

> See [Language Model: Closures and Capture](LANGUAGE_MODEL.md#closures-and-capture) for the conceptual model of capture semantics, stack vs. allocated closures, and the escape restriction.

```tl
f := (x) { x + 1 }           // Lambda expression
f := (x: Int) { x + 1 }      // With type annotation
(x) { x + 1 } (5)            // Immediate application (returns 6)
```

Lambdas capture variables from the enclosing scope (closures):

```tl
val := 10
f := () { val }    // Captures val
f()                // Returns 10
```

**Capture by reference:** Closures capture variables by reference, not by value. This means mutations after
the lambda is created are visible inside the lambda:

```tl
x := 1
f := () { x }
x = 0              // Mutate x after creating lambda
f()                // Returns 0, not 1
```

This also means lambdas can mutate captured variables:

```tl
counter := 0
increment := () { counter = counter + 1 }
increment()        // counter is now 1
increment()        // counter is now 2
```

**Lambdas do not support explicit type parameters:** Lambdas can be generic through type inference
(un-annotated parameters are inferred from usage), but cannot declare explicit type parameters with `[T]`
syntax. If you need explicit type parameters, use a named function instead.

**Stack closures cannot be returned from functions:** Because stack closures capture variables by reference,
returning one would create dangling pointers to stack variables that no longer exist. The compiler prohibits
this:

```tl
// ERROR: Cannot return lambda from function
make_adder(n) { (x) { x + n } }
```

If you need to return a callable without captures, use a named
function (with arity) or an allocated closure:

```tl
add1(x) { x + 1 }
get_adder() { add1/1 }     // OK: returns function pointer, not lambda
```

#### Allocated Closures

To return a closure from a function or store it in a struct, use an **allocated closure**. The `[[alloc]]`
attribute allocates the captured state on the heap, and `[[capture(...)]]` explicitly lists which variables
to capture by value:

```tl
make_adder(n: Int) {
  [[alloc, capture(n)]] (x) { x + n }
}

add5 := make_adder(5)
add5(10)   // 15
```

Because captures are by value, mutations after closure creation are not visible inside the closure:

```tl
n := 5
f := [[alloc, capture(n)]] (x) { x + n }
n = 10
f(1)       // 6, not 11 — n was copied at creation
```

To mutate state from within an allocated closure, capture a pointer:

```tl
make_counter(p: Ptr[Int]) {
  [[alloc, capture(p)]] () { p.* = p.* + 1, p.* }
}

count := 0
counter := make_counter(count.&)
counter()   // 1
counter()   // 2
counter()   // 3
```

The capture list must exactly match the free variables in the body — missing or unused captures are compile errors:

```tl
// ERROR: body uses 'y' but it's not in the capture list
f := [[alloc, capture(x)]] () { x + y }

// ERROR: 'z' is listed but not used in the body
g := [[alloc, capture(x, z)]] () { x + 1 }
```

**Allocator control:** `[[alloc]]` uses the default allocator. Pass an explicit allocator with `[[alloc(expr)]]`:

```tl
f := [[alloc(my_arena), capture(x)]] (y) { x + y }
// context freed when arena is destroyed
```

Both stack and allocated closures share the `Closure[F]` type and calling convention, so higher-order functions accept either kind interchangeably.

### Function Pointers

To get a pointer to a function, use the function name followed by `/` and its arity (number of parameters):

```tl
add1(x) { x + 1 }
fp := add1/1         // Get pointer to add1 (arity 1)
apply(f, x) { f(x) }
apply(fp, 5)         // Pass function pointer as argument
```

Function pointers can be stored in structs:

```tl
f1() { 1 }
Ctx[T] : { callback: T }
ctx := Ctx(callback = f1/0)
ctx.callback()       // Call through struct field
```

**Note:** Explicit type parameters are not supported on lambda functions. Lambdas can still be generic through type inference (un-annotated parameters), but cannot declare type parameters with `[T]`. See [Lambdas](#lambdas).

### Generic Function Signatures

```tl
map[T, U](f: (T) -> U, arr: Arr[T]) -> Arr[U]
```

Type parameters can have trait bounds that constrain them to types satisfying a trait:

```tl
sort[T: Ord](arr: Array[T]) { ... }
double[T: Add](x: T) -> T { x + x }
convert[A, B: Add](a: A, b: B) -> B { ... }   // only B is bounded
```

See [Traits](#traits) for details on trait declarations, conformance, and bounds.

### Function Overloading by Arity

Functions with the same name can be defined with different numbers of parameters. The compiler resolves calls based on the number of arguments provided:

```tl
add(x) { x }
add(x, y) { x + y }
add(x, y, z) { x + y + z }

main() {
  a := add(1)        // Calls single-argument version
  b := add(1, 2)     // Calls two-argument version
  c := add(1, 2, 3)  // Calls three-argument version
}
```

Each overload must have a distinct number of parameters. Overloads are resolved at compile time based on argument count.

When taking a pointer to an overloaded function, the `/arity` syntax disambiguates which version:

```tl
fp := add/2          // Pointer to the two-argument version
```

### Variadic Functions

Variadic functions accept a variable number of arguments through a **trait-bounded** mechanism. The last
parameter uses `...Trait` syntax, where each extra argument must satisfy the named trait. The compiler
applies the trait's function to each argument at the call site, packing the results into a `Slice`.

#### Declaration

```tl
#import <ToString.tl>

log(level: Int, args: ...ToString) -> Void {
    // args is Slice[String] — the trait function to_string was applied to each arg
    i: CSize := 0
    while i < args.size, i += 1 {
        s := args.v.[i]
        c_printf(c"%s", s.&.cstr())
    }
    c_printf(c"\n")
    void
}
```

The variadic parameter must be the **last** parameter. The function body sees it as `Slice[R]` where `R` is
the return type of the trait's function.

#### Trait requirements

A trait used as a variadic bound must:

1. Declare exactly **one function** (excluding inherited parent functions)
2. That function must be **unary** (one parameter of the trait's type `T`)
3. The return type must be **concrete** (not `T`) — so all call-site results share one type

Valid variadic traits:
- `ToString[T] : { to_string(a: T) -> String }` — returns `String`
- `Hash[T] : { hash(x: T) -> CSize }` — returns `CSize`

Invalid variadic traits:
- `Eq[T] : { eq(a: T, b: T) -> Bool }` — binary, not unary
- `Neg[T] : { neg(a: T) -> T }` — return type is `T`, not concrete

#### Call-site semantics

```tl
log(1, 42, "hello", 3.14)
```

The compiler:
1. Checks each extra argument satisfies the trait bound (`ToString`)
2. Applies `to_string()` to each argument at the call site
3. Packs the results into a stack-allocated array
4. Passes a `Slice` (pointer + count) to the function

The generated C is equivalent to:
```c
String tmp0 = Int__to_string__1(NULL, 42);
String tmp1 = CString__to_string__1(NULL, "hello");
String tmp2 = Float__to_string__1(NULL, 3.14);
String arr[] = {tmp0, tmp1, tmp2};
log__2(NULL, 1, (Slice_String){arr, 3});
```

#### Body access

Inside the function, the variadic parameter is a `Slice[T]`:

- `args.size` — number of variadic arguments (`CSize`)
- `args.v.[i]` — access the i-th element (pointer indexing)

Iteration via `for x in Slice args { ... }` is supported when `Slice.tl` is imported.

#### Zero arguments

Calling with zero variadic arguments is valid:

```tl
log(1)    // args is empty: Slice with NULL pointer and size 0
```

#### Overloading restriction

A variadic function precludes arity overloading for that name in the same module. If `print` is variadic, no other `print` with any arity can exist in the same module.

#### Function pointers

Variadic functions have a concrete type signature for pointer purposes:

```tl
// print : (Slice[String]) -> Void
fp := Print.print/1
```

The caller must construct the `Slice` manually when calling through a pointer — the syntactic sugar only applies to direct calls.

#### Restrictions

- Variadic parameter must be the **last** parameter
- **Not allowed** on: operator functions, lambdas/closures
- C FFI functions (`c_` prefix) continue to use the existing untyped `...` mechanism

#### Standard library

`ToString.tl` provides the `ToString` trait with implementations for `Int`, `UInt`, `Float`, `Bool`, `CString`, and `String`. The `Print` module provides variadic `print` and `println`:

```tl
#import <ToString.tl>

main() {
    Print.println(42, " is the answer")
    Print.print("x = ", 3.14, ", done = ", true)
    0
}
```

### Tail Call Optimization

Tess defines tail call optimization (TCO) as part of the language semantics. The generated C code is structured to enable TCO, and the underlying C compilers implement the optimization on all supported platforms. Tail-recursive functions are compiled to loops, allowing deep recursion without stack overflow:

```tl
sum_to(n, acc) {
  if n == 0 { acc }
  else { sum_to(n - 1, acc + n) }  // Tail call - optimized to loop
}

result := sum_to(1000000, 0)       // Works without stack overflow
```

### Dot-Call Syntax

Also known as uniform function call syntax (UFCS).

Any function can be called using dot syntax on its first argument. If `x.foo(a, b)` does not match a struct field named `foo`, the compiler rewrites it to `foo(x, a, b)`:

```tl
Vec2: { x: Int, y: Int }

length_sq(v: Vec2) -> Int { v.x * v.x + v.y * v.y }
scale(v: Vec2, s: Int) -> Vec2 { Vec2(x = v.x * s, y = v.y * s) }

main() {
    v := Vec2(x = 3, y = 4)

    v.length_sq()            // calls length_sq(v)
    v.scale(2)               // calls scale(v, 2)
    v.scale(3).length_sq()   // chaining works
}
```

**Priority:** Struct fields always take priority over dot-call syntax. If a struct has a field `foo`, then `x.foo(...)` calls the field's function pointer, not a free function named `foo`.

**Pointer receiver:** The `.` operator auto-dereferences pointer receivers. If a function takes `Ptr[T]` and the receiver is a value of type `T`, the address is taken implicitly. If the receiver is already a pointer, it is passed as-is:

```tl
reset(p: Ptr[Vec2]) { p->x = 0, p->y = 0, void }

v.reset()                    // calls reset(v.&) — implicit address-of
```

Note: The `->` operator is reserved for struct field access through pointers (`ptr->field`) and does not support dot-call syntax.

**Cross-module dot-call syntax:** For struct types, the compiler looks up functions in the module that defines the struct's type — no module qualifier is needed. For non-struct values (e.g., integers, pointers), include the module name after the dot as a fallback:

```tl
v.length_sq()                // v is Vec2 — looks up length_sq in Vec2's module
n.mymath.square()            // n is Int — module qualifier needed for non-struct types
```

**Generics:** dot-call syntax works with generic structs and generic functions. Inside generic function bodies, dot-call syntax resolution is deferred to specialization, when the receiver's type is known.

### Receiver Blocks

A receiver block factors shared parameters out of a group of function declarations and/or definitions.
This is purely syntactic sugar — the parser desugars each entry into a normal top-level function with the
block's parameters prepended.

```tl
(s: Ptr[Const[String]]): {
    len()                 -> CSize
    is_empty()            -> Bool
    byte_at(index: CSize) -> Option[Byte]
}
```

Desugars to:

```tl
len(s: Ptr[Const[String]])               -> CSize
is_empty(s: Ptr[Const[String]])           -> Bool
byte_at(s: Ptr[Const[String]], index: CSize) -> Option[Byte]
```

The identifier before the colon inside the parentheses is the parameter name — there is no implicit `self`
or `this`. The name is available in function bodies:

```tl
(s: Ptr[Const[String]]): {
    is_empty() -> Bool {
        len(s) == 0
    }
}
```

A block can contain forward declarations, full definitions, or both.

#### Multiple Receiver Types

A module may have multiple blocks for different receiver types:

```tl
// Immutable access.
(s: Ptr[Const[String]]): {
    len()      -> CSize
    is_empty() -> Bool
}

// Mutation.
(self: Ptr[String]): {
    push(other: Ptr[Const[String]]) -> Void
    free()                          -> Void
}
```

#### Multiple Parameters

Multiple parameters can be factored out by separating them with commas:

```tl
(a: Ptr[Const[String]], b: Ptr[Const[String]]): {
    eq()  -> Bool
    cmp() -> CInt
}
```

Desugars to:

```tl
eq(a: Ptr[Const[String]], b: Ptr[Const[String]])  -> Bool
cmp(a: Ptr[Const[String]], b: Ptr[Const[String]]) -> CInt
```

The parameters need not share a type. All named parameters are available in function bodies.

#### Generics

The parser infers type parameters from the receiver type by checking which identifiers are known types and
which are not. In `Ptr[Array[T]]`: `Ptr` and `Array` are known, `T` is unknown — therefore `T` becomes a
type parameter threaded to every function in the block:

```tl
(self: Ptr[Array[T]]): {
    push(x: T) -> Void
    pop()       -> T
}
```

Desugars to:

```tl
push[T](self: Ptr[Array[T]], x: T) -> Void
pop[T](self: Ptr[Array[T]])        -> T
```

**Trait constraints** on inferred type parameters are specified inline:

```tl
(self: Ptr[HashMap[K: HashEq, V]]): {
    set(key: K, value: V) -> Void
    get(key: K)           -> Ptr[V]
}
// desugars to:
// set[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K, value: V) -> Void
// get[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Ptr[V]
```

Functions that don't need the constraint go in a separate block without it.

**Additional function-level type parameters** are merged after the block-level ones:

```tl
(self: Ptr[Array[T]]): {
    map[U](f: fn/1(T) -> U) -> Array[U]
}
// desugars to: map[T, U](self: Ptr[Array[T]], f: fn/1(T) -> U) -> Array[U]
```

#### Explicit Type Parameters

Zero-parameter blocks can specify type parameters explicitly with `[T](): { ... }`. This is useful for
constructor-like functions that have no receiver to infer type parameters from:

```tl
[T](): {
    empty()                           -> Array[T]
    init(val: T, count: CSize)        -> Array[T]
    from_ptr(ptr: Ptr[T], len: CSize) -> Array[T]
}
```

Desugars to:

```tl
empty[T]()                             -> Array[T]
init[T](val: T, count: CSize)          -> Array[T]
from_ptr[T](ptr: Ptr[T], len: CSize)   -> Array[T]
```

Multiple type parameters and trait constraints work the same as with inferred type parameters:

```tl
[K: HashEq, V](): {
    create() -> Ptr[HashMap[K, V]]
}
// desugars to: create[K: HashEq, V]() -> Ptr[HashMap[K, V]]
```

Functions inside the block can introduce additional type parameters, merged after the block-level ones:

```tl
[T](): {
    convert[U](val: T, f: (T) -> U) -> U
}
// desugars to: convert[T, U](val: T, f: (T) -> U) -> U
```

#### Scope

Receiver blocks are only valid at the module top level. They cannot appear inside function bodies, struct
definitions, or other nested contexts.

### The `main` Function

The entry point is `main()` in the `main` module. It must return `CInt`. The compiler accepts two forms:

```tl
main() { 0 }                                     // no arguments
main(argc, argv: Ptr[CString]) { 0 }             // with command-line arguments
```

The compiler provides `argc` and `argv` from C's `int argc, char const* argv[]`.

## Expressions

### Operators

**Arithmetic:** `+`, `-`, `*`, `/`, `%`

**Relational:** `==`, `!=`, `<`, `<=`, `>`, `>=`

**Logical:** `&&`, `||`, `!`

**Bitwise:** `&`, `|`, `~`, `^`, `<<`, `>>`

**Compound assignment:** `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`

### Operator Overloading

Arithmetic, comparison, and bitwise operators can be overloaded for user-defined types (structs
and tagged unions). Define a function with the corresponding name in the type's module:

| Operator | Function                   | Operator          | Function                               |
|----------|----------------------------|-------------------|----------------------------------------|
| `+`      | `add(a: T, b: T) -> T`     | `==`              | `eq(a: T, b: T) -> Bool`               |
| `-`      | `sub(a: T, b: T) -> T`     | `!=`              | derived from `eq`                      |
| `*`      | `mul(a: T, b: T) -> T`     | `<` `<=` `>` `>=` | derived from `cmp(a: T, b: T) -> CInt` |
| `/`      | `div(a: T, b: T) -> T`     | `-` (unary)       | `neg(a: T) -> T`                       |
| `%`      | `mod(a: T, b: T) -> T`     | `!` (unary)       | `not(a: T) -> Bool`                    |
| `&`      | `bit_and(a: T, b: T) -> T` | `~` (unary)       | `bit_not(a: T) -> T`                   |
| `\|`     | `bit_or(a: T, b: T) -> T`  | `<<`              | `shl(a: T, b: T) -> T`                 |
| `^`      | `bit_xor(a: T, b: T) -> T` | `>>`              | `shr(a: T, b: T) -> T`                 |

```tl
#module Vec

Vec3 : { x: Int, y: Int, z: Int }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

#module main

main() {
    a := Vec.Vec3(x = 1, y = 2, z = 3)
    b := Vec.Vec3(x = 4, y = 5, z = 6)
    c := a + b       // calls Vec.add(a, b)
    d := a == b       // calls Vec.eq(a, b)
    e := a != b       // calls !Vec.eq(a, b)
}
```

Both operands must be the same type. The compiler resolves the overload by looking up the
function in the left operand's module. Built-in types use intrinsics directly and cannot
receive new operator overloads, but they conform to the compiler-provided traits matching
their intrinsic operators — `Int` satisfies `Add`, `Eq`, `Ord`, etc., so trait-bounded
generic functions like `double[T: Add](x: T)` work with built-in types.

Compound assignment desugars to the corresponding operator: `a += b` becomes `a = add(a, b)`.

For `==` and `!=`, the compiler first looks for `eq`. If absent, it falls back to deriving
from `cmp` (i.e., `cmp(a, b) == 0`). The ordering operators (`<`, `<=`, `>`, `>=`) are
derived from `cmp` returning `CInt` (negative, zero, or positive).

The `eq`-from-`cmp` fallback applies to both operator dispatch and trait conformance. A type
with only `cmp` can use `==` and `!=` operators, and also satisfies `Eq` and `Ord` trait
bounds — the compiler derives `eq` from `cmp` in both contexts. See [Traits](#traits) for
the conformance rules.

**Not overloadable:** `&&`, `||` (short-circuit semantics), `.`, `->` (struct access),
`&` (address-of), `*` (dereference), `=` (assignment), `::` (type predicate), `[]` (indexing).

### Literals

```tl
42                  // Integer (weak signed, defaults to Int)
42u                 // Unsigned integer (weak unsigned, defaults to UInt)
42z                 // CPtrDiff (signed, platform-dependent size)
42zu                // CSize (unsigned, platform-dependent size)
1_234_567           // Integer with separators (underscores ignored)
0xFF                // Hexadecimal (weak signed)
0xFFu               // Hexadecimal unsigned (weak unsigned)
0377                // Octal
3.14                // Float
1.5e-10             // Scientific notation
"hello"             // String (SSO)
c"hello"            // Ptr[CChar] (C string)
f"hello {name}"     // Format string (f-string)
'a'                 // Character literal (CChar)
'\n'                // Character with escape sequence
true, false         // Boolean
null                // Null pointer
void                // Void value
```

Character literals use C syntax: single-quoted characters with the same escape sequences as C (`'\0'`, `'\n'`, `'\t'`, `'\\'`, `'\''`, etc.). They have type `CChar`.

#### Format Strings (f-strings)

The `f"..."` prefix creates a `String` with embedded expressions. Any expression inside `{...}` is evaluated and converted to a string via `ToString`:

```tl
name := "world"
age  := 42
greeting := f"hello {name}, age {age}"   // "hello world, age 42"
```

Expressions can be arbitrary — arithmetic, function calls, or block expressions:

```tl
f"sum = {x + y}"
f"result = {add(x, y)}"
f"val = {({a := 3; a + 7})}"             // block expression in braces
```

Literal braces are produced by doubling them:

```tl
f"{{braces}}"       // "{braces}"
f"{{{x}}}"          // "{42}" when x is 42
```

Standard backslash escape sequences (`\n`, `\t`, `\\`, etc.) work in the literal segments of f-strings, just as in regular strings.

An f-string with no interpolation holes degenerates to a plain `String` literal. Any type that implements `ToString` can appear in a hole — `Int`, `Float`, `Bool`, `String`, and user-defined types with a `to_string` implementation all work.

##### Format Specifiers

An expression in an f-string hole can be followed by a colon and a format specifier that controls how the value is formatted. The syntax follows Python's [format specification mini-language](https://docs.python.org/3/library/string.html#format-specification-mini-language):

```
{expr:[[fill]align][sign][#][0][width][.precision][type]}
```

All parts are optional. When no specifier is given, the default `ToString` conversion is used.

| Part        | Values                        | Meaning                                      |
|-------------|-------------------------------|----------------------------------------------|
| `fill`      | any character                 | Padding character (default: space)            |
| `align`     | `<` `>` `^`                   | Left, right, or center alignment              |
| `sign`      | `+` `-` (space)               | Sign display for numbers                      |
| `#`         |                               | Alternate form (e.g. `0x`, `0o`, `0b` prefix) |
| `0`         |                               | Zero-pad numbers (implies right-align)        |
| `width`     | integer                       | Minimum field width                           |
| `.precision`| integer                       | Decimal places for floats                     |
| `type`      | `x` `X` `o` `b` `d` `e` `E` `f` | Type-specific conversion (see below)      |

**Integer types** (`x` hex, `X` upper hex, `o` octal, `b` binary, `d` decimal):

```tl
f"{255:#x}"          // "0xff"
f"{255:#X}"          // "0XFF"
f"{42:#o}"           // "0o52"
f"{42:#b}"           // "0b101010"
f"{42:08b}"          // "00101010"
f"{-3:+d}"           // "-3"
f"{3:+d}"            // "+3"
```

**Float types** (`f` fixed, `e`/`E` scientific):

```tl
pi := 3.14159265
f"{pi:.2f}"          // "3.14"
f"{pi:.4e}"          // "3.1416e+00"
f"{pi:+.2f}"         // "+3.14"
```

**Alignment and padding** work with any type:

```tl
f"{'hello':>10}"     // "     hello"
f"{'hello':<10}"     // "hello     "
f"{'hello':^10}"     // "  hello   "
f"{'hello':*>10}"    // "*****hello"
```

**Combined specifiers**:

```tl
f"{3.14:>+10.2f}"   // "     +3.14"
f"{255:#010x}"       // "0x000000ff"
```

**Trait dispatch.** Specifiers that require type-specific formatting (sign, `#`, zero-pad, precision, type char) use the `ToStringFormat` trait, which provides `to_string_format(a: T, spec: FormatSpec) -> String`. The standard library implements this for `Int`, `UInt`, and `Float` families. Layout-only specifiers (fill, align, width) work with any type that implements `ToString` — the value is converted to a string first, then padded.

If a type-specific specifier is used on a type that does not implement `ToStringFormat`, the compiler reports an error.

#### Integer Literals

Integer literals have a **polymorphic (weak) type** that adapts to context. The suffix determines which family of integer types the literal can become:

| Suffix    | Type          | Polymorphic?                                               |
|-----------|---------------|------------------------------------------------------------|
| (none)    | Weak signed   | Yes — resolves to any signed integer; defaults to `Int`    |
| `u`/`U`   | Weak unsigned | Yes — resolves to any unsigned integer; defaults to `UInt` |
| `z`/`Z`   | `CPtrDiff`    | No — always exactly `CPtrDiff`                             |
| `zu`/`ZU` | `CSize`       | No — always exactly `CSize`                                |

```tl
f(x: CInt) { x }
f(42)                    // OK: 42 adapts to CInt
c_malloc(10zu)           // OK: 10zu is CSize
offset: CPtrDiff := -4z  // OK: -4z is CPtrDiff
```

A weak literal resolves to a concrete type when it meets one in unification. If unconstrained at the end of type inference, it defaults to `Int` (signed) or `UInt` (unsigned). The compiler checks that the literal value fits in the resolved type's range — for example, `256` resolving to `CInt8` is a compile-time error.

Weak literals do not resolve to standalone types (`CSize`, `CPtrDiff`, `CChar`). Use the `z`/`zu` suffixes or a type annotation instead.

### Let-in Expressions

Bindings can be introduced inline within parentheses:

```tl
res := (x := 42
        x - 10)     // x is scoped to the expression; res = 32
```

When a binding is the last expression in a block (no body follows), the block evaluates to the bound value:

```tl
foo() {
  result := compute()     // No body — equivalent to: result := compute(); result
}
```

> See [Language Model: Trailing binding shorthand](LANGUAGE_MODEL.md#trailing-binding-shorthand) for the conceptual explanation.

### Block Expressions

A block can be used as an expression by wrapping it in parentheses: `({ ... })`. The block's value is its final expression:

```tl
result := ({
  setup()
  compute()
  cleanup()
  42           // Block evaluates to 42
})
```

This is useful when you need to execute multiple statements in an expression context, such as in the middle of a larger expression or as an argument to a function:

```tl
total := base_cost + ({
  discount := calculate_discount()
  apply_tax(price - discount)
})
```

Block expressions combine naturally with binding expressions:

```tl
value := ({
  x := 10
  y := 20
  x * y        // Returns 200
})
```

### Block Statements

A bare `{ ... }` block used as a statement (not wrapped in parentheses) creates a new lexical scope without producing a value. Bindings declared inside the block are destroyed when the block exits and do not escape to the enclosing scope:

```tl
x := 1
{
  y := 2        // y is scoped to this block only
  x = x + y    // x from the outer scope is visible here
}
// y is not accessible here
```

This is useful when you need a temporary variable for a calculation but don't want it to pollute the surrounding scope:

```tl
buf := load_data()
{
  tmp := compute_checksum(buf)
  verify(tmp)
}
// tmp is gone; buf is still in scope
```

Unlike block expressions `({ ... })`, block statements have no value and cannot appear on the right-hand side of a binding.

### Expression Separators

Because whitespace is not significant in Tess, the parser can sometimes interpret adjacent expressions differently than intended. A comma (`,`) or semicolon (`;`) can be placed between expressions to explicitly mark where one expression ends and the next begins.

This is most commonly needed when a binding's right-hand side is followed by a parenthesized expression. Without a separator, the parser interprets the parenthesized expression as a function call:

```tl
// Problem: parsed as tag_len(tl & 0xF0zu) — a function call on tag_len
tl: CSize := s.small.tag_len
(tl & 0xF0zu) >> 4zu

// Solution with comma: marks the end of the binding expression
tl: CSize := s.small.tag_len,
(tl & 0xF0zu) >> 4zu

// Solution with semicolon: equivalent effect
tl: CSize := s.small.tag_len;
(tl & 0xF0zu) >> 4zu
```

The separator is only needed when the next expression starts with `(` and the previous expression ends with an identifier, which would otherwise be parsed as a function call. In most code, natural syntax (different starting tokens, explicit `return`, etc.) avoids the ambiguity entirely.

### Type Predicates

```tl
x :: Int            // true if x has type Int
```

### Attributes

Attributes annotate declarations with metadata that can be queried at compile time.

#### Declaring Attributes

Prefix a declaration with `[[name]]`:

```tl
[[my_attr]] x := 42
[[my_attr]] foo() { ... }
```

Multiple attributes use a comma-separated list:

```tl
[[attr1, attr2]] bar() { ... }
```

Attributes can take arguments:

```tl
[[NFA(42)]] nfa_func() { ... }
```

Attributes work on both top-level and local declarations:

```tl
main() {
    [[local_attr]] x := 123
}
```

#### Attribute Predicates

Use `::` with an attribute set on the right-hand side to test whether a symbol has a given attribute. Like type predicates, attribute predicates are evaluated at compile time and produce a boolean value.

```tl
sym :: [[my_attr]]          // true if sym has attribute my_attr
foo/0 :: [[my_attr]]        // use arity-mangled name for functions
```

For attributes with arguments, an exact match requires the same arguments:

```tl
nfa_func/0 :: [[NFA(42)]]  // true  - exact match
nfa_func/0 :: [[NFA(99)]]  // false - argument mismatch
```

A general match (attribute name without arguments) matches regardless of the declared arguments:

```tl
nfa_func/0 :: [[NFA]]      // true  - matches any NFA(...) attribute
```

## Control Flow

### If Expressions

`if` is an expression that returns a value:

```tl
result := if condition { expr1 } else { expr2 }
result := if a { 1 } else if b { 2 } else { 3 }
```

### Case/Match (Value Matching)

Pattern matching on values using equality by default. For tagged union destructuring, see [`when`](#pattern-matching-when-expression).

```tl
case n {
  0    { "zero" }
  1    { "one" }
  else { "other" }
}
```

#### Custom Predicates

By default, `case` uses `==` to compare the value against each pattern. You can provide a custom predicate function that takes two arguments (the value and the pattern) and returns a boolean.

**Using a named function:**

```tl
streq(a, b) { 0 == c_strcmp(a, b) }

result := case s, streq {
  "hello" { 0 }
  "world" { 1 }
  else    { 2 }
}
```

**Using an inline lambda:**

```tl
result := case s, (a, b) { 0 == c_strcmp(a, b) } {
  "hello" { 0 }
  "world" { 1 }
  else    { 2 }
}
```

In both forms, the predicate is called as `predicate(value, pattern)` for each arm until a match is found.

### While Loop

```tl
while condition {
  body
}

// With update expression
while i < 10, i = i + 1 {
  body
}
```

### For-in Loop

The `for` statement iterates over collections using a module-based iterator interface.
The iterator module is inferred from the collection's type via UFCS dispatch — any type
whose module implements the iterator interface can be used directly:

```tl
for x in collection {
  use(x)           // x is a copy of the current element
}

for x.& in collection {
  x.* = 0          // x is a pointer; can mutate the element
}
```

This works with `Array`, `Slice`, `String`, and any user-defined type that implements
the iterator interface in its module:

```tl
#import <Slice.tl>
s := arr.slice(0, 4)
for x in s { use(x) }         // Slice module inferred from type
```

#### Explicit Iterator Modules

When you need both the element and its index, use `Array.Indexed`:

```tl
for it in Array.Indexed xs {
  Print.println(f"index={it.index} value={it.value}")
}

for it.& in Array.Indexed xs {
  it.ptr.* = it.index * 2    // Modify element using pointer
}
```

#### Iterator Interface

Iterator modules must implement these functions:

| Function      | Signature                    | Purpose                                     |
|---------------|------------------------------|---------------------------------------------|
| `iter_init`   | `(Ptr[T]) -> Iter`           | Initialize iterator from collection pointer |
| `iter_value`  | `(Ptr[Iter]) -> TValue`      | Get current element value                   |
| `iter_ptr`    | `(Ptr[Iter]) -> Ptr[TValue]` | Get pointer to current element              |
| `iter_cond`   | `(Ptr[Iter]) -> Bool`        | Check if iteration should continue          |
| `iter_update` | `(Ptr[Iter]) -> Void`        | Advance to next element                     |
| `iter_deinit` | `(Ptr[Iter]) -> Void`        | Clean up iterator resources                 |

The `Iter` type can contain arbitrary fields accessible in the loop body (like `index` in `Array.Indexed`).

#### Desugaring

When no explicit module is specified, the compiler emits UFCS dot-calls and the module is
inferred from the collection's type during type inference:

```tl
iter := xs.iter_init()
while iter.iter_cond(), iter.iter_update() {
  x: Const := iter.iter_value()
  body
}
iter.iter_deinit()
```

When an explicit module is specified (`for x in Module xs { body }`), the compiler emits
module-qualified calls directly:

```tl
iter := Module.iter_init(xs.&)
while Module.iter_cond(iter.&), Module.iter_update(iter.&) {
  x: Const := Module.iter_value(iter.&)
  body
}
Module.iter_deinit(iter.&)
```

With `.&`, `iter_ptr` is called instead of `iter_value`.

Loop variables are implicitly `Const` — they cannot be reassigned within the loop body. For pointer iterators, the variable is `Const[Ptr[T]]`: the pointer itself cannot be reassigned, but the pointee can be mutated through it.

### Break and Continue

Use `break` to exit a loop early, and `continue` to skip to the next iteration:

```tl
while i < 10, i = i + 1 {
  if i == 5 { break }       // Exit loop when i reaches 5
  if i % 2 == 0 { continue } // Skip even numbers
  c_printf(c"%d\n", i)
}

for x in collection {
  if x < 0 { continue }     // Skip negative values
  if x > 100 { break }      // Stop at first value over 100
  process(x)
}
```

Both `break` and `continue` work in `while` and `for` loops.

### Defer

`defer` schedules a statement or block to execute when the enclosing scope is exited, whether by `break`, `continue`, `return`, or falling through to the end:

```tl
defer x = 5                // Single statement

defer {                    // Block form
    a := 1
    x = 10
}
```

**Execution order:** Multiple defers in the same scope execute in LIFO (last-in, first-out) order:

```tl
x := 0
while true {
    defer x = x * 10 + 1   // Executes third
    defer x = x * 10 + 2   // Executes second
    defer x = x * 10 + 3   // Executes first
    break
}
// x is 321
```

**Works with all exit paths:** Defers run before `break`, `continue`, and `return`:

```tl
// With break
while true {
    defer x = 5
    break          // defer runs, then loop exits
}

// With continue
while i < 3, i = i + 1 {
    defer sum = sum + 1
    continue       // defer runs each iteration
}

// With return
while true {
    defer x = 0
    return x       // x's value is captured, then defer runs
}
```

**Capture-first returns:** When `return` is used with a defer, the return value is captured *before* defers execute. This also applies to implicit returns at the end of a function body:

```tl
f() {
    x := 42
    defer x = 0
    x              // Returns 42, not 0
}
```

**Nested scopes:** Each scope has its own defers. Inner defers run when the inner scope exits, outer defers run when the outer scope exits:

```tl
while true {
    defer x = x + 1       // Runs when outer loop exits
    while true {
        defer x = x + 10  // Runs when inner loop exits
        break
    }
    break
}
// Both defers have run
```

### Return Statement

```tl
return value    // Early return from function
```

Use `return` for early exit from a function. Since Tess is expression-based, implicit returns (the last expression in a function body) are preferred for normal control flow.

### Try

`try` is a prefix operator for error propagation. It works on any two-variant tagged union: it unwraps the first variant (success) or returns early from the enclosing function with the second variant (error).

```tl
Result[T, E] : | Ok { v: T } | Err { e: E }

parse(input) -> Result[Data, Error] { ... }

process(input) -> Result[Data, Error] {
    data := try parse(input)    // unwraps Ok, or returns Err
    transform(data)
}
```

This is purely structural — no special type names are required. Any two-variant tagged union works:

```tl
Option[T] : | Some { v: T } | None

lookup(key) -> Option[Value] { ... }

fetch(key) -> Option[Value] {
    val := try lookup(key)    // unwraps Some, or returns None
    validate(val)
}
```

`try` composes inline:

```tl
data := try parse(try read_file(path))
```

The enclosing function's return type must be compatible — it must be the same two-variant tagged union (or a type whose second variant matches the error being propagated).

`try` fully unwraps to the inner value of the success variant:

```tl
// These are equivalent:
data := try parse(input)

tmp := parse(input)
ok: Ok := tmp else { return tmp }
data := ok.value
```

## Structs

### Definition

```tl
Point[a] : { x: a, y: a }              // Generic struct
Circle : { radius: Float }             // Concrete struct
Empty : { }                            // Empty struct
```

### Construction

```tl
p := Point(x = 1, y = 2)               // Named arguments required
```

### Field Access

```tl
p.x                                    // Read field
p.x = 10                               // Write field
```

### Uninitialized Fields

Use `void` to leave a field uninitialized during construction:

```tl
Buffer : { data: Ptr[Byte], size: Int, capacity: Int }

buf := Buffer(
  data = c_malloc(1024),
  size = void,         // Left uninitialized
  capacity = 1024
)
buf.size = 0           // Initialize later
```

This is useful when a field will be set immediately after construction or when working with low-level memory patterns.

### Value Semantics

Structs have C-like value semantics: binding with `:=` performs a **shallow copy** of the struct (equivalent to C struct assignment). Fields that are pointers to heap memory are copied as pointer values — the underlying memory is **not** duplicated.

This means types containing heap pointers — such as `String` (for strings longer than 14 bytes) or `Array` — share the underlying buffer after a copy. Freeing one copy invalidates the other:

```tl
a := String.from_cstr("a string longer than 14 bytes")
b := a            // shallow copy — a and b share the heap buffer
String.free(a.&)     // b is now dangling
```

To get an independent copy, use the type's copy/clone function:

```tl
b := String.copy(a)       // deep copy — independent buffer
c := Array.clone(arr)   // deep copy of array
```

### Nested Structs

Structs can contain nested struct definitions. Nested types are accessed using dot syntax (`Parent.Child`):

```tl
Outer[T] : {
  Inner : {
    value: T
  }
  contents: Inner
}

inner := Outer.Inner(value = 42)
outer := Outer(contents = inner)
outer.contents.value   // 42
```

Nested structs are desugared internally with double-underscore-separated names (e.g., `Outer__Inner`), but the user-facing syntax is always `Parent.Child`. This also works across modules:

```tl
#module Shapes
Canvas : {
  Layer : {
    name: CString
  }
  base: Layer
}

#module main
layer := Shapes.Canvas.Layer(name = "bg")
```

## Enums

```tl
Color : { Red, Green, Blue }           // Define enumeration
val := Color.Red                       // Access variant
```

Enums in a module are accessed with the module prefix:

```tl
#module Foo
Status : { Ok, Error }

#module main
s := Foo.Status.Ok
```

## Unions

```tl
Value : { | the_int: Int | the_float: Float }

v := Value(the_int = 42)
n := v.the_int
```

## Tagged Unions

```tl
Shape : | Circle    { radius: Float }
        | Square    { length: Float }
        | Empty
```

### Construction

All construction forms return the tagged union type.

**Unscoped positional:** Constructor functions take positional arguments:

```tl
s := Circle(2.0)              // returns Shape
n := Empty()                  // returns Shape
```

**Unscoped named:** Named arguments also return the tagged union:

```tl
s := Circle(radius = 2.0)    // returns Shape
```

**Scoped positional:** Prefix with the type name:

```tl
s := Shape.Circle(2.0)       // returns Shape
```

**Scoped named:** Prefix with the type name and use named arguments:

```tl
s := Shape.Circle(radius = 2.0)   // returns Shape
```

**Bare nullary variant sugar:** Variants with no fields (like `Empty` or `None`) can omit the parentheses. Bare `Empty` is promoted to `Empty()` automatically. Since nullary variants carry no data, the type must be inferrable from context (type annotation, function return type, if/else branch, etc.):

```tl
empty: Option[Int] := None    // type annotation provides context
opt := if x > 0 { Some(x) } else { None }  // Some branch provides context
```

From another module, prefix with the module name:

```tl
s := Foo.Circle(2.0)          // returns Foo.Shape
s := Foo.Shape.Circle(radius = 2.0)  // also returns Foo.Shape
```

### Pattern Matching (When Expression)

> See [Language Model: Pattern Matching and Scope](LANGUAGE_MODEL.md#pattern-matching-and-scope) for how arm scoping relates to binding expressions.

The `when` keyword provides tagged union pattern matching with type inference. The tagged union type is inferred from the scrutinee — no type annotation needed:

```tl
area := when s {
  c:  Circle { c.radius * c.radius * 3.14159  }
  sq: Square { sq.length * sq.length }
  e:  Empty  { 0.0 }
}
```

Variant names in the arms are resolved automatically from the inferred type's module scope. For example, if `s` is a `Foo.Shape`, you write `Circle` in the arm — not `Foo.Circle`:

```tl
circle := Foo.Circle(2.0)
area := when circle {
  c:  Circle { c.radius * c.radius * 3.14159 }
  sq: Square { sq.length * sq.length }
  e:  Empty  { 0.0 }
}
```

`when` expressions must be exhaustive: there must be one arm per
variant, or an `else` arm:

```tl
describe := when s {
  c: Circle { "circle" }
  else      { "not a circle" }
}
```

### Explicit Type Annotation (Case Expression)

When the tagged union type cannot be inferred from the scrutinee, use `case` with an explicit type annotation:

```tl
result := case opt: Option[Int] {
  s: Some { s.value }
  n: None { 0 }
}
```

This is needed when the scrutinee's type is ambiguous, such as inside a type predicate branch:

```tl
unwrap[T, U](opt_or_res, default: T) -> T {
    if opt_or_res :: Option[T] {
        case opt_or_res: Option[T] {
            s: Some { s.value }
            n: None { default }
        }
    } else if opt_or_res :: Result[T, U] {
        case opt_or_res: Result[T, U] {
            o: Ok { o.v }
            e: Err { default }
        }
    }
    else {
        _tl_fatal_("unwrap: invalid type")
    }
}
```

Prefer `when` when the type is known; use `case var: Type` when it isn't.

### Mutable when

```tl
when s.& {
  c:  Circle { c->radius *= 2.0  }
  sq: Square { sq->length * sq->length }
}
```

Use the `.&` suffix on the scrutinee to get pointers to each variant. This is the same syntax used to access mutable iterators with the `for` statement.

### Variant Binding

Also known as let-else. See [Language Model: Variant Binding](LANGUAGE_MODEL.md#variant-binding) for how this connects to binding expression scoping.

When you need a single variant's value for the rest of a scope, use a variant binding to unwrap it or diverge:

```tl
s: MySome := val else { return 0 }
// s is available for the rest of the scope
s.value + 1
```

The `else` block may either diverge (`return`, `break`, `continue`) or produce a value. When it produces a value, the overall expression evaluates to that value if the match fails — the continuation after the variant binding is not reached:

```tl
// Diverging: exit the function if no match
s: MySome := val else { return -1 }
s.value + 1

// Non-diverging: use a fallback value if no match
s: MySome := val else { 0 }
// if val was MyNone, the whole expression evaluated to 0
// if val was MySome, s is bound and execution continues here
```

This avoids trapping the unwrapped value inside a `when` arm when subsequent code needs it:

```tl
// Without variant binding — value is trapped inside the arm
when val {
    s: MySome { use(s.value) }
    n: MyNone { return 0 }
}
// can't use s here

// With variant binding — value available in the rest of the scope
s: MySome := val else { return 0 }
use(s.value)
```

For the conditional case (doing different things per variant), use `when` with `else`:

```tl
when val {
    s: MySome { s.value + 1 }
    else { fallback }
}
```

### Conditional Variant Binding

When you need to check for a single variant and execute a block only if it matches, use a conditional variant binding with `if`:

```tl
if c: Circle := shape {
    // c is bound to the Circle value
    use(c.radius)
}
// execution continues here if shape was not a Circle
```

An optional `else` clause handles the non-matching case:

```tl
if c: Circle := shape {
    use(c.radius)
}
else {
    handle_other()
}
```

This is not intended to replace `when` — use `when` when matching multiple variants. Conditional variant binding is useful when a single variant results in a special case, especially as an early-return guard:

```tl
if err: Err := result {
    return err.message
}
// continue with the success path
```

## Traits

Traits are compile-time type constraints that describe a set of functions a type must provide.
They use structural conformance — no `impl` blocks or explicit declarations are needed. A type
conforms to a trait if its module contains functions matching all of the trait's signatures.

### Trait Declaration

Traits are declared with a type parameter in square brackets. The body contains function
signatures (not implementations):

```tl
Eq[T] : {
    eq(a: T, b: T) -> Bool
}

Printable[T] : {
    to_string(a: T) -> CString
    print(a: T) -> CInt
}
```

The parser disambiguates traits from structs by the body content: `identifier(` signals
a function signature (trait), while `identifier:` signals a field (struct).

### Structural Conformance

A type conforms to a trait if its module has functions matching all signatures:

```tl
#module Vec

Vec3 : { x: Float, y: Float, z: Float }

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

// Vec3 now structurally conforms to Eq — no declaration needed.
```

Conformance does not require importing the trait. The trait only needs to be visible where
it is used as a bound.

### Trait Bounds

Trait bounds constrain generic type parameters:

```tl
sort[T: Ord](arr: Array[T]) { ... }
are_equal[T: Eq](a: T, b: T) -> Bool { a == b }
double[T: Add](x: T) -> T { x + x }
```

`T: Ord` means "T must satisfy the `Ord` trait." Bounds are checked when the generic
function is called with a concrete type — not at the definition site.

Multiple type parameters can have independent bounds:

```tl
compare_second[A, B: Eq](a: A, x: B, y: B) -> Bool { x == y }
```

**Bounds apply to functions, not type definitions.** Structs and tagged unions cannot have
bounded type parameters:

```tl
// NOT allowed:
SortedList[T: Ord] : { items: Array[T] }

// Instead, bound the functions:
SortedList[T] : { items: Array[T] }
insert[T: Ord](list: Ptr[SortedList[T]], item: T) { ... }
```

### Trait Inheritance

Traits can inherit from other traits. A type must satisfy all parent traits:

```tl
Ord[T] : Eq[T] {
    cmp(a: T, b: T) -> CInt
}
// A type satisfying Ord must have cmp. eq is derived from cmp if absent.
```

Combined traits with no additional functions use empty braces:

```tl
Numeric[T] : Add[T], Sub[T], Mul[T], Div[T] { }
```

Circular inheritance is rejected. Diamond inheritance is allowed — the compiler deduplicates
requirements.

Ad-hoc multi-bounds (`T: Add + Eq`) are not supported. Define a named combined trait instead:

```tl
Summable[T] : Add[T], Eq[T] { }
sum[T: Summable](arr: Array[T], zero: T) -> T { ... }
```

### Conditional Conformance

A generic type conditionally conforms to a trait when its conformance depends on its type
parameter. This is expressed through trait bounds on the implementing function:

```tl
#module Wrapper

Pair[A] : { fst: A, snd: A }

// Pair[T] satisfies Eq when T satisfies Eq
eq[T: Eq](a: Pair[T], b: Pair[T]) -> Bool {
    a.fst == b.fst && a.snd == b.snd
}
```

The compiler verifies conditional conformance transitively: `Pair[Int]` satisfies `Eq`
because `Int` satisfies `Eq`. `Pair[SomeType]` does not unless `SomeType` also satisfies `Eq`.

### Opting Out of Conformance

Structural conformance checks shape, not intent. A type can accidentally satisfy a trait
whose semantic contract it does not meet. Use `[[no_conform(Trait1, Trait2, ...)]]` on a
type definition to prevent it from satisfying the listed traits:

```tl
#module Graph

[[no_conform(Ord)]]
Node : { id: Int, edges: Array[Int] }

// This cmp exists for topological sorting — it is a partial order, not total.
// Node will NOT satisfy Ord despite having a matching cmp function.
cmp(a: Node, b: Node) -> CInt { ... }
```

If a generic function with bound `T: Trait` is called with a type that has
`[[no_conform(Trait)]]`, the compiler reports a trait bound error with a note explaining
that conformance was explicitly denied.

**Trait inheritance.** Blocking a trait does not automatically block its children or parents.
`[[no_conform(Ord)]]` blocks `Ord` but not `Eq`. However, blocking a parent trait makes
derived traits unsatisfiable: `[[no_conform(Eq)]]` prevents both `Eq` and `Ord` (since
`Ord` inherits `Eq`).

**Compiler-provided traits.** When a compiler-provided trait is blocked, using the
corresponding operator on that type is a compile-time error. `[[no_conform(Add)]]` means
`a + b` does not compile for that type, even though `add(a, b)` can still be called as a
regular function.

**Generic types.** For a generic type like `Pair[T]`, `[[no_conform(Eq)]]` means `Pair[T]`
never satisfies `Eq` regardless of whether `T` does — conditional conformance is suppressed
entirely.

**Attribute predicates.** `no_conform` is queryable:

```tl
Node :: [[no_conform(Ord)]]    // true
Node :: [[no_conform]]         // true (any no_conform present)
Node :: [[no_conform(Eq)]]     // false (only Ord is blocked)
```

### Compiler-Provided Traits

The compiler provides built-in traits for operator overloading. These are always visible and
do not need to be imported. User code cannot define types or traits with these names.

| Trait       | Function signature         | Operator             | Notes                                                 |
|-------------|----------------------------|----------------------|-------------------------------------------------------|
| `Add[T]`    | `add(a: T, b: T) -> T`     | `+`, `+=`            |                                                       |
| `Sub[T]`    | `sub(a: T, b: T) -> T`     | `-`, `-=`            |                                                       |
| `Mul[T]`    | `mul(a: T, b: T) -> T`     | `*`, `*=`            |                                                       |
| `Div[T]`    | `div(a: T, b: T) -> T`     | `/`, `/=`            |                                                       |
| `Mod[T]`    | `mod(a: T, b: T) -> T`     | `%`, `%=`            |                                                       |
| `BitAnd[T]` | `bit_and(a: T, b: T) -> T` | `&`, `&=`            | Bitwise AND                                           |
| `BitOr[T]`  | `bit_or(a: T, b: T) -> T`  | `\|`, `\|=`          | Bitwise OR                                            |
| `BitXor[T]` | `bit_xor(a: T, b: T) -> T` | `^`, `^=`            | Bitwise XOR                                           |
| `Shl[T]`    | `shl(a: T, b: T) -> T`     | `<<`, `<<=`          | Shift left                                            |
| `Shr[T]`    | `shr(a: T, b: T) -> T`     | `>>`, `>>=`          | Shift right                                           |
| `Eq[T]`     | `eq(a: T, b: T) -> Bool`   | `==`, `!=`           | `!=` is `!eq(a, b)`                                   |
| `Ord[T]`    | `cmp(a: T, b: T) -> CInt`  | `<`, `<=`, `>`, `>=` | Inherits from `Eq`; `eq` derived from `cmp` if absent |
| `Neg[T]`    | `neg(a: T) -> T`           | `-` (unary)          |                                                       |
| `Not[T]`    | `not(a: T) -> Bool`        | `!` (unary)          |                                                       |
| `BitNot[T]` | `bit_not(a: T) -> T`       | `~` (unary)          |                                                       |
| `Hash[T]`   | `hash(a: T) -> CSize`      | `x.hash()`           | Defined in `Hash.tl`; requires `#import <Hash.tl>`    |

**Hash trait:** Unlike the operator traits above (which are compiler builtins), `Hash` is
defined in the standard library (`Hash.tl`). Files that use `.hash()` or depend on the `Hash`
trait bound — including indirect use through `HashMap` — must `#import <Hash.tl>`:

```tl
#import <Hash.tl>       // Required: provides Hash trait + builtin hash implementations
#import <HashMap.tl>    // HashMap requires Hash to be imported by the caller

#module main
main() {
    n: Int := 42
    h := n.hash()                          // dot-call syntax call
    m := HashMap.create[Int, Int]()        // Works because Hash.tl is imported
    HashMap.set(m, 1, 100)
    HashMap.destroy(m)
    0
}
```

`Hash.tl` provides hash implementations for all builtin type families (`Int`, `UInt`,
`Float`, `Bool`, `CSize`, `CPtrDiff`) and `CString` (`Ptr[CChar]`). For user-defined types,
define `hash` in the type's module:

```tl
#module Point
hash(p: Point) -> CSize {
    hx := p.x.hash()
    hy := p.y.hash()
    hx ^ (hy * 0x100000001b3zu)
}
```

### Limitations

- **No dynamic dispatch.** Traits are purely compile-time constraints. There are no trait
  objects or vtables. Use tagged unions for runtime polymorphism.
- **No default implementations.** Trait bodies contain only signatures. The one exception
  is `eq`, which the compiler derives from `cmp` during conformance checking.
- **Single type parameter.** Traits have exactly one type parameter.
- **No ad-hoc multi-bounds.** `T: A + B` is not supported; define a named combined trait.
- **No associated types.** Use explicit type parameters instead.
- **Same-type operators only.** Both operands must be the same type `T`.
- **Accidental conformance.** Structural conformance means a type can unintentionally satisfy
  a trait. Mitigated by `[[no_conform(Trait)]]` — see *Opting Out of Conformance* above.

## C Interoperability

### Embedding C Code

```tl
#ifc
int add(int a, int b) { return a + b; }
#endc
```

### Declaring C Functions

Functions with the `c_` prefix map directly to C functions:

```tl
c_printf(fmt: CString, ...) -> CInt     // Declares printf
c_malloc(size: CSize) -> Ptr[any]       // Declares malloc
```

Use `Ptr[Const[T]]` for C functions that take `const` pointer parameters:

```tl
c_strlen(s: Ptr[Const[CChar]]) -> CSize           // const char*
c_strcmp(a: Ptr[Const[CChar]], b: Ptr[Const[CChar]]) -> CInt
```

To call a C function named `foo`, declare it as `c_foo` in Tess.

### Declaring C Symbols

```tl
c_INT_MAX: Int                         // C constant or variable
```

### Declaring C Struct Types

The `c_struct_` prefix transpiles directly to a C `struct` and is used
to annotate the types of the struct's fields. Interestingly, only the
fields used by the TL program need to be annotated.

```tl
c_struct_foo : { x: CInt, y: CInt }    // Annotate C struct layout
```

### Built-in Functions

```tl
sizeof[T]()         // Size of type T in bytes
sizeof(x)           // Size of value x in bytes
alignof[T]()        // Alignment of type T
alignof(x)          // Alignment of value x
```

### Compiler Intrinsics

These low-level intrinsics are used internally by the standard library:

```tl
_tl_sizeof_[T]()    // Type-parameterized sizeof (for generic code)
_tl_alignof_[T]()   // Type-parameterized alignof (for generic code)
_tl_fatal_(msg)     // Terminate with error message
```

The `_tl_sizeof_` and `_tl_alignof_` intrinsics differ from `sizeof`/`alignof` in that they work with type variables in generic functions:

```tl
allocate[a]() -> Ptr[a] {
  c_malloc(_tl_sizeof_[a]())   // Works with generic type parameter
}
```

### Exporting Functions to C (`c_export`)

The `[[c_export]]` attribute gives Tess functions stable C symbol names, making them callable from C code. When compiling with `tess lib`, a `.h` header file is automatically generated alongside the library. Use `tess lib --static` to produce a static archive (`.a`/`.lib`) instead of a shared library (`.so`/`.dll`).

The default export name is `Module_func` (module-qualified with `_` separator). For `#module main`, the module prefix is omitted. Use `[[c_export("name")]]` to specify a custom C symbol name.

```tl
#module MyLib

// Exports as MyLib_add
[[c_export]] add(x: CInt, y: CInt) -> CInt { x + y }

// Exports as "multiply" (custom name overrides module prefix)
[[c_export("multiply")]] mul(a: CInt, b: CInt) -> CInt { a * b }
```

Compiling with `tess lib mylib.tl -o libmylib.so` (or `tess lib --static mylib.tl -o libmylib.a`) produces both the library and `libmylib.h`:

```c
#ifndef LIBMYLIB_H
#define LIBMYLIB_H

#include <stddef.h>
#include <stdint.h>

void tl_init_mylib(void);

int MyLib_add(int, int);
int multiply(int, int);

#endif
```

The init function name is derived from the output path: `tess lib mylib.tl -o libmylib.so` produces `tl_init_mylib`. This works identically for static libraries (`-o libmylib.a`). The namespacing prevents symbol collisions when multiple Tess libraries are linked into the same program. The consumer must call this function before calling any exported functions.

**Type restrictions:** Only C-compatible types are allowed in `c_export` function signatures. The compiler rejects Tess-specific types like `String`, user structs, tagged unions, and enums. Allowed types include all `C*` types (`CInt`, `CChar`, `CSize`, etc.), `Int`, `Float`, `Bool`, `Void`, `Ptr[T]`, and `c_struct_*` types.

## Global Variables

Bindings at module scope are global variables:

```tl
#module Foo
counter := 0

increment() {
  counter = counter + 1
}
```

All global variables are thread-local. There are no non-thread-local variables.

## Mutual Recursion

Functions can call each other without forward declarations:

```tl
is_even(n) { if n == 0 { true  } else { is_odd(n - 1)  } }
is_odd(n)  { if n == 0 { false } else { is_even(n - 1) } }
```

## Recursive Types

Types can reference themselves through pointers:

```tl
Node[T] : { value: T, next: Ptr[Node[T]] }

n1 := Node(value = 1, next = null)
n2 := Node(value = 2, next = n1.&)
```

Mutually recursive types are also supported—types can reference each other:

```tl
Tree[T]   : { value: T, children: Ptr[Forest[T]] }
Forest[T] : { trees: Array.Array[Tree[T]] }

t1 := Tree(value = 1, children = null)
forest := Forest(trees = Array.with_capacity[Tree[Int]](16))
t1.children = forest.&
```

**Definition order matters.** A type used by value (as a struct field or tagged union variant field) must be defined before the type that references it. Forward references—using a type before it is defined—require `Ptr` indirection:

```tl
// ERROR: Inner is not yet defined when Outer is parsed
Outer : | OA { inner: Inner }
        | OB
Inner : | IA { value: Int }
        | IB

// OK: reorder so Inner is defined first
Inner : | IA { value: Int }
        | IB
Outer : | OA { inner: Inner }
        | OB

// OK: use Ptr for forward references
Outer : | OA { inner: Ptr[Inner] }
        | OB
Inner : | IA { value: Int }
        | IB
```

Note that functions do not have this restriction—they can call each other regardless of definition order (see [Mutual Recursion](#mutual-recursion)).

## Comments

```tl
// Single-line comment
```

## Naming Conventions

The standard library follows these conventions:

| Pattern               | Meaning                    | Example                          |
|-----------------------|----------------------------|----------------------------------|
| `lowercase_snake`     | Public functions           | `with_capacity`, `iter_init`     |
| `_leading_underscore` | Private/internal functions | `_bump_malloc`, `_find_bucket`   |
| `c_name`              | C function binding         | `c_malloc`, `c_printf`           |
| `c_struct_name`       | C struct type annotation   | `c_struct_timespec`              |
| `PascalCase`          | Types and modules          | `Array`, `Alloc`, `Point`        |
| `__double_underscore` | Special functions          | `__init` (module initialization) |
| `_tl_name_`           | Compiler intrinsics        | `_tl_sizeof_`, `_tl_fatal_`      |

Single-letter names like `a`, `b`, `T` are conventionally used for type parameters in generic definitions.

**Note:** Identifiers containing `__` (double underscore) are reserved for compiler name mangling and will
be rejected by the parser. The only exceptions are `__init` (module initialization) and `c_` prefixed C
interop symbols (e.g., `c__Exit`).
