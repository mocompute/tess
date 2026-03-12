# Trait Conformance Opt-Out

> **Status: Proposed.** Documentation-only for now; implementation to follow.

## Problem

Tess uses structural trait conformance: a type satisfies a trait if its module has functions
matching all signatures. This is lightweight and boilerplate-free, but means a type can
accidentally conform to a trait whose semantic contract it does not satisfy.

### Why this matters

Structural conformance checks *shape*, not *intent*. A function with the right name, arity,
and signature is enough — the compiler cannot verify that the function satisfies the trait's
semantic contract.

**Example: partial order masquerading as total order.**

```tl
#module Graph

Node : { id: Int, edges: Array[Int] }

// Topological comparison — a partial order (not all nodes are comparable).
// But this signature matches Ord's `cmp`, so Node accidentally satisfies Ord.
cmp(a: Node, b: Node) -> CInt { ... }
```

A generic `sort[T: Ord](arr: Array[T])` would accept `Array[Node]` and compile without
error, but produce incorrect results because topological comparison is not a total order.

**Example: approximate equality.**

```tl
#module Sensor

Reading : { value: Float, timestamp: Int }

// "Close enough" comparison for deduplication — not true equality.
// But this satisfies Eq, so Reading can be used anywhere Eq is required.
eq(a: Reading, b: Reading) -> Bool {
    abs(a.value - b.value) < 0.001
}
```

This `eq` is not reflexive for NaN values and not transitive in general — it violates the
`Eq` contract but structurally conforms.

### Prior art: Go interfaces

Go is the most widely-used language with structural interface conformance. Its experience
is instructive:

- **`error` interface**: Any type with `Error() string` satisfies `error`. Types with
  `Error()` methods for other purposes (e.g., error formatting helpers) accidentally become
  error values, leading to unexpected behavior in type switches and error handling chains.

- **`fmt.Stringer` interface**: Any type with `String() string` satisfies `Stringer`, which
  changes how `fmt.Printf("%v", x)` renders it — sometimes unintentionally.

- **No opt-out mechanism exists.** The Go team's response has been naming conventions and
  documentation rather than a language feature. After 15+ years, this remains a known
  limitation with no planned fix.

Tess's risk is arguably higher than Go's because Tess matches *module-level functions*
rather than *methods on a type*. Common function names like `eq`, `add`, `cmp` are more
likely to collide with trait signatures than method names on a specific receiver type.

## Design alternatives considered

### Option 1: No action (status quo)

Rely on naming discipline. If your function isn't meant to be an `eq` for trait purposes,
don't call it `eq`.

**Rejected.** This is Go's approach and has caused real bugs over 15 years.

### Option 2: Explicit conformance (opt-in)

Require a declaration like `[[conforms(Eq)]]` for a type to satisfy a trait. Without it,
matching functions are just functions.

**Rejected.** This is essentially Rust's model wearing a structural costume. It eliminates
the main benefit of structural typing — frictionless conformance — and would require
retroactively annotating the entire standard library. The 95% case where conformance is
intentional gets penalized to protect against the 5% case.

### Option 3: Structural conformance with opt-out (selected)

The default remains structural — matching functions satisfy traits automatically. A type
can explicitly deny specific conformances using an attribute.

## Proposed design

### Syntax

Use `[[no_conform(Trait1, Trait2, ...)]]` on a type definition to prevent the type from
satisfying the listed traits:

```tl
#module Graph

[[no_conform(Ord)]]
Node : { id: Int, edges: Array[Int] }

// This cmp exists for topological sorting — it is a partial order, not total.
// Node will NOT satisfy Ord despite having a matching cmp function.
cmp(a: Node, b: Node) -> CInt { ... }
```

Multiple traits can be listed:

```tl
[[no_conform(Eq, Ord)]]
Sensor : { ... }
```

### Semantics

- `[[no_conform(Trait)]]` prevents the type from satisfying `Trait` during conformance
  checking. If a generic function with bound `T: Trait` is called with this type, the
  compiler reports a trait bound error.

