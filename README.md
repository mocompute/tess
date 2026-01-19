# Tess

A statically-typed programming language that transpiles to C.

## Features

- **Type inference** - Hindley-Milner style, no explicit types required
- **Generics** - Polymorphic functions and types with automatic specialization
- **Lambdas and closures** - First-class functions with lexical capture
- **Expression-based** - Everything is an expression, implicit returns
- **C interoperability** - Seamless integration with C libraries

## Example

```tl
#module main

Point(a) : { x: a, y: a }

add(p1, p2) {
  Point(p1.x + p2.x, p1.y + p2.y)
}

main() {
  a := Point(1, 2)
  b := Point(3, 4)
  result := add(a, b)
  c_printf("(%d, %d)\n", result.x, result.y)
  0
}
```

## Build

```bash
make -j              # Build the compiler
make -j test         # Run tests
./tess exe -I src/tl/std program.tl -o program
```

## Project Status

This is currently an experimental research project.

## Documentation

See [docs/](docs/) for language reference and compiler internals.

## License

All rights reserved.
