# Glossary

Terms used throughout the Tess documentation. Where a term differs from established programming language terminology, the conventional name is noted.

---

**Allocated closure.** A closure that copies captured values onto the heap using `[[alloc, capture(...)]]`.
Unlike stack closures, allocated closures can be returned from functions, stored in structs, and outlive the
scope where they were created. See also: *stack closure*.

**Auto-collapse.** When a module defines a type with the same name as the module (or named `T`), the
compiler registers the bare module name as a type alias. This lets callers write `Point(x = 1, y = 2)`
instead of `Point.Point(x = 1, y = 2)`.

**Binding.** A name introduced with `:=`. Unlike a C variable declaration, a binding is an expression — it
has a value and opens a scope that extends to the end of the enclosing block. See also: *binding
expression*, *shadowing*.

**Binding expression.** The expression formed by `:=`, which binds a name and defines a scope (the body)
where that name is available. Known as a *let-in expression* in ML-family languages.

**Declaration type annotation.** A type annotation on a `:=` binding, written as `name: Type := value`. This
is also the universal cast syntax in Tess — there is no separate `as` keyword. Known as a *let-in type
annotation* in earlier documentation.

**Dot-call syntax.** Calling a free function with dot notation on its first argument: `v.length()` is
rewritten to `length(v)`. The compiler looks up the function in the module that defines the receiver's type.
Also known as *uniform function call syntax (UFCS)*.

**Exhaustive matching.** A `when` expression must handle every variant of a tagged union, either with an arm
per variant or with an `else` arm. The compiler enforces this.

**Module.** A namespace declared with `#module Name`. A module typically defines a single type and its
associated functions. Modules are the unit of operator resolution and dot-call dispatch. Not the same as a
file — a file may contain multiple modules.

**Monomorphization.** The process of generating specialized versions of generic functions and types for each
concrete type they are used with. Also called *specialization*. The result is zero-overhead generics — no
runtime dispatch.

**Scrutinee.** The value being matched in a `when` or `case` expression. In `when s { ... }`, `s` is the
scrutinee.

**Shadowing.** Reusing a name with `:=` creates a new binding that hides the previous one. The old binding
still exists — it is just no longer visible. This is not mutation.

**Specialization.** See *monomorphization*.

**Stack closure.** A closure that captures variables by reference and is allocated on the stack. Fast, but
cannot be returned from functions because the captured references would dangle. See also: *allocated
closure*.

**Structural conformance.** A type satisfies a trait if its module contains functions matching the trait's
signatures. No explicit `impl` block is needed. This is how traits are resolved in Tess.

**Submodule.** A module declared with a dotted name: `#module Outer.Inner` makes `Inner` a submodule of
`Outer`. The parent module must be declared first. The relationship is organizational — submodules are
independent namespaces.

**Tagged union.** A type with a fixed set of named variants, each of which may carry different data. Similar
to Rust's `enum`, ML's variant types, or a discriminated union. Matched with `when` (type-inferred) or
`case` (type-annotated).

**Trait.** A compile-time constraint on a type parameter. A trait declares a set of function signatures; a
type satisfies it through structural conformance. Traits are resolved at compile time — there is no runtime
dispatch. Similar to Haskell's type classes or Rust's traits, but with structural rather than nominal
conformance.

**Variant.** One of the alternatives in a tagged union. A variant may have named fields (like a struct) or
no fields at all (a nullary variant). Variants are constructed as functions: `Circle(radius = 2.0)`.

**Variant binding.** A binding that matches a value against a single tagged union variant, with a mandatory
`else` clause: `s: Some := val else { return 0 }`. If the match succeeds, the unwrapped value is bound for
the rest of the scope. Known as *let-else* in some languages.

**Weak integer literal.** An integer literal like `42` that has no fixed type. It adapts to context —
becoming `Int`, `CInt`, `CShort`, or any compatible integer type as needed. Use a type annotation or suffix
(`42u`, `42zu`) to force a specific type.
