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