- Inherited traits are NOT automatically blocked. `[[no_conform(Ord)]]` blocks `Ord` but
  does not block `Eq`. If you want to block both, list both explicitly. Rationale:
  the type might have a valid `eq` but an invalid `cmp` — blocking `Ord` alone is the
  correct granularity.

- Compiler-provided traits can be blocked. `[[no_conform(Add)]]` prevents the type's
  `add` function from being used for the `+` operator.

- When a compiler-provided trait is blocked, using the corresponding operator on that type
  is a compile-time error. `[[no_conform(Add)]]` means `a + b` does not compile for that
  type, even though `add(a, b)` can still be called as a regular function.

- `no_conform` applies to the type itself. For generic types like `Pair[T]`, blocking
  `Eq` means `Pair[T]` never satisfies `Eq` regardless of whether `T` does — the
  conditional conformance is suppressed entirely.

### Interaction with trait inheritance

Given:

```tl
Ord[T] : Eq[T] {
    cmp(a: T, b: T) -> CInt
}
```

- `[[no_conform(Ord)]]` → type can still satisfy `Eq` (if it has `eq`), but not `Ord`
- `[[no_conform(Eq)]]` → type cannot satisfy `Eq` directly, and also cannot satisfy `Ord`
  (since `Ord` inherits `Eq`)
- `[[no_conform(Eq, Ord)]]` → neither

The rule is: a type must satisfy all ancestors. If any ancestor is blocked, derived traits
are also unsatisfiable.

### Error messages

When a trait bound fails due to `no_conform`, the error message should indicate this:

```
error: type 'Node' does not satisfy trait bound 'Ord'
  --> main.tl:12:5
   |
12 |     sort(nodes)
   |     ^^^^ 'Ord' bound required here
   |
note: 'Node' has a matching 'cmp' function but conformance to 'Ord'
      is explicitly denied via [[no_conform(Ord)]]
  --> graph.tl:3:1
   |
 3 | [[no_conform(Ord)]]
   | ^^^^^^^^^^^^^^^^^^^
```

### Attribute predicates

`no_conform` should be queryable via attribute predicates:

```tl
Node :: [[no_conform(Ord)]]    // true
Node :: [[no_conform]]         // true (general match)
Node :: [[no_conform(Eq)]]     // false (only Ord is blocked)
```

## Documentation changes

### LANGUAGE_REFERENCE.md

Add a new subsection under **Traits** after "Conditional Conformance":

**Section: "Opting Out of Conformance"**

Content should cover:
1. The `[[no_conform(Trait)]]` attribute syntax
2. Motivation: when structural conformance produces a semantic mismatch
3. The `Node`/`Ord` example (partial order)
4. Interaction with trait inheritance (blocking a parent blocks derived traits)
5. Interaction with compiler-provided traits (blocks operator use too)
6. Attribute predicate queries

### TRAITS_AND_OPERATORS.md

Update two sections:

1. **"Comparison with Other Languages — Go — Interfaces"** (line 157-178): Add a note
   that Tess addresses Go's lack of opt-out with `[[no_conform]]`.

2. **"Summary of Tess Tradeoffs — Costs"** (line 233-235): Update the "Accidental
   conformance" bullet to note that `[[no_conform]]` is available as a mitigation, changing
   the characterization from "unlikely to cause problems" to "mitigated by opt-out."

### Limitations section

Update the Limitations section (line 1651-1660) to NOT list this as a limitation, since
it's now addressed. Alternatively, add a brief note: "Accidental conformance can be
prevented with `[[no_conform(Trait)]]` — see Opting Out of Conformance."

## Future considerations

- **Lint/warning for suspicious conformances**: The compiler could warn when a type newly
  satisfies a trait due to a function being added, especially for compiler-provided traits.
  This would be a separate feature, not part of this proposal.

- **`[[no_trait]]` on functions**: A function-level attribute that excludes a specific
  function from all trait matching. Lower priority than type-level opt-out since the
  question is usually "should this type conform" not "should this function participate."
  Could be added later if needed.
