# The Tess Language (TL)

An ML-flavoured systems language that transpiles to C.

## Features

- **Type inference** - Hindley-Milner style inference means most type annotations are optional.

- **Tagged unions** - Algebraic data types with exhaustive destructuring via `when`:
  ```tl
  Shape: | Circle { radius: Float }
         | Square { length: Float }

  area(s) {
    when s {
      c: Circle { c.radius * c.radius * 3.14 }
      s: Square { s.length * s.length }
    }
  }
  ```

- **Operator overloading** - Define `+`, `-`, `*`, `==`, `<`, and other operators for your types. `!=` is derived from `eq`, comparisons from `cmp`, and compound assignments like `+=` work automatically:
  ```tl
  #module Vec2
  T: { x: Int, y: Int }
  add(a, b) { T(x = a.x + b.x, y = a.y + b.y) }
  eq(a, b) { a.x == b.x && a.y == b.y }

  #module main
  a := Vec2(x = 1, y = 2)
  b := Vec2(x = 3, y = 4)
  c := a + b                // calls Vec2.add
  c == b                    // calls Vec2.eq
  ```

- **Traits** - Compile-time type constraints with structural conformance — no `impl` blocks needed. A type satisfies a trait if its module has matching functions. Supports inheritance, diamond inheritance, and conditional conformance:
  ```tl
  Summable[T] : Add[T], Eq[T] { }
  sum[T: Summable](a: T, b: T) -> T { a + b }
  ```

- **Generics** - Polymorphic functions and types with automatic specialization. When a module defines a type named `T` (or the same as the module), the bare module name works as a constructor — `Vec2(...)` instead of `Vec2.T(...)`:
  ```tl
  #module Vec2
  T[a]: { x: a, y: a }

  vi := Vec2(x = 1, y = 2)           // Vec2[Int]
  vf := Vec2(x = 1.0, y = 2.0)       // Vec2[Float]
  ```

- **Let-else** - Destructure a single tagged union variant into the current scope, with a mandatory `else` that diverges:
  ```tl
  s: MySome := val else { return 0 }
  // s is available for the rest of the scope
  s.v + 1
  ```

- **Option and Result** - Built-in tagged unions for nullable values and error handling:
  ```tl
  Option[T]:    | Some { v: T } | None
  Result[T, U]: | Ok   { v: T } | Err { v: U }
  ```

- **Try** - Error propagation for any two-variant tagged union. Unwraps the first variant or returns early with the second:
  ```tl
  data := try parse(try read_file(path))
  ```

- **Defer** - Schedule cleanup to run when leaving a scope, regardless of exit path (`break`, `continue`, `return`). Multiple defers execute in reverse order:
  ```tl
  while true {
      defer x = x + 1
      break              // defer runs before loop exits
  }
  ```

- **Uniform function call syntax** - Call any function with dot syntax on its first argument. `v.length_sq()` calls `length_sq(v)`. Struct fields take priority. Works with `->`, chaining, generics, and cross-module calls (`v.Mod.foo()`).

- **Expression-based** - Almost everything is an expression. Functions implicitly return their last expression—no `return` keyword needed. Control flow constructs like `if` and `case` produce values. No semicolons. Postfix pointer operators (`.&`, `.*`, `->`) read left-to-right like field access.

- **Case expressions** - `case` for value matching with support for custom predicates:
  ```tl
  result := case n {
    0    { "zero" }
    1    { "one" }
    else { "other" }
  }
  ```

- **Function overloading** - Define multiple functions with the same name but different arities. The compiler resolves calls by argument count:
  ```tl
  add(x) { x }
  add(x, y) { x + y }
  add(x, y, z) { x + y + z }
  ```

- **Packages** - Distribute reusable libraries as `.tlib` source archives. Declare dependencies in `package.tl`, and the compiler handles version verification, transitive resolution, and whole-program compilation.

