# The Tess Language (TL)

A statically-typed programming language that transpiles to C11.

## Features

- **Type inference** - Hindley-Milner style, most type annotations optional
- **Generics** - Polymorphic functions and types with automatic specialization
- **Lambdas and closures** - First-class functions with lexical
  capture and stack-based closures
- **Expression-based** - Almost everything is an expression, implicit
  returns
- **Tagged unions** - Algebraic data types and case exhaustiveness
  checking
- **Iterators** - iteration over collections with custom iterators
- **Modules** - simple namespacing
- **C interoperability** - Seamless integration with C libraries:
  directly `#include` and access C functions, structs and symbols.

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
./tess exe -I src/tl/std program.tl -o program
```

## Project Status

This is currently an experimental research project.

## Documentation

See [docs/](docs/) for language reference and compiler internals.

## License

All rights reserved.
