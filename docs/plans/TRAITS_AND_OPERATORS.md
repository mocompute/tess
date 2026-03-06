# Traits and Operator Overloading

A structural trait system for compile-time type constraints. Traits are keyword-free,
use structural conformance (no `impl` blocks), and support trait inheritance, associated
types, and conditional conformance. Operator overloading is built on top of traits:
the compiler provides standard traits like `Add` and `Eq`, and dispatches operators
through trait conformance for user-defined types (built-in types use intrinsics).

## Traits

### Declaration

Traits are declared with the type parameter in square brackets. No keywords — the syntax
mirrors struct definitions, with function signatures instead of fields:

```tl
Eq[T] : {
    eq(a: T, b: T) -> Bool
}
```

The type parameter `T` represents the implementing type (no `Self` keyword).

### Syntax Disambiguation

Traits, structs, and tagged unions all use `:` before their body:

| Form | Meaning | Determined by |
|------|---------|---------------|
| `Name : { x: T, y: T }` | Struct | Body has `identifier: Type` (fields) |
| `Name : { eq(a: T, b: T) -> Bool }` | Trait | Body has `identifier(...)` (signatures) |
| `Name : { Item }` | Trait | Body has bare `identifier` (associated type) |
| `Name : \| V1 \| V2` | Tagged union | `:` followed by `\|` |
| `Name : Parent { ... }` | Trait with inheritance | `:` followed by identifier |

The parser distinguishes structs from traits by peeking at the body content after `{`:

- `identifier(` → function signature → trait
- `identifier:` → field declaration → struct
- `identifier` followed by `,` or newline (no parens, no colon) → associated type → trait

When `:` is followed by an identifier (not `{` or `|`), it is trait inheritance — the
parent trait list, with the body in braces after the last parent.

### Trait Inheritance

Traits can inherit from other traits. A type must satisfy all parent traits to satisfy
the child:

```tl
Ord[T] : Eq[T] {
    cmp(a: T, b: T) -> CInt
}
```

A type satisfying `Ord` must have both `cmp` and `eq`.

Combined traits with no additional functions use empty braces. Here `:` introduces the
parent list and the body is empty:

```tl
Numeric[T] : Add[T], Sub[T], Mul[T], Div[T] { }
```

**Circular inheritance is rejected.** If `A : B` and `B : A`, the compiler reports an error.

**Diamond inheritance is allowed.** If `D : B, C` and both `B : A` and `C : A`, the
compiler flattens the required functions and deduplicates by name and arity. `A`'s
requirements are checked once.

Ad-hoc multi-bounds are **not** supported. You must name the combination:

```tl
// NOT allowed:
sum[T: Add + Eq](arr: Array[T]) -> T { ... }

// Instead, define a named trait:
Summable[T] : Add[T], Eq[T] { }
sum[T: Summable](arr: Array[T]) -> T { ... }
```

### Associated Types

Bare identifiers in a trait body (no parentheses) declare associated types:

```tl
Iterator[It] : {
    Item                                     // associated type
    next(it: Ptr[It]) -> Option[Item]
}
```

Associated types are accessed via dot notation on the type parameter:

```tl
collect[It: Iterator](it: Ptr[It]) -> Array[It.Item] { ... }
```

**Constraint:** every associated type must appear in at least one function signature.
Unused associated types are meaningless — there is nothing to infer their binding from.

**Dot notation resolution:** `It.Item` in a type position resolves to the associated
type of the trait bound on `It`. The compiler distinguishes this from struct field access
(value position) and module-qualified names (resolved during parsing) by context — `It`
must be a bounded type parameter, and the lookup happens in the type system, not the
module system.

### Bounds

Trait bounds constrain generic type parameters:

```tl
sort[T: Ord](arr: Array[T]) { ... }
```

`T: Ord` is sugar for `Ord[T]`. This sugar applies to single-parameter traits only.

Multiple type parameters may have independent bounds:

```tl
sort[T: Ord, U](pairs: Array[Pair[T, U]]) { ... }   // only T is bounded
convert[A, B: Add](a: A, b: B) -> B { ... }          // only B is bounded
```

