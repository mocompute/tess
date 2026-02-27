# Tess Type System

Tess features a Hindley-Milner style type system with full type inference, parametric polymorphism (generics), and monomorphization. This document describes how the type system works.

## Overview

The Tess type system provides:

- **Type Inference** - Types are inferred from usage; explicit annotations are rarely required
- **Parametric Polymorphism** - Generic functions and types with type parameters
- **Monomorphization** - Generic code is specialized to concrete types at compile time
- **Structural Typing** - Types are compared by structure, not by name
- **C Interoperability** - Seamless integration with C types

## Type Inference

Tess uses constraint-based type inference. The compiler:

1. Assigns fresh type variables to all expressions
2. Collects constraints from the program structure
3. Solves constraints via unification
4. Produces concrete types for all expressions

### Inference in Action

```tl
add(a, b) { a + b }
```

The compiler infers:
- `a` and `b` must support `+`
- The return type equals the result type of `+`
- Final type: `(Int, Int) -> Int` or `(Float, Float) -> Float` depending on usage

### When Annotations Are Required

Type annotations are needed when:

1. **C function declarations** - The compiler cannot infer types across the FFI boundary:
   ```tl
   c_malloc(size: CSize) -> Ptr[any]
   c_printf(fmt: CString, ...) -> CInt
   ```

2. **Ambiguous pointer types** - When `c_malloc` or similar returns `Ptr[any]`:
   ```tl
   p : Ptr[Int] := c_malloc(sizeof[Int]() * 10)
   ```

3. **Functions only used through pointers** - Without a direct call site, the compiler cannot specialize:
   ```tl
   // Annotation required: malloc is only stored in a struct, never called directly
   malloc(count: CSize) -> Ptr[Int] {
     c_malloc(count)
   }
   ```

4. **Disambiguation** - When multiple types would be valid:
   ```tl
   x : CInt := 42    // Force CInt instead of Int
   ch := 'a'         // Character literal: CChar
   ```

## Generics (Parametric Polymorphism)

### Generic Types

Types can have type parameters, written in square brackets:

```tl
Point[a] : { x: a, y: a }           // One type parameter
Map[K, V] : { keys: Ptr[K], values: Ptr[V], size: Int }  // Multiple parameters
```

Type parameters are conventionally single lowercase letters (`a`, `b`, `T`, etc.).

### Generic Functions

Functions are automatically generic when they work with any type:

```tl
id(x) { x }                         // Inferred: (a) -> a
swap(a, b) { (b, a) }               // Inferred: (a, b) -> (b, a)
apply(f, x) { f(x) }                // Inferred: ((a) -> b, a) -> b
```

### Explicit Type Parameters

Generic functions declare type parameters using square brackets. Square brackets always denote type arguments; parentheses always denote value arguments:

```tl
empty[T]() -> Array[T] { ... }
with_capacity[T](n: Int) -> Array[T] { ... }
map[a, b](f: (a) -> b, arr: Array[a]) -> Array[b] { ... }

// Usage
arr := empty[Int]()
floats := with_capacity[Float](16)
```

## Monomorphization (Specialization)

Tess compiles generic code by **monomorphization**: creating specialized versions for each concrete type used.

### How It Works

```tl
id(x) { x }

main() {
  a := id(42)       // Creates id_Int: (Int) -> Int
  b := id(3.14)     // Creates id_Float: (Float) -> Float
  c := id("hello")  // Creates id_String: (CString) -> CString
}
```

The compiler generates three specialized versions of `id`, each with concrete types.

### Type Constructor Specialization

Generic structs are also specialized:

```tl
Point[a] : { x: a, y: a }

main() {
  p1 := Point(x = 1, y = 2)       // Creates Point_Int
  p2 := Point(x = 1.0, y = 2.0)   // Creates Point_Float
}
```

### Specialization at Call Sites

Specialization happens at each call site. A generic function called with different types produces multiple specialized versions:

```tl
first(pair) { pair.x }

main() {
  p1 := Point(x = 1, y = 2)
  p2 := Point(x = 1.0, y = 2.0)

  a := first(p1)    // Specializes first for Point[Int]
  b := first(p2)    // Specializes first for Point[Float]
}
```

