# Tess FAQ

## Historical notes

### Project origin

The Tess Language grew out of a small research project that began
with the question of whether it was feasible to combine the simplicity
and efficiency of C with the convenience of type-inferred languages
like ML. After several iterations on syntax and backend, Tess today uses
a C-like syntax (without semicolons) and uses C as an intermediate
language, relying on the system toolchain to compile to native code.

### About the name

Tess started life as "typed s-expressions," and the name Tess arose
naturally. Subsequently, I dropped s-expressions in favour of ML
syntax, and began using `tl` as the file extension. Subsequent to
that, I changed the style of syntax to be more like C, but lacked
creativity to change the name.

## Design decisions

### Why does Tess compile to C instead of LLVM?

Tess compiles to C rather than LLVM IR. If we view C as an
intermediate representation, it has several compelling properties. It
is an ISO-standardised language with decades of stability, whereas
LLVM IR offers no stability guarantees and often breaks between
releases. C has multiple independent native code backends: GCC, Clang,
MSVC, ICC, and others, each with mature optimization pipelines,
whereas LLVM is a single implementation. This means a language
targeting C inherits broad platform support, competitive optimizations
improved over decades, and a choice of toolchains without taking on LLVM
as a dependency, which is large, complex, and must be constantly
updated as its API shifts. The generated C is also human-readable,
which simplifies debugging the compiler itself. The tradeoff is less
fine-grained control over code generation, because this is outsourced
to the various C compilers. For Tess, the stability, portability, and
simplicity of targeting C outweigh this limitation.
