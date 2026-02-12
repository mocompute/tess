# Tess Documentation

This directory contains technical documentation for the Tess programming language and compiler.

## Documents

| Document | Description |
|----------|-------------|
| [LANGUAGE_REFERENCE.md](LANGUAGE_REFERENCE.md) | Language syntax reference covering modules, types, variables, functions, control flow, and C interoperability |
| [TYPE_SYSTEM.md](TYPE_SYSTEM.md) | Conceptual overview of the Hindley-Milner type system, type inference, and generics |
| [SPECIALIZATION.md](SPECIALIZATION.md) | Deep dive into generic specialization (monomorphization) in the compiler |
| [TAGGED_UNIONS.md](TAGGED_UNIONS.md) | Technical documentation on tagged union (sum type) implementation |
| [NAME_MANGLING.md](NAME_MANGLING.md) | Name mangling system for arity overloading, modules, and specialization |
| [ALPHA_CONVERSION.md](ALPHA_CONVERSION.md) | Variable renaming system that ensures type safety across specializations |
| [PACKAGES.md](PACKAGES.md) | Package system for distributing reusable `.tlib` libraries |
| [STANDARD_LIBRARY.md](STANDARD_LIBRARY.md) | Standard library API reference (Array, Alloc, Unsafe, C bindings) |
| [FAQ.md](FAQ.md) | Historical background, motivations and miscellaneous |

## Suggested Reading Order

**For language users:**
1. [LANGUAGE_REFERENCE.md](LANGUAGE_REFERENCE.md) - Learn the language syntax
2. [TYPE_SYSTEM.md](TYPE_SYSTEM.md) - Understand how types work
3. [STANDARD_LIBRARY.md](STANDARD_LIBRARY.md) - Standard library API reference
4. [PACKAGES.md](PACKAGES.md) - Creating and consuming reusable libraries

**For compiler contributors:**
1. [TYPE_SYSTEM.md](TYPE_SYSTEM.md) - Understand the type system concepts
2. [SPECIALIZATION.md](SPECIALIZATION.md) - Learn how generics are monomorphized
3. [ALPHA_CONVERSION.md](ALPHA_CONVERSION.md) - Variable renaming for type safety
4. [TAGGED_UNIONS.md](TAGGED_UNIONS.md) - Understand sum type implementation
5. [NAME_MANGLING.md](NAME_MANGLING.md) - Learn identifier transformation for C output

## See Also

- [CLAUDE.md](../CLAUDE.md) - Build commands and project architecture overview
- [src/tl/std/](../src/tl/std/) - Standard library source (especially `Array.tl` for documented examples)
- [src/tess/tl/](../src/tess/tl/) - Test files demonstrating language features