### Specialization via Context

Generic functions are specialized based on their usage context. When a function is stored in a struct field with a concrete function type, that type provides the specialization:

```tl
// Generic identity function - no type annotations needed
identity(x) { x }

// The struct field type (Int) -> Int provides specialization context
Callbacks : { on_event: (Int) -> Int }

main() {
  cb := Callbacks(on_event = identity)  // identity specialized to (Int) -> Int
  cb.on_event(42)                       // Works: calls specialized version
}
```

Type annotations on functions are only required when there is no usage context to infer the concrete type.

## Type Aliases

Type aliases create shorthand names for types:

```tl
Pt = Point[Int]                                  // Simple alias
StringIntMap = Collections.Map[CString, Int]  // Fully specialized alias
```

Aliases are expanded at compile time. `Pt` and `Point[Int]` are the same type.

### Partial Specialization (Not Supported)

Tess does not support partially specialized type aliases:

```tl
// NOT SUPPORTED:
StringMap[V] = Map[CString, V]   // Error: partial specialization
```

Instead, use the full generic type or create a fully specialized alias.

## Type Constructors

User-defined types (structs, unions, enums) introduce **type constructors**.

### Built-in Type Constructors

`Ptr[T]` and `Const[T]` are built-in unary type constructors. `Const[T]` is a type qualifier primarily used inside `Ptr` to express read-only pointer access: `Ptr[Const[T]]`. The compiler enforces const correctness by rejecting mutation through const pointers and preventing implicit removal of const qualifiers.

### Struct Type Constructors

```tl
Point[a] : { x: a, y: a }
```

`Point` is a type constructor that takes one type argument. `Point[Int]` and `Point[Float]` are distinct concrete types.

### The Type Hierarchy

- **Type Variables** - Placeholders during inference (`t0`, `t1`, etc.)
- **Monotypes** - Concrete types without quantifiers (`Int`, `Point[Float]`)
- **Polytypes** - Type schemes with quantified variables (`forall a. a -> a`)

### Arrow Types (Function Types)

Function types use arrow notation:

```tl
(Int) -> Int                    // Function taking Int, returning Int
(Int, Int) -> Int               // Function taking two Ints
((a) -> b, a) -> b              // Higher-order function
```

### Tuple Types

Tuples group multiple types:

```tl
(Int, Float)                    // Pair
(Int, Float, String)            // Triple
```

Tuples are a feature of the type system, not the Tess language. They
are used to represent function parameter lists in arrow types.

## Constraint Satisfaction

The type checker generates and solves constraints:

### Constraint Types

1. **Equality constraints** - Two types must be the same
   ```tl
   x := y    // typeof(x) = typeof(y)
   ```

2. **Application constraints** - Function application
   ```tl
   f(x)      // typeof(f) = typeof(x) -> result
   ```

3. **Field constraints** - Struct field access
   ```tl
   p.x       // typeof(p) must have field x
   ```

### Unification

Constraints are solved by **unification** - finding substitutions that make types equal:

```tl
id(x) { x }
y := id(42)
```

Generates:
1. `id : t0 -> t1` (fresh type variables)
2. `x : t0`
3. Return type = `t1`
4. Body `x` means `t1 = t0`
5. Call `id(42)` means `t0 = Int`
6. Therefore: `id : Int -> Int` for this call site

### Occurs Check

The type checker prevents infinite types. When a function is used, the occurs check detects self-referential types:

```tl
f(x) { f }

main() {
  g := f(1)   // Error: would require t = t -> t (infinite type)
}
```

## Type Coercion

Tess performs limited implicit type coercion:

### Integer Types

Integer types are organized into **seven sub-chains**, each with a width
ordering. Implicit widening is only permitted within a single sub-chain,
from narrower to wider.

**C-Named Signed:** `CSignedChar < CShort < CInt < CLong < CLongLong (= Int)`

**C-Named Unsigned:** `CUnsignedChar < CUnsignedShort < CUnsignedInt < CUnsignedLong < CUnsignedLongLong (= UInt)`

**Fixed-Width Signed:** `CInt8 < CInt16 < CInt32 < CInt64`

