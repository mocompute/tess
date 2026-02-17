# The Tess Language (TL)

An ML-flavoured systems language that transpiles to C.

## Features

- **Expression-based** - Almost everything is an expression. Functions implicitly return their last expression—no `return` keyword needed. Control flow constructs like `if` and `case` produce values.

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

- **Generics** - Polymorphic functions and types with automatic specialization.
  ```tl
  Vec2[T]: { x: T, y: T }   // Define a generic type

  vi := Vec2(x = 1, y = 2)           // Vec2[Int]
  vf := Vec2(x = 1.0, y = 2.0)       // Vec2[Float]
  ```

- **Minimal syntax** - No semicolons. Colons always introduce types. Braces delimit blocks uniformly. Postfix pointer operators (`.&`, `.*`, `->`) read left-to-right like field access.

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
  tess lib mylib.tl  # produces libmylib.so + libmylib.h
  ```

- **250+ tests** - Unit tests for the compiler internals and integration tests for every language feature, including expected-failure tests that verify the compiler rejects invalid programs.

## Example

```tl
#module main
#import <stdio.tl>

Point[a]: { x: a, y: a }

add(p1, p2) {
  Point(x = p1.x + p2.x, y = p1.y + p2.y)
}

main() {
  a := Point(x = 1, y = 2)
  b := Point(x = 3, y = 4)
  result := add(a, b)
  c_printf("(%d, %d)\n", result.x, result.y)

  af := Point(x = 1.2, y = 2.3)
  bf := Point(x = 3.4, y = 4.5)
  result := add(af, bf)
  c_printf("(%f, %f)\n", result.x, result.y)
  0
}
```

## Build

```bash
make -j              # Build the compiler
make -j test         # Run tests

# build an executable
./tess exe main.tl -o program
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
- **[Array Tutorial](src/tl/std/Array-tutorial.tl)** - Tutorial introduction to Tess through the Array implementation
- **[Standard Library Reference](docs/STANDARD_LIBRARY.md)** - API reference for Array, Alloc, and other modules
- **[All Documentation](docs/)** - Type system, specialization, and compiler internals

## Standard Library

The standard library is located in [src/tl/std/](src/tl/std/) and includes:
- `Array.tl` - Generic dynamic array
- `Alloc.tl` - Memory allocation interface
- `stdlib.tl`, `stdio.tl`, `string.tl` - C standard library bindings

See [Array-tutorial.tl](src/tl/std/Array-tutorial.tl) for extensively commented example code.

## License

All rights reserved.