**Bounds apply to functions, not type definitions.** Type parameters on structs and tagged
unions cannot have bounds:

```tl
// NOT allowed:
SortedList[T: Ord] : { items: Array[T] }

// Instead, leave the type unconstrained:
SortedList[T] : { items: Array[T] }

// Bounds go on the functions that need them:
insert[T: Ord](list: Ptr[SortedList[T]], item: T) { ... }
```

This means you can create a `SortedList[SomeType]` even if `SomeType` doesn't satisfy
`Ord` — the error only appears when you call a function that requires the bound. This
avoids viral bound propagation through generic code and keeps the type system simpler.

To use a trait as a bound, the module defining the trait must be imported. Conformance
does not require the trait to be imported (structural), but referencing the trait name
in a bound does.

### Traits Are Types

Traits follow the same rules as struct and tagged union types:

- **Module-mangled.** A trait `Eq` defined in module `Math` becomes `Math__Eq` internally.
- **Imported like types.** To reference a trait by name (in a bound), its module must be
  `#import`ed. Compiler-provided traits (`Add`, `Eq`, `Ord`, etc.) are always visible.
- **Unique names.** A trait and a struct cannot share the same name within a module — they
  occupy the same namespace. `Vec3 : { x: Float }` and `Vec3[T] : { add(a: T) -> T }` in
  the same module is an error (duplicate type name).
- **Cross-module bounds.** `sort[T: Math.Ord](...)` uses the same dot syntax as cross-module
  type references.

### Structural Conformance

Conformance is implicit — no `impl` blocks. A type conforms to a trait if its module
contains functions matching all of the trait's signatures:

```tl
// Vec3 module:
Vec3 : { x: Float, y: Float, z: Float }

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

// Vec3 now structurally conforms to Eq — no declaration needed.
```

**Full conformance required.** A trait bound `T: Eq` requires the type to have ALL
functions from the trait (including inherited traits). If a type has `eq` but is missing
another function that `Eq` requires, it does not satisfy `Eq`. The error appears when
code with a `T: Eq` bound is called with the non-conforming type.

**Accidental conformance is allowed.** If a type happens to have functions matching a
trait's signatures, it conforms. This is by design.

**No import required for conformance.** A type's module does not need to import a trait
to conform to it. The trait only needs to be visible where it is used as a bound.

**Arity mangling.** Trait function signatures specify logical parameter counts. When
checking conformance, the compiler searches for the arity-mangled name in the type's
module. For example, the trait function `eq(a: T, b: T) -> Bool` has arity 2, so
conformance checks look for `eq__2` (or `Module__eq__2` for cross-module types).

### Conditional Conformance

A generic type conditionally conforms to a trait when its conformance depends on its
type parameter. This is expressed naturally through trait bounds on the function's type
parameter:

```tl
// In Array module:
eq[T: Eq](a: Array[T], b: Array[T]) -> Bool {
    if a.len != b.len { false }
    else {
        // compare elements using T's eq
        ...
    }
}
```

The compiler sees: Array module has `eq` matching the `Eq` signature, but only when
`T: Eq`. Therefore `Array[T]` conforms to `Eq` when `T` conforms to `Eq`.

**Transitive checking.** When verifying a conditional conformance like `Array[Int]: Eq`,
the compiler:

1. Finds `eq[T: Eq](Array[T], Array[T]) -> Bool` in the Array module
2. Matches `Array[Int]` against `Array[T]` to determine `T = Int`
3. Recursively verifies that `Int` satisfies the bound `T: Eq`
4. Only if step 3 succeeds does `Array[Int]` satisfy `Eq`

This recursive checking applies transitively — if `Array[T]: Eq` requires `T: Eq`, and
`T` is itself a generic type with conditional conformance, the compiler follows the chain.

### Conformance Checking

When the compiler encounters a trait bound, it checks structural conformance:

1. Collect all functions in the type's module (using arity-mangled names)
2. For each function in the trait (and all inherited traits), find a matching function
   by name and arity
3. Unify the trait signature against the actual function signature, substituting the
   trait's type parameter for the concrete type
