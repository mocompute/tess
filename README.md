# The Tess Language

A statically-typed language that transpiles to C.

Tess adds type inference, generics, and tagged unions to a C-like foundation. It transpiles to plain C: you keep your toolchain, your debugger, and your performance.

```tl
#module Point

Point[T]: { x: T, y: T }

add(p1, p2) {
    Point(x = p1.x + p2.x, y = p1.y + p2.y)
}

eq(p1, p2) {
    p1.x == p2.x && p1.y == p2.y
}

#module main
#import <Print.tl>

println = Print.println

main() {
    a := Point(x = 1, y = 2)
    b := Point(x = 3, y = 4)
    c := a + b
    println("(", c.x, ", ", c.y, ")")

    if a != b { println("different") }
    0
}
```

`add` and `eq` have no type annotations: the compiler infers them. `Point` is generic: the same definition works for integers, floats, or any type with `+` and `==`.

## Features

**Type inference.** Write the function, the compiler determines the types. No forward declarations, no header files.

```tl
factorial(n) {
    if n <= 1 { 1 }
    else { n * factorial(n - 1) }
}
```

**Tagged unions.** Define variants with associated data. The compiler checks that every case is handled.

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

**Generics.** One definition, multiple types, no macros. Type parameters can be constrained by traits: checked at compile time, specialized to concrete types.

```tl
Summable[T]: Add[T], Eq[T] { }

sum[T: Summable](a, b) { a + b }
```

**C interop.** Include a C header and call its functions directly. Export Tess functions back to C: the compiler generates the `.h` for you.

```tl
#include <math.h>
c_sqrt(x: CDouble) -> CDouble

result := c_sqrt(2.0)
```

```tl
[[c_export]] add(x: CInt, y: CInt) { x + y }
```

```bash
tess lib mylib.tl    # produces libmylib.so + libmylib.h
```

**Error handling.** `Result` and `Option` replace error codes and null checks. `try` unwraps the success case or returns the error: no goto chains.

```tl
read_config(path) {
    content := try read_file(path)
    config  := try parse(content)
    Ok(config)
}
```

Tess also has closures, defer, operator overloading, function overloading, dot-call syntax, iterators,
conditional compilation, and a package manager.

## Build

```bash
make -j              # Build the compiler (~5 seconds)
make -j test         # Run tests (~30 seconds)
make install         # Install to /usr/local (see docs/BUILD.md)
```

## Quick Start

Run a single file: no project setup needed:

```bash
echo '#module main
#import <Print.tl>
main() { Print.println("Hello, world!"), 0 }' > hello.tl
tess run hello.tl
```

For multi-file projects:

```bash
tess init            # creates package.tl, src/main.tl
tess run             # compile and execute
tess exe -o myapp    # standalone binary
```

## Documentation

- [Language Reference](docs/LANGUAGE_REFERENCE.md): complete syntax guide
- [Language Model](docs/LANGUAGE_MODEL.md): scoping, closures, and semantics
- [Type System](docs/TYPE_SYSTEM.md): inference, generics, and integer types
- [Standard Library](docs/STANDARD_LIBRARY.md): Array, String, HashMap, and more
- [Packages](docs/PACKAGES.md): creating and consuming `.tpkg` libraries
- [Building](docs/BUILD.md): build configurations, installation, and standalone binary
- [Glossary](docs/GLOSSARY.md): definitions of Tess-specific terms

## Examples

The [examples directory](src/tl/examples/) contains complete projects demonstrating C interop, command-line handling, and shared/static libraries.

## License

[Apache License 2.0](LICENSE)
