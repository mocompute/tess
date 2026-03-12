# Traits and Operator Overloading — Design Notes

> **Status: Implemented.** This document preserves design rationale and implementation
> notes not covered in the canonical references. For the user-facing specification, see
> [Language Reference: Traits](../LANGUAGE_REFERENCE.md#traits) and
> [Language Reference: Operator Overloading](../LANGUAGE_REFERENCE.md#operator-overloading).
> For a hands-on introduction, see the
> [Operator Overloading Tutorial](../tutorials/OPERATOR_OVERLOADING.md).

---

## Syntax Disambiguation

Traits, structs, and tagged unions all use `:` before their body. The parser disambiguates
by peeking at the body content:

| Form | Meaning | Determined by |
|------|---------|---------------|
| `Name : { x: T, y: T }` | Struct | Body has `identifier: Type` (fields) |
| `Name : { eq(a: T, b: T) -> Bool }` | Trait | Body has `identifier(...)` (signatures) |
| `Name : { }` | Struct | Empty body defaults to struct |
| `Name : \| V1 \| V2` | Tagged union | `:` followed by `\|` |
| `Name : Parent { ... }` | Trait with inheritance | `:` followed by identifier |

When `:` is followed by an identifier (not `{` or `|`), it is trait inheritance — the
parent trait list, with the body in braces after the last parent.

## Associated Types (Deferred)

Associated types (e.g., `Iterator[It] : { Item; next(it: Ptr[It]) -> Option[Item] }`)
are deferred to a future extension. They require projection types (`It.Item`) in the
type system — a significant addition that is not needed for the initial trait system.

Patterns that would use associated types can use explicit type parameters instead:

```tl
collect[It, Item](it: Ptr[It], next: (Ptr[It]) -> Option[Item]) -> Array[Item] { ... }
```

## Implementation Notes

### Compiler Pipeline Integration

No new compiler phases were needed. Traits hook into existing phases:

| Phase | What happens |
|-------|-------------|
| 2 — Load toplevels | Parse `ast_trait_definition` nodes, register in trait registry, flatten parent chains, detect circular inheritance. Compiler-provided traits pre-registered at `tl_infer_create`. |
| 3 — Generic inference | No changes. Existing operator constraints work for same-type overloads. |
| 5 — Specialization | Operator rewrite (user-defined type operands → NFA calls) and trait bound verification (conformance checking on concrete types). |

### Key Files

| File | Role |
|------|------|
| `src/tess/src/infer_specialize.c` | Operator rewrite + bound verification (Phase 5) |
| `src/tess/src/infer_constraint.c` | Trait registration (Phase 2) |
| `src/tess/src/infer.c` | Trait registry initialization, conformance checking |
| `src/tess/src/parser.c` | Trait declaration + bound parsing |
| `src/tess/include/ast.h` | `ast_trait_definition` node type |
| `src/tess/include/type.h` | Trait definition struct, `module` field on type constructor def |

### Operator Rewrite Strategy

Overloaded operators are rewritten to function calls (NFAs) during Phase 5, after types
are concrete. By the time code generation runs, operator nodes have been replaced — no
changes needed in `transpile.c`.

The rewrite transforms `ast_binary_op { left: a, right: b, op: "+" }` into
`ast_named_function_application { name: "Module__add__2", args: [a, b] }`.

For comparison derivation, `a < b` becomes a built-in `CInt` comparison:
`cmp(a, b) < 0`.

### Conformance Checking Algorithm

1. Collect all functions in the type's module (using arity-mangled names)
2. For each function in the trait (and all inherited traits), find a matching function
   by name and arity
3. Unify the trait signature against the actual function signature, substituting the
   trait's type parameter for the concrete type
4. If the matching function has trait bounds on its own type parameters, verify those
   bounds are satisfied (conditional conformance — recursive)
5. All functions must match for conformance

Special case: when looking up `eq` with arity 2, if not found, fall back to `cmp` with
arity 2 — equality is derivable from ordering.

### Edge Cases

- **Tagged unions**: Same module lookup rules as structs.
- **No ambiguous conformance**: Tess only supports function overloading by arity, not by
  type signature. A module cannot have two `eq` functions with the same arity, so
  conformance lookup always finds at most one candidate — no ambiguity resolution needed.
- **Empty traits**: `Marker[T] : { }` is not supported — empty braces without a parent
  list parse as a struct. Combined traits with parents and empty braces are supported.

## Comparison with Other Languages

### Rust — Traits

Rust traits are the closest relative. Key differences:

| Aspect | Rust | Tess |
|--------|------|------|
| Conformance | Explicit `impl Trait for Type` blocks | Structural — matching functions suffice |
| Dispatch | Static (monomorphized) + dynamic (`dyn Trait`) | Static only (no dynamic dispatch) |
| Orphan rule | Cannot impl foreign trait for foreign type | No restriction — structural conformance |
| Default methods | Supported in trait body | Not supported |
| Associated types | Yes, with `type Item = ...` in impl | Deferred |
| Multi-parameter | Yes (`trait Add<Rhs>`) | No — single type parameter only |
| Ad-hoc bounds | `T: A + B` | Must name the combination |
| `Self` keyword | Yes | No — explicit type parameter `T` |
| Syntax | `trait` keyword, `impl` blocks | Keyword-free, content-based parsing |

**Tradeoff: structural vs explicit conformance.** Rust's explicit `impl` blocks prevent
accidental conformance and serve as documentation. Tess's structural approach is lighter —
no boilerplate for simple cases — but means a type can accidentally satisfy a trait. In
practice this is rare because trait function names are specific (`eq`, `cmp`, `add`) and
signatures must match exactly. The benefit is that conformance works across module
boundaries without coordination: a type author doesn't need to know about every trait
their type might satisfy.

**Tradeoff: no dynamic dispatch.** Rust's `dyn Trait` enables heterogeneous collections
and runtime polymorphism. Tess uses tagged unions for this purpose, which are more explicit
and have known size. This is consistent with Tess's preference for predictable, C-like
code generation.

**Tradeoff: no default methods.** Rust traits can provide default implementations that
conforming types inherit. Tess requires each type to provide all functions explicitly,
with a special case: `eq` is derived from `cmp` during conformance checking, so `Ord`
types only need to define `cmp`. Beyond this, there are no default implementations.

### Haskell — Type Classes

Tess traits are inspired by Haskell's type classes but significantly simpler:

| Aspect | Haskell | Tess |
|--------|---------|------|
| Conformance | Explicit `instance` declarations | Structural |
| Multi-parameter | Yes (with extension) | No |
| Functional dependencies | Yes | No |
| Default methods | Yes | No |
| Dispatch | Dictionary-passing (runtime) or specialization | Monomorphized (compile-time) |
| Higher-kinded types | Yes (`Functor f`) | No |

**Tradeoff: no higher-kinded types.** Haskell's `Functor`, `Monad`, etc. abstract over
type constructors (`f a` where `f` is `Maybe`, `List`, etc.). Tess traits only abstract
over concrete types. This rules out expressing patterns like "any container that supports
map" generically, but avoids the complexity of higher-kinded polymorphism.

**Tradeoff: monomorphization vs dictionary-passing.** Haskell's type class dispatch
typically uses dictionary-passing (a hidden vtable argument), which supports polymorphic
recursion but has runtime overhead. Tess monomorphizes everything, generating specialized
code per type — zero runtime overhead but no polymorphic recursion and larger binaries.

### Go — Interfaces

Go interfaces are structurally typed like Tess traits:

| Aspect | Go | Tess |
|--------|-----|------|
| Conformance | Structural (method sets) | Structural (module functions) |
| Dispatch | Dynamic (interface values have vtables) | Static (monomorphized) |
| Generics | Type constraints (Go 1.18+) | Trait bounds on type parameters |
| Methods vs functions | Methods (receiver syntax) | Module-scoped functions |
| Associated types | No | Deferred |
| Inheritance | Embedding | Named trait inheritance |

**Key similarity: structural conformance.** Both Go and Tess check conformance by matching
function signatures rather than requiring explicit declarations. The difference is that Go
interfaces are primarily a dynamic dispatch mechanism (interface values carry a vtable),
while Tess traits are purely compile-time constraints erased after checking.

**Key difference: methods vs functions.** Go attaches methods to types via receiver syntax
(`func (v Vec3) Add(other Vec3) Vec3`). Tess uses module-scoped free functions. This
means Tess conformance is determined by the module a function lives in, not by receiver
type.

**Key difference: opt-out.** Go has no mechanism to prevent a type from satisfying an
interface. Tess provides `[[no_conform(Trait)]]` to explicitly deny conformance when
structural matching would produce a semantic mismatch (e.g., a partial-order `cmp`
accidentally satisfying `Ord`).

### Swift — Protocols

| Aspect | Swift | Tess |
|--------|-------|------|
| Conformance | Explicit (`: Protocol` on type or extension) | Structural |
| Associated types | Yes, with `associatedtype` keyword | Deferred |
| Conditional conformance | Yes (`extension Array: Eq where Element: Eq`) | Yes, via bounded generic functions |
| Default implementations | Yes (in protocol extensions) | No |
| Dynamic dispatch | Yes (existentials, `any Protocol`) | No |
| Retroactive conformance | Yes (extensions) | Yes (structural — automatic) |

**Tradeoff: conditional conformance syntax.** Swift uses dedicated `extension` syntax to
declare conditional conformance. Tess expresses it naturally through bounded generic
functions — `eq[T: Eq](a: Array[T], b: Array[T])` makes `Array[T]` conform to `Eq` when
`T` does. This is more implicit but requires no new syntax.

### C++ — Concepts (C++20)

| Aspect | C++ Concepts | Tess |
|--------|-------------|------|
| Checking | Structural (expression-based) | Structural (signature-based) |
| Error timing | At use site (better since C++20) | At use site |
| Operator overloading | Free-standing, unrestricted | Trait-based, same-type only |
| Dispatch | Static (templates) | Static (monomorphization) |
| Multi-type operators | Yes (`Vec3 * float`) | No |

**Key similarity: monomorphization.** Both generate specialized code per type instantiation.
Neither has runtime dispatch overhead for generic code.

**Tradeoff: constrained vs unconstrained.** Pre-C++20 templates are fully duck-typed —
any expression that compiles is accepted, with errors deep in template instantiation.
C++20 concepts add optional constraints. Tess trait bounds are always checked at the
call site, giving clearer errors, but are less flexible (no arbitrary expression checking).

### Summary of Tess Tradeoffs

**Benefits:**
- **No boilerplate.** No `impl` blocks, no `instance` declarations, no `: Protocol`. Types
  conform by having the right functions — nothing else to write.
- **Predictable codegen.** Monomorphization only, no vtables, no dictionary-passing. The
  generated C code is straightforward.
- **Keyword-free.** Traits use the same syntactic patterns as structs and tagged unions.
  The language stays small.
- **Simple mental model.** One type parameter, structural conformance, no default methods,
  no higher kinds. Easy to reason about what a trait bound requires.

**Costs:**
- **No dynamic dispatch.** Cannot abstract over "any type satisfying Trait" at runtime.
  Tagged unions fill this role but require enumerating variants upfront.
- **No mixed-type operators.** `vec * scalar` requires explicit function calls. Less
  ergonomic for mathematical code.
- **No default methods.** Every conforming type must provide all functions explicitly,
  except for the special case where `eq` is derived from `cmp` during conformance checking.
- **Accidental conformance.** A type with a function named `eq` taking two arguments of its
  type and returning `Bool` conforms to `Eq` whether intended or not. Mitigated by
  `[[no_conform(Trait)]]`, which explicitly denies conformance for specific traits.
- **No higher-kinded traits.** Cannot express `Functor`, `Monad`, or similar abstractions
  over type constructors.