4. If the matching function has trait bounds on its own type parameters, verify those
   bounds are satisfied (conditional conformance — see transitive checking above)
5. Resolve associated type bindings from the unification
6. All functions must match for conformance

This check only runs when a trait bound is actually used — not eagerly for every
type/trait combination.

## Operator-to-Trait Mapping

Operators dispatch through compiler-provided traits. These traits exist implicitly — they
are not written in user code and do not need to be imported.

### Reserved Trait Names

The following trait names are reserved by the compiler. User code cannot define types or
traits with these names:

`Add`, `Sub`, `Mul`, `Div`, `Mod`, `BitAnd`, `BitOr`, `BitXor`, `Shl`, `Shr`,
`Eq`, `Ord`, `Neg`, `Not`, `BitNot`

### Compiler-Provided Traits

```tl
// Arithmetic
Add[T] : { add(a: T, b: T) -> T }
Sub[T] : { sub(a: T, b: T) -> T }
Mul[T] : { mul(a: T, b: T) -> T }
Div[T] : { div(a: T, b: T) -> T }
Mod[T] : { mod(a: T, b: T) -> T }

// Bitwise
BitAnd[T] : { bit_and(a: T, b: T) -> T }
BitOr[T]  : { bit_or(a: T, b: T) -> T }
BitXor[T] : { bit_xor(a: T, b: T) -> T }
Shl[T]    : { shl(a: T, b: T) -> T }
Shr[T]    : { shr(a: T, b: T) -> T }

// Comparison
Eq[T]  : { eq(a: T, b: T) -> Bool }
Ord[T] : Eq[T] { cmp(a: T, b: T) -> CInt }

// Unary
Neg[T]    : { neg(a: T) -> T }
Not[T]    : { not(a: T) -> Bool }
BitNot[T] : { bit_not(a: T) -> T }
```

### Operator Dispatch

| Operator | Trait | Function | Notes |
|----------|-------|----------|-------|
| `+`      | `Add` | `add` | |
| `-`      | `Sub` | `sub` | |
| `*`      | `Mul` | `mul` | |
| `/`      | `Div` | `div` | |
| `%`      | `Mod` | `mod` | |
| `&`      | `BitAnd` | `bit_and` | Bitwise AND |
| `\|`     | `BitOr` | `bit_or` | Bitwise OR |
| `^`      | `BitXor` | `bit_xor` | Bitwise XOR |
| `<<`     | `Shl` | `shl` | Shift left |
| `>>`     | `Shr` | `shr` | Shift right |
| `==`     | `Eq` | `eq` | Falls back to `Ord.cmp` |
| `!=`     | `Eq` | | `!eq(a, b)` — compiler-derived |
| `<`      | `Ord` | | `cmp(a,b) < 0` — compiler-derived |
| `<=`     | `Ord` | | `cmp(a,b) <= 0` — compiler-derived |
| `>`      | `Ord` | | `cmp(a,b) > 0` — compiler-derived |
| `>=`     | `Ord` | | `cmp(a,b) >= 0` — compiler-derived |
| `-` (unary) | `Neg` | `neg` | |
| `!` (unary) | `Not` | `not` | |
| `~` (unary) | `BitNot` | `bit_not` | |

### Resolution

1. See `a + b`
2. Is `a` a built-in type (Int, Float, CInt, etc.)? → use intrinsic (existing path)
3. Is `a` a user-defined type? → check if it conforms to `Add` → call `add`
4. Neither? → error

Built-in types use intrinsics directly — no synthesized trait conformance.

### Comparison Operators

- **`eq(a: T, b: T) -> Bool`** — handles `==` and `!=` (negated)
- **`cmp(a: T, b: T) -> CInt`** — handles `<`, `<=`, `>`, `>=`

For `==` and `!=`: the compiler first checks for `eq`. If absent, falls back to deriving
from `cmp` (i.e., `cmp(a, b) == 0`).

`Ord` inherits from `Eq`, so a type satisfying `Ord` must have both `eq` and `cmp`.
A type that only supports equality (not ordering) defines only `eq` and satisfies `Eq`
but not `Ord`.

