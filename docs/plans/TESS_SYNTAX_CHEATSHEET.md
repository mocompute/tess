# Tess Syntax Cheat Sheet (for test writing)

## File Structure & Modules
```tess
#module main                    // Declare module (required)
#import <cstdint.tl>            // Import standard library

// Module organization: each file declares ONE module
// Multi-module file: use #module to switch
#module ModuleOne
...
#module ModuleTwo
...
```

## Functions & Main
```tess
fact(x) { if (x <= 1) { 1 } else { x * fact(x - 1) } }

main() {
    expr1
    expr2
    result  // Last expression is return value
}
```

Test pattern: `main()` returns Int, 0 = success, non-zero = failure.
Accumulate errors: `error += if cond { 0 } else { 1 }`

## Generics

### Generic Functions
```tess
id[T](x: T) -> T { x }
apply[T](f: T -> T, x: T) -> T { f(x) }
apply(id/1, 0)         // Pass function ref with arity suffix
double[T](x: T) -> T { x + x }
double(42)              // T inferred as Int
```

### Generic Structs
```tess
Point[a]: { x: a, y: a }
p := Point(x = 1, y = 0)
p.y
```

### Generic Tagged Unions
```tess
MyOption[a]: | Has { v: a }
             | Empty

value := Has(42)
result := when value {
    h: Has { h.v * 2 }
    e: Empty { 0 }
}
```

## Trait Bounds & Operator Overloading

### Define trait methods (structural conformance)
```tess
#module Vec
Vec3 : { x: Int, y: Int, z: Int }
eq(a: Vec3, b: Vec3) -> Bool { a.x == b.x && a.y == b.y && a.z == b.z }
add(a: Vec3, b: Vec3) -> Vec3 { Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z) }
```

### Use trait bounds
```tess
are_equal[T: Eq](a: T, b: T) -> Bool { a == b }
is_less[T: Ord](a: T, b: T) -> Bool { a < b }
double[T: Add](x: T) -> T { x + x }
```

Operators resolve by type: `a + b` calls `Vec.add(a, b)`.
Derived: `!=` auto-derived from `eq`.

## Closures & Lambdas

```tess
f := (a, b) { a + b }              // Two-arg lambda
g := () { expr }                   // Zero-arg lambda
h := (n) { x = x + n }            // Mutates captured variable
```

Lambdas capture by reference. Shadowing creates new binding.

```tess
x := 10
f := () { x }        // captures original x
x := 20              // shadow (new binding)
g := () { x }        // captures new x
f()  // 10
g()  // 20
```

### Function references
```tess
apply(id/1, 0)       // Pass function with arity suffix
f: () -> Int          // Function pointer types
g: (Int, Int) -> Int
```

## Tagged Unions & Pattern Matching

```tess
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

circle := Circle(2.0)
none := MyNone()

// Exhaustive match
area := when circle {
    c: Circle { c.radius * c.radius * 3.14159 }
    sq: Square { sq.length * sq.length }
    n: MyNone { 0.0 }
}

// Partial with else
area := when circle { c: Circle { c.radius } else { 0.0 } }

// Bail/early return
sa: Val := a else { return false }
```

## Type Aliases

```tess
#module Foo
Point[T]: {x: T, y: T}
Pt = Point[Int]       // Concrete alias
p := Foo.Pt(x = 10, y = 20)
```

## Function Aliases

```tess
#import <Print.tl>
println = Print.println   // Alias for cross-module function
println(42, " hello")     // Direct call (all arities available)
apply(println/1, "hi")    // Function reference via alias/N
```

## Weak Integer Literals

```tess
42       // Weak signed, defaults to Int
42u      // Weak unsigned, defaults to UInt
42z      // CPtrDiff
42zu     // CSize
```

Context resolves type:
```tess
take_cint(x: CInt) { x }
take_cint(42)      // 42 resolves as CInt
```

In generics:
```tess
first[T](x: T, y: T) -> T { x }
result := first(42, 99)    // Both weak → merge → default to Int
```

## Fail Tests

Fail tests live in test/fail/ and must be rejected by the compiler.
```tess
#module main
main() {
    x := 1u + 2  // FAIL: unsigned + signed cross-family
    x
}
```

## Control Flow

```tess
if cond { expr1 } else { expr2 }
while condition, update { body }
```

## Variables
```tess
x := expr          // New binding
x = new_value      // Mutate existing
```
