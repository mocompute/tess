# Tess Type System — How Inference Works

The compiler infers types from usage, so most code needs no annotations. Generic functions and types are specialized to concrete types at compile time: no runtime dispatch, no overhead.

This document explains how the compiler reasons about types — the internal machinery of inference, unification, and specialization. For syntax and usage, see the [Language Reference](LANGUAGE_REFERENCE.md). For the conceptual model of bindings and scoping, see the [Language Model](LANGUAGE_MODEL.md).

## Type Inference

The compiler determines types by looking at how values are used. Write `add(a, b) { a + b }` and the compiler figures out that `a` and `b` must support `+` and infers the return type accordingly.

### Inference in Action

```tl
add(a, b) { a + b }
```

The compiler infers:
- `a` and `b` must support `+`
- The return type equals the result type of `+`
- Final type: `(Int, Int) -> Int` or `(Float, Float) -> Float` depending on usage

## The Type Hierarchy

The type system has three levels:

- **Type Variables** — Placeholders during inference (`t0`, `t1`, etc.)
- **Monotypes** — Concrete types without quantifiers (`Int`, `Point[Float]`)
- **Polytypes** — Type schemes with quantified variables (`forall T. T -> T`)

### Arrow Types (Function Types)

Function types use arrow notation:

```tl
(Int) -> Int                    // Function taking Int, returning Int
(Int, Int) -> Int               // Function taking two Ints
((T) -> U, T) -> U              // Higher-order function
```

Tuples in arrow types represent function parameter lists — they are a feature of the type system, not the language itself.

## Constraint Satisfaction

The type checker generates and solves constraints:

### Constraint Types

1. **Equality constraints** — Two types must be the same
   ```tl
   x := y    // typeof(x) = typeof(y)
   ```

2. **Application constraints** — Function application
   ```tl
   f(x)      // typeof(f) = typeof(x) -> result
   ```

3. **Field constraints** — Struct field access
   ```tl
   p.x       // typeof(p) must have field x
   ```

### Unification

Constraints are solved by **unification** — finding substitutions that make types equal:

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
6. Therefore: `id: Int -> Int` for this call site

### Occurs Check

The type checker prevents infinite types. When a function is used, the occurs check detects self-referential types:

```tl
f(x) { f }

main() {
  g := f(1)   // Error: would require t = t -> t (infinite type)
}
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
Point[T] : { x: T, y: T }

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

## Polymorphic Recursion

Functions can call themselves with different type arguments:

```tl
// Mutually recursive functions with polymorphic types
is_even(n) { if n == 0 { true  } else { is_odd(n - 1)  } }
is_odd(n)  { if n == 0 { false } else { is_even(n - 1) } }
```

Both functions are specialized to `(Int) -> Bool`.

## Further Reading

- [Language Reference](LANGUAGE_REFERENCE.md) — Complete syntax reference including type annotations, generics, traits, and conversions
- [Language Model](LANGUAGE_MODEL.md) — Bindings, scoping, closures, and pattern matching
- [Specialization](SPECIALIZATION.md) — Detailed monomorphization pipeline documentation
