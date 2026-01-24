# The Tess Language (TL)

A statically-typed programming language that transpiles to C11.

## Features

- **Expression-based** - Almost everything is an expression. Functions implicitly return their last expression—no `return` keyword needed. Control flow constructs like `if` and `case` produce values.

- **Type inference** - Hindley-Milner style inference means most type annotations are optional. The compiler figures out the types so you can focus on the logic.

- **Pattern matching** - `case` expressions for clean conditional logic, with exhaustiveness checking for tagged unions and support for custom predicates:
  ```tl
  result := case n {
    0    { "zero" }
    1    { "one" }
    else { "other" }
  }

  // Custom predicate for string comparison
  streq(a, b) { 0 == c_strcmp(a, b) }
  msg := case name, streq {
    "Alice" { "hello friend" }
    else    { "hello stranger" }
  }
  ```

- **Clear intent** - `:=` declares a new binding, `=` mutates an existing one. No confusion about whether you're creating or modifying.

- **Minimal syntax** - No semicolons. Colons always introduce types. Braces delimit blocks uniformly. Postfix pointer operators (`.&`, `.*`, `->`) read left-to-right like field access.

- **Generics** - Polymorphic functions and types with automatic specialization. Write once, use with any type:
  ```tl
  Vec2(T) : { x: T, y: T }   // Define a generic type

  vi := Vec2(1, 2)           // Vec2(Int)
  vf := Vec2(1.0, 2.0)       // Vec2(Float)
  ```

- **Function overloading** - Define multiple functions with the same name but different arities. The compiler resolves calls by argument count:
  ```tl
  add(x) { x }
  add(x, y) { x + y }
  add(x, y, z) { x + y + z }
  ```

- **Tagged unions** - Algebraic data types with exhaustive case matching:
  ```tl
  Shape : | Circle { radius: Float }
          | Square { length: Float }
  ```

- **Tail call optimization** - Guaranteed by the language. Write recursive algorithms without worrying about stack overflow.

- **C interoperability** - Seamless integration with C libraries. Directly `#include` headers and call C functions with the `c_` prefix.

## Example

```tl
#module main
#import <stdio.tl>

Point(a) : { x: a, y: a }

add(p1, p2) {
  Point(p1.x + p2.x, p1.y + p2.y)
}

main() {
  a := Point(1, 2)
  b := Point(3, 4)
  result := add(a, b)
  c_printf("(%d, %d)\n", result.x, result.y)

  af := Point(1.2, 2.3)
  bf := Point(3.4, 4.5)
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
./tess exe -o program
```

### Windows

Build from a Developer Command Prompt or Developer PowerShell using CMake:

```powershell
cmake -B out/build -S .
cmake --build out/build --config Release
ctest -C Release --test-dir out/build   # Run tests
```

## Project Status

This is a research project exploring what a minimal, C-like language might look like if Hindley-Milner-style type inference was added to it.

## Documentation

- **[Language Reference](docs/LANGUAGE_REFERENCE.md)** - Complete syntax guide
- **[Array Tutorial](src/tl/std/Array-tutorial.tl)** - Tutorial introduction to Tess through the Array implementation
- **[All Documentation](docs/)** - Type system, specialization, and compiler internals

## Standard Library

The standard library is located in [src/tl/std/](src/tl/std/) and includes:
- `Array.tl` - Generic dynamic array
- `Alloc.tl` - Memory allocation interface
- `stdlib.tl`, `stdio.tl`, `string.tl` - C standard library bindings

See [Array-tutorial.tl](src/tl/std/Array-tutorial.tl) for extensively commented example code.

## License

All rights reserved.