- **Call C from Tess** - `#include` headers and call C functions directly with the `c_` prefix:
  ```tl
  #include <math.h>
  c_sqrt(x: CDouble) -> CDouble
  result := c_sqrt(2.0)
  ```

- **Call Tess from C** - Export functions with `[[c_export]]`—the compiler generates a `.h` header automatically:
  ```tl
  #module MyLib
  [[c_export]] add(x: CInt, y: CInt) { x + y }
  ```
  ```bash
  tess lib mylib.tl           # produces libmylib.so + libmylib.h
  tess lib --static mylib.tl  # produces libmylib.a + libmylib.h
  ```

- **Integer type safety** - Seven integer sub-chains with implicit widening within a chain and explicit narrowing via let-in annotation casts. Polymorphic integer literals adapt to context (`42` becomes any signed type, `42u` any unsigned):
  ```tl
  x: CShort := 1
  y: Int := x               // OK: implicit widening
  narrow: CInt := y         // OK: explicit narrowing cast
  ```

- **Conditional compilation** - `#ifdef`, `#ifndef`, `#define`, `#undef`, and `#endif` directives with `-D` command-line flag:
  ```tl
  #ifdef DEBUG
  log(msg) { c_printf("%s\n", msg)  void }
  #endif
  ```

- **380+ tests** - Unit tests for the compiler internals and integration tests for every language feature, including expected-failure tests that verify the compiler rejects invalid programs.

## Example

```tl
#module Point

T[a]: { x: a, y: a }

add(p1, p2) {
  T(x = p1.x + p2.x, y = p1.y + p2.y)
}

eq(p1, p2) {
  p1.x == p2.x && p1.y == p2.y
}

#module main

main() {
  a := Point(x = 1, y = 2)
  b := Point(x = 3, y = 4)
  c := a + b                  // operator overloading
  c_printf("(%d, %d)\n", c.x, c.y)

  if a != b { c_printf("different\n") }

  af := Point(x = 1.2, y = 2.3)
  bf := Point(x = 3.4, y = 4.5)
  cf := af + bf               // works for Point[Float] too
  c_printf("(%f, %f)\n", cf.x, cf.y)
  0
}
```

## Build

```bash
make -j              # Build the compiler
make -j test         # Run tests
```

### Quick Start

```bash
mkdir myproject && cd myproject
tess init                        # creates package.tl, src/main.tl
tess run                         # compile and execute
```

To produce a standalone binary instead:

```bash
tess exe -o myproject            # compile to an output file
```

### Windows

Build from a Developer Command Prompt or Developer PowerShell using CMake:

```powershell
cmake -GNinja -B out/build -S .
cmake --build out/build --config Release -j
ctest -C Release --test-dir out/build -j   # Run tests
```

## Project Status

This is a research project exploring what a minimal, C-like language might look like if Hindley-Milner-style type inference was added to it.

## Documentation

- **[Language Reference](docs/LANGUAGE_REFERENCE.md)** - Complete syntax guide
- **[Packages](docs/PACKAGES.md)** - Creating and consuming reusable `.tlib` libraries
- **[Type System](docs/TYPE_SYSTEM.md)** - Integer sub-chains, conversions, and type inference details
- **[Standard Library Reference](docs/STANDARD_LIBRARY.md)** - API reference for Array, Alloc, and other modules
- **[All Documentation](docs/)** - Specialization, name mangling, and compiler internals

## Standard Library

The standard library is located in [src/tl/std/](src/tl/std/) and includes:
- `Array.tl` - Generic dynamic array with sort, map, filter, reduce
- `Str.tl` - String type with small string optimization
- `Alloc.tl` - Memory allocation interface with bump allocator
- `HashMap.tl` - Robin Hood open-addressing hash map
- `builtin.tl` - Option, Result, and other built-in types
- `stdlib.tl`, `stdio.tl`, `string.tl` - C standard library bindings

## License

All rights reserved.