### Compound Assignment

`a += b` desugars to `a = add(a, b)` when the type conforms to `Add`. Same for `-=`,
`*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `^=`, `|=`.

## What Does NOT Get Overloaded

- `&&`, `||` — logical operators (short-circuit semantics, always Bool)
- `.`, `->` — struct access
- `&` (address-of), `*` (dereference) — pointer operations (unary; binary `&` is bitwise
  AND and IS overloadable via `BitAnd`)
- `=` — assignment
- `::` — type predicate
- `[]` — indexing (built-in for `Ptr` and C-arrays only)

## Examples

### Basic Operator Overloading

```tl
Vec3 : { x: Float, y: Float, z: Float }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

neg(v: Vec3) -> Vec3 {
    Vec3(x = 0.0 - v.x, y = 0.0 - v.y, z = 0.0 - v.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

main() {
    a := Vec3(x = 1.0, y = 2.0, z = 3.0)
    b := Vec3(x = 4.0, y = 5.0, z = 6.0)
    c := a + b       // calls add(a, b)
    d := -a           // calls neg(a)
    e := a == b       // calls eq(a, b)
    f := a != b       // calls !eq(a, b)
    0
}
```

### Trait Bounds

```tl
Summable[T] : Add[T], Eq[T] { }

sum[T: Summable](arr: Array[T], zero: T) -> T {
    result := zero
    i := 0
    while i < arr.len {
        result = result + arr[i]
        i = i + 1
    }
    result
}
```

### Conditional Conformance

```tl
// In Array module — Array[T] is Eq when T is Eq:
eq[T: Eq](a: Array[T], b: Array[T]) -> Bool {
    if a.len != b.len { false }
    else {
        i := 0
        while i < a.len {
            if !(a[i] == b[i]) { false }
            else { i = i + 1 }
        }
        true
    }
}
```

### Generic Type Operators

A generic type can overload operators by defining generic functions with trait bounds.
This combines operator overloading with conditional conformance:

```tl
Pair[A] : { fst: A, snd: A }

add[T: Add](a: Pair[T], b: Pair[T]) -> Pair[T] {
    Pair(fst = a.fst + b.fst, snd = a.snd + b.snd)
}
```

`Pair[Int]` supports `+` because `Int` satisfies `Add`. `Pair[SomeType]` does not unless
`SomeType` also satisfies `Add`. The compiler resolves this through the same conditional
conformance machinery.

### Associated Types

```tl
Iterator[It] : {
    Item
    next(it: Ptr[It]) -> Option[Item]
}

collect[It: Iterator](it: Ptr[It]) -> Array[It.Item] {
    result := Array.empty[It.Item]()
    while true {
        case next(it) {
            Some(v) { Array.push(result.&, v) }
            None    { return result }
        }
    }
    result
}
```

## Implementation Sketch

This section maps the design onto the existing compiler pipeline, identifies where new
logic hooks in, and determines whether additional compiler phases are needed.

### Existing Pipeline

The compiler's type inference runs through 7 phases in `tl_infer_run` (`infer.c`):

| # | Phase | File | Purpose |
|---|-------|------|---------|
| 1 | Alpha conversion | `infer_alpha.c` | Rename variables to unique names |
| 2 | Load toplevels | `infer_constraint.c` | Register functions + types in toplevel map |
| 3 | Generic inference | `infer_constraint.c` | Bottom-up constraint generation + unification |
| 4 | Free variable check | `infer_constraint.c` | Validate, apply substitutions |
| — | Weak int defaulting | `infer.c` | `42` → `Int`, `42u` → `UInt` |
| 5 | Specialization | `infer_specialize.c` | Monomorphize generic functions at call sites |
| 6 | Tree shaking | `infer_update.c` | Remove unreachable definitions |
| 7 | Type updates | `infer_update.c` | Final type resolution for codegen |

**No new phases are needed.** Traits and operator overloading hook into existing phases:

### Phase 2 — Load Toplevels: Trait Registration

Currently, Phase 2 scans all top-level definitions and calls
`create_type_constructor_from_user_type` for structs and tagged unions. Traits hook in here:

- Parse `ast_trait_definition` nodes (new AST tag)
- Register traits in a **trait registry** (new data structure alongside the type registry)
- Each trait entry stores: name, type parameter, function signatures (name + arity +
  parameter types + return type), associated types, parent trait list
- Trait inheritance is resolved here: flatten parent chains, detect cycles
- Reserved compiler-provided traits (`Add`, `Eq`, etc.) are pre-registered at `tl_infer_create`

The trait registry is keyed by module-mangled name (e.g., `Math__Sortable`). Compiler-
provided traits have no module prefix and are always visible.

### Phase 3 — Generic Inference: Operator Overload + Bound Checking

This is where constraint generation happens. Two changes:

**Operator overload (Stage A).** In `infer_binary_op` and `infer_unary_op`, after resolving
operand types:

1. Substitute to check if the left operand's type is concrete
2. If it's a user-defined type (`tl_monotype_is_inst` and not built-in):
   - Extract the type's module from its mangled name
   - Look up the trait function (e.g., `add__2`) in the toplevel map
   - If found: rewrite the `ast_binary_op` node to an `ast_named_function_application`
     (NFA) calling that function, then constrain as a normal function call
   - If not found: emit error ("no overload for operator '+' on type Vec3")
3. If the type is still a variable: generate standard constraints (unchanged).
   The operator will be resolved in Phase 5

For comparison operators, the lookup chain is: `eq__2` first, fall back to `cmp__2`.
For derived operators (`!=`, `<`, `<=`, `>`, `>=`): wrap the rewritten NFA in additional
AST nodes (negation, comparison against 0).

For compound assignment (`infer_reassignment`): extract the base operator, look up the
overload function, rewrite `a += b` to `a = Module__add__2(a, b)`.

**Trait bound checking.** When a function with trait bounds is called (e.g.,
`sort[T: Ord](...)`), the compiler must verify the concrete type satisfies the bound.
This check can trigger during Phase 3 if the type argument is already concrete, but
typically triggers during Phase 5 when types become concrete through specialization.

The check itself is: look up each trait function (by arity-mangled name) in the type's
module, unify the trait signature against the found function's type. For conditional
conformance, recursively verify any bounds on the found function's own type parameters.

### Phase 5 — Specialization: Deferred Operator Rewrite + Bound Verification

Specialization (`specialize_applications_cb` in `infer_specialize.c`) already walks the
AST and rewrites generic calls to specialized versions. Two additions:

**Deferred operator rewrite (Stage B).** For `ast_binary_op`/`ast_unary_op` nodes that
survived Phase 3 with type variables (inside generic functions), the operand types are
now concrete after specialization. The same overload lookup from Phase 3 runs here:

- In `specialize_applications_cb` or a sibling callback, check for remaining operator
  nodes with user-defined-type operands
- Rewrite to NFA nodes calling the overload function
- The NFA then gets specialized by the existing machinery

This handles code like:
```tl
double(v: Vec3) -> Vec3 { v + v }   // + can't resolve in Phase 3 (v is generic T)
                                      // resolves in Phase 5 when T = Vec3
```

**Bound verification.** In `specialize_arrow`, before creating a new specialization:

1. Resolve the function's trait bounds against the concrete type arguments
2. For each bound `T: Trait`, run conformance checking on the concrete type
3. If conformance fails, emit error ("type Vec3 does not satisfy trait Ord")
4. If the conforming function has its own bounds (conditional conformance), verify
   those recursively

This happens naturally in the specialization flow: `specialize_arrow` already resolves
type arguments and creates concrete instances. Bound checking is an additional gate
before the clone-and-re-infer step.

In `post_specialize`, after re-inference of a specialized function body, any new operator
nodes or trait-bounded calls that appear are handled by the recursive
`specialize_applications_cb` call that already exists.

### Parsing: Trait Declarations

In `parser.c`, a new `toplevel_trait()` function handles trait syntax. It runs during
both parsing passes:

**Pass 1 (symbol collection):** Register the trait name via `add_module_symbol()`, just
like structs. This makes the name available for forward references.

**Pass 2 (full parse):** Parse the complete trait declaration:

1. Parse `Name[T]` (name + type parameter)
2. If `:` followed by identifier (not `{` or `|`): parse parent trait list
3. Parse `{` body `}`:
   - Peek at first token after `{` to disambiguate from struct
   - `identifier(` → function signature (trait)
   - `identifier:` → field (struct — backtrack, not a trait)
   - bare `identifier` → associated type (trait)
4. Produce `ast_trait_definition` node

The `toplevel_trait()` function runs before `toplevel_struct()` in the dispatch chain.
Since both start with `Name :`, the peek-ahead on body content resolves the ambiguity.
The speculative arena pattern (try, backtrack on failure, clone on success) applies.

### AST Rewrite Strategy

Overloaded operators are rewritten to function calls (NFAs) during type inference. By the
time code generation runs, the operator nodes have been replaced. No changes needed in
`transpile.c` — the existing function call codegen handles the rewritten nodes.

The rewrite transforms:

```
ast_binary_op { left: a, right: b, op: "+" }
```

into:

```
ast_named_function_application { name: "Module__add__2", args: [a, b] }
```

For comparison derivation, `a < b` becomes:

```
ast_binary_op {
    left: ast_named_function_application { name: "Module__cmp__2", args: [a, b] },
    right: ast_integer { value: 0 },
    op: "<"
}
```

The outer `<` is now a built-in integer comparison (CInt < 0), handled by existing codegen.

### Module Extraction

The type constructor's `name` is module-mangled (e.g., `Math__Vec3`). The `generic_name`
field holds the unmangled name (`Vec3`). The module prefix is derived by stripping
`generic_name` and the `__` separator from `name`. For types in the main module (no
prefix), the function name is just `add__2`.

### Data Structures

**Trait definition** (new, in `type.h` or a new `trait.h`):

```
trait_def:
    name             — module-mangled trait name (e.g., Math__Sortable)
    generic_name     — unmangled name (Sortable)
    type_param       — the trait's type parameter name (alpha-converted)
    functions[]      — array of { name, arity, param_types, return_type }
    associated_types[] — array of associated type names
    parents[]        — array of parent trait names (module-mangled)
```

**Trait registry** (new, in `tl_infer` struct):

```
trait_registry:
    definitions      — map: str → trait_def (all registered traits)
```

Compiler-provided traits are pre-populated. User traits are added during Phase 2.

**Conformance cache** (new, in `tl_infer` struct):

```
conformance_cache:
    checked          — map: (type_name, trait_name) → bool
```

Avoids re-checking the same type/trait pair. Populated lazily during Phase 3 and 5.

### Key Files

| File | Role |
|------|------|
| `src/tess/src/infer_constraint.c` | Operator overload lookup + AST rewrite (Phase 3) |
| `src/tess/src/infer_specialize.c` | Deferred operator rewrite + bound verification (Phase 5) |
| `src/tess/src/infer.c` | Trait registry initialization, conformance checking |
| `src/tess/src/parser.c` | Trait declaration parsing |
| `src/tess/include/ast.h` | New `ast_trait_definition` node type |
| `src/tess/include/type.h` | Trait definition struct, conformance types |
| `src/tess/src/transpile.c` | No changes expected |

## Edge Cases

- **Tagged unions**: `Option.eq(a, b)` works for `opt1 == opt2`. Same module lookup rules.
- **`!=` derivation**: Always `!eq(a, b)` or `!(cmp(a, b) == 0)`.
- **Chained operators**: `a + b + c` parses as `(a + b) + c`. Each `+` is independently
  rewritten. Works naturally.
- **Short-circuit operators**: `&&`, `||` are never overloadable (evaluation semantics).
- **Pointer operators**: `&` (address-of), `*` (dereference), `.`, `->` are never
  overloadable (memory model semantics).
- **No ambiguous conformance**: Tess only supports function overloading by arity, not by
  type signature. A module cannot have two `eq` functions with the same arity (e.g., both
  a concrete and a generic version). This means trait conformance lookup always finds at
  most one candidate per function name and arity — no ambiguity resolution needed.
- **Empty traits**: `Marker[T] : { }` is syntactically valid but semantically vacuous —
  every type trivially conforms since there are no required functions.

## Limitations

### No mixed-type operators

All binary operator overloads require both operands to be the same type `T`. Expressions
like `vec3 * 2.0` (scalar multiplication) or `matrix * vector` cannot be expressed through
operator syntax. Users must call the function explicitly: `Vec3.scale(v, 2.0)`.

### No right-hand dispatch

When the left operand is a built-in type, the compiler never searches for overloads.
`2.0 * vec3` cannot work — `Float` is built-in, so no `Vec3.mul` lookup occurs.


### No dynamic dispatch

Traits are purely compile-time constraints, erased after checking. There are no trait
objects or vtables. Heterogeneous collections should use tagged unions instead.

### No default implementations

Trait bodies contain only function signatures, not default implementations. Compiler-
derived operators (`!=` from `eq`, ordering from `cmp`) are the only defaults, and they
are hardcoded in the compiler.

### No multi-parameter traits

Traits have exactly one type parameter (the implementing type). Mixed-type relationships
(e.g., `Add[T, U, R]`) are not expressible.

### No ad-hoc multi-bounds

Trait bounds are always a single trait name. `T: A + B` is not supported — define a named
combined trait instead.

### Fixed operator return types

Arithmetic/bitwise operators must return the same type `T`. `eq` must return `Bool`.
`cmp` must return `CInt`. Partial ordering (returning `Option[CInt]`) is not supported
through operator syntax.

## Comparison with Other Languages

### Rust — Traits

Rust traits are the closest relative. Key differences:

| Aspect | Rust | Tess |
|--------|------|------|
| Conformance | Explicit `impl Trait for Type` blocks | Structural — matching functions suffice |
| Dispatch | Static (monomorphized) + dynamic (`dyn Trait`) | Static only (no dynamic dispatch) |
| Orphan rule | Cannot impl foreign trait for foreign type | No restriction — structural conformance |
| Default methods | Supported in trait body | Not supported |
| Associated types | Yes, with `type Item = ...` in impl | Yes, inferred from function signatures |
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
conforming types inherit. Tess requires each type to provide all functions explicitly
(except compiler-derived operators like `!=`). This is simpler but means more boilerplate
for trait hierarchies — e.g., every `Ord` type must define both `eq` and `cmp` separately.

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
| Associated types | No | Yes |
| Inheritance | Embedding | Named trait inheritance |

**Key similarity: structural conformance.** Both Go and Tess check conformance by matching
function signatures rather than requiring explicit declarations. The difference is that Go
interfaces are primarily a dynamic dispatch mechanism (interface values carry a vtable),
while Tess traits are purely compile-time constraints erased after checking.

**Key difference: methods vs functions.** Go attaches methods to types via receiver syntax
(`func (v Vec3) Add(other Vec3) Vec3`). Tess uses module-scoped free functions. This
means Tess conformance is determined by the module a function lives in, not by receiver
type.

### Swift — Protocols

| Aspect | Swift | Tess |
|--------|-------|------|
| Conformance | Explicit (`: Protocol` on type or extension) | Structural |
| Associated types | Yes, with `associatedtype` keyword | Yes, bare identifiers |
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
- **No default methods.** Every conforming type must provide all functions. Trait hierarchies
  like `Ord : Eq` require implementing both `eq` and `cmp` even though `eq` could be
  derived from `cmp`.
- **Accidental conformance.** A type with a function named `eq` taking two arguments of its
  type and returning `Bool` conforms to `Eq` whether intended or not. In practice, the
  specificity of trait function names makes this unlikely to cause problems.
- **No higher-kinded traits.** Cannot express `Functor`, `Monad`, or similar abstractions
  over type constructors.

### Built-in types cannot be extended

Operator overloading only applies to user-defined types (structs and tagged unions).
Built-in types like `CString`, `Int`, `Float` cannot receive new operator overloads. A
user-defined wrapper type (e.g., `Str`) in a standard library module can get overloads.