**Fixed-Width Unsigned:** `CUInt8 < CUInt16 < CUInt32 < CUInt64`

**Standalone Types** (no implicit conversion): `CSize`, `CPtrDiff`, `CChar`

#### Implicit Widening

Within a sub-chain, narrower types implicitly widen to wider types. This
applies in all directed contexts — variable bindings, reassignment,
function arguments, and return values:

```tl
// Let-in binding
x : CInt := 42
y : Int := x             // OK: CInt → Int

// Reassignment
z : Int := 0
z = x                    // OK: CInt → Int

// Function arguments
take_int(n: Int) { n }
take_int(x)               // OK: CInt argument widens to Int parameter

// Return values
to_int(n: CInt) -> Int { n }  // OK: CInt return widens to Int
```

#### Explicit Narrowing and Cross-Chain Conversion

Narrowing (wide → narrow), cross-chain (e.g., signed ↔ unsigned, C-named ↔
fixed-width), and standalone type conversions require an explicit **let-in
type annotation**:

```tl
narrow : CInt := some_int_value       // Narrowing: Int → CInt
unsigned : UInt := some_int_value     // Cross-family: signed → unsigned
fixed : CInt32 := some_cint_value     // Cross-chain: C-named → fixed-width
size : CSize := some_uint_value       // Standalone: UInt → CSize
```

The let-in annotation is the only syntax for explicit conversion — there is
no `as` keyword or cast function. This makes every conversion point visually
prominent when scanning code.

#### Weak Integer Literals

Integer literals receive a **polymorphic (weak) type** that adapts to context:

- `42` — weak signed, resolves to any signed integer type; defaults to `Int`
- `42u` — weak unsigned, resolves to any unsigned integer type; defaults to `UInt`
- `42z` — concrete `CPtrDiff` (not polymorphic)
- `42zu` — concrete `CSize` (not polymorphic)

```tl
f(x: CInt) { x }
f(42)                    // OK: literal 42 adapts to CInt
c_malloc(10zu)           // OK: 10zu is CSize
```

When a weak literal resolves to a concrete type, the compiler verifies the
literal value fits in that type's range. For example, `256` resolving to
`CInt8` is a compile-time error.

#### Exact Match in Generics and Operators

Type variables require exact type match — no implicit widening through generics:

```tl
f(x: T, y: T) -> T { x + y }

a : CInt := 1
b : CShort := 2
f(a, b)              // Error: T bound to CInt, CShort != CInt
```

Arithmetic and comparison operators are typed as `(T, T) -> T`, requiring
both operands to be the same concrete type. Conditional (`if`) and `case`
expressions also require all branches to have the same type.

#### Debug Bounds Checking

In debug builds, narrowing conversions emit a runtime bounds check before the
cast. If the value does not fit in the target type, the program aborts with
a diagnostic message.

### Pointer Types

Pointers can be implicitly cast:
```tl
p : Ptr[Int] := c_malloc(...)
q : Ptr[Byte] := p  // Implicit cast
```

### Const Pointer Coercion

`Ptr[T]` implicitly coerces to `Ptr[Const[T]]` (adding const is safe):

```tl
read(p: Ptr[Const[Int]]) { p.* }

main() {
  p : Ptr[Int] := c_malloc(8)
  p.* = 42
  val := read(p)     // OK: Ptr[Int] -> Ptr[Const[Int]]
}
```

The reverse is rejected — `Ptr[Const[T]]` does not coerce to `Ptr[T]`:

```tl
write(p: Ptr[Int]) { p.* = 10  0 }

pass(p: Ptr[Const[Int]]) {
  write(p)            // Error: cannot strip const
}
```

Const stripping is also rejected through nested pointer levels:
`Ptr[Ptr[Const[T]]]` cannot coerce to `Ptr[Ptr[T]]`.

In the generated C code, `Ptr[Const[T]]` transpiles to `const T*`.

### Const and Generic Type Parameters

`Const[T]` cannot currently be used with generic type parameters. When a generic function uses the same type variable `T` in both a `Ptr[T]` and a `Ptr[Const[T]]` position, unification fails because `T` would need to be both `X` and `Const[X]` simultaneously:

```tl
// Does NOT work — T unifies to conflicting types:
c_memcpy(dst: Ptr[T], src: Ptr[Const[T]], n: CSize) -> Ptr[T]

// Works — use Ptr[T] for both when T is generic:
c_memcpy(dst: Ptr[T], src: Ptr[T], n: CSize) -> Ptr[T]

// Works — Const is fine with concrete types:
c_strcmp(s1: Ptr[Const[CChar]], s2: Ptr[Const[CChar]]) -> CInt
```

This is why the standard library `mem*` bindings (`c_memcpy`, `c_memmove`, `c_memcmp`, `c_memchr`) use `Ptr[T]` without `Const`, while string functions that use concrete `CChar` types include `Const` where the C headers specify `const`.

### No Implicit Float/Integer Coercion

`Int` and `Float` are not implicitly convertible:
```tl
x : Int := 42
y : Float := x      // Error: conflicting types Float versus Int
```

To convert between `Int` and `Float`, use the `Unsafe` module:
```tl
#import <Unsafe.tl>
x : Float := 3.7
y := Unsafe.float_to_int(x)   // y is Int
```

Integer literals are always integer-typed, not `Float`:
```tl
x : Float := 0      // Error: 0 is an integer literal, not Float
x : Float := 0.0    // OK: 0.0 is Float
```

## Recursive Types

Types can reference themselves through pointers:

```tl
Node[a] : { value: a, next: Ptr[Node[a]] }

// Usage
n1 := Node(value = 1, next = null)
n2 := Node(value = 2, next = n1.&)
```

### Mutually Recursive Types

Types can reference each other:

```tl
Tree[a] : { value: a, children: Ptr[Forest[a]] }
Forest[a] : { trees: Ptr[Tree[a]], count: Int }
```

## Polymorphic Recursion

Functions can call themselves with different type arguments:

```tl
// Mutually recursive functions with polymorphic types
is_even(n) { if n == 0 { true  } else { is_odd(n - 1)  } }
is_odd(n)  { if n == 0 { false } else { is_even(n - 1) } }
```

Both functions are specialized to `(Int) -> Bool`.

## The `any` Type

The `any` type represents an unknown pointer type, used primarily for C interop:

```tl
c_malloc(size: CSize) -> Ptr[any]
```

`Ptr[any]` can be assigned to any pointer type:
```tl
p : Ptr[Int] := c_malloc(...)     // Ptr[any] -> Ptr[Int]
q : Ptr[Point[Float]] := c_malloc(...)
```

## Type Predicates

Use `::` to test a type. The `::` operator is a binary predicate.

```tl
x := 42
x :: Int    // true if x has type Int

y := get_value()
y :: Point[Int]
y
```

Type predicates are checked at compile time and produce an expression
with a boolean value.

### Attribute Predicates

The `::` operator also serves as an **attribute predicate** when the right-hand side is an attribute set (`[[...]]`) instead of a type. Attribute predicates test whether a symbol was declared with a given attribute.

```tl
[[my_attr]] x := 42
x :: [[my_attr]]        // true
x :: [[other]]          // false
```

For attributes with arguments (e.g., `[[NFA(42)]]`), two matching modes apply:

- **Exact match**: `sym :: [[NFA(42)]]` — matches only if the attribute has the same arguments.
- **General match**: `sym :: [[NFA]]` — matches any `NFA(...)` attribute regardless of arguments.

Like type predicates, attribute predicates are resolved at compile time and produce a boolean value.

## Void and Unit

- `Void` - The type of expressions with no value (statements)
- `void` - The void value (used to explicitly return nothing)

```tl
print(x) {
  c_printf("%d\n", x)
  void    // Explicit void return
}
```

Functions without a meaningful return value have type `(...) -> Void`.

## Summary

The Tess type system provides:

| Feature | Description |
|---------|-------------|
| Inference | Types deduced from usage |
| Generics | Parametric polymorphism with type variables |
| Monomorphization | Generic code specialized at compile time |
| Structural typing | Types compared by structure |
| Constraint solving | Unification-based type checking |
| C interop | Seamless integration with C types |

The combination of type inference and monomorphization means you get the safety of static types with the convenience of dynamic-feeling code, while generating efficient specialized code.
