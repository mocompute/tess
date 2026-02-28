# Integer Conversions — Design Document

**Status: Fully implemented** (all 7 phases). See [TYPE_SYSTEM.md](../TYPE_SYSTEM.md)
and [LANGUAGE_REFERENCE.md](../LANGUAGE_REFERENCE.md) for user-facing documentation.

## Problem Statement

The current integer type system has a disconnect between unification and
monomorphisation. Unification treats all same-family integers as freely
interchangeable (bidirectional), but monomorphisation produces distinct
specializations for each concrete type. This means:

- `CInt` and `Int` unify freely, but `Array[CInt]` and `Array[Int]` are
  different specializations with different C representations.
- Narrowing conversions (e.g., `Int` -> `CInt`) happen silently, which is
  unsafe — data can be lost without the developer's knowledge.
- The `Unsafe` module provides explicit conversion functions
  (`unsigned_to_signed`, `signed_to_unsigned`) for cross-family conversions,
  but the implicit same-family behavior is inconsistent with this caution.

## Design Goals

1. **Implicit widening**: safe conversions (narrow -> wide) happen automatically
2. **Explicit narrowing**: unsafe conversions (wide -> narrow) require developer intent
3. **Explicit cross-family**: signed <-> unsigned always requires developer intent
4. **Monomorphisation correctness**: distinct C types produce distinct specializations
5. **Ergonomic literals**: integer literals adapt to their context without annotation
6. **Predictable generics**: type variables require exact match, no implicit conversion

## Integer Sub-Chains

Integer types are organized into **seven sub-chains**, each with a width
ordering. Implicit widening is only permitted within a single sub-chain,
following the ordering (left = narrowest, right = widest).

### C-Named Signed
```
CSignedChar < CShort < CInt < CLong < CLongLong (= Int)
```

### C-Named Unsigned
```
CUnsignedChar < CUnsignedShort < CUnsignedInt < CUnsignedLong < CUnsignedLongLong (= UInt)
```

### Fixed-Width Signed
```
CInt8 < CInt16 < CInt32 < CInt64
```

### Fixed-Width Unsigned
```
CUInt8 < CUInt16 < CUInt32 < CUInt64
```

### Standalone Types (No Implicit Conversion)

These types have platform-dependent widths and form degenerate sub-chains
of length 1. Any conversion to/from these types requires explicit annotation.

- **CSize** — `size_t`, unsigned, platform-dependent width
- **CPtrDiff** — `ptrdiff_t`, signed, platform-dependent width
- **CChar** — `char`, signedness is implementation-defined in C

Note: `CChar` is classified separately from both signed and unsigned chains
because its signedness is platform-dependent in C. A future `Char` type alias
(for `CUnsignedChar`) may be introduced for Tess-native code, but is out of
scope for this design.

## Conversion Rules

### Within a Sub-Chain

| Direction | Rule | Syntax |
|-----------|------|--------|
| Widening (narrow -> wide) | Implicit | `y: Int := cint_val` |
| Widening (narrow -> wide) | Explicit (also OK) | `y: Int := cint_val` (annotation optional) |
| Narrowing (wide -> narrow) | Explicit required | `y: CInt := int_val` (annotation = cast) |

### Across Sub-Chains or Families

| Conversion | Rule | Syntax |
|------------|------|--------|
| Signed <-> Unsigned | Explicit required | `y: UInt := int_val` (let-in annotation) |
| C-named <-> Fixed-width | Explicit required | `y: CInt32 := cint_val` (let-in annotation) |
| Any <-> Standalone | Explicit required | `y: CSize := uint_val` (let-in annotation) |

### Float <-> Integer

Float-to-integer and integer-to-float conversions also use the let-in
annotation form. This replaces the current `Unsafe.float_to_int` and
`Unsafe.int_to_float` functions. (Migration of these `Unsafe` functions
is a lower-priority follow-up.)

## Explicit Cast Syntax: Generalized Let-In Annotation

The existing let-in type annotation mechanism (currently used for pointer
casts) is generalized to cover all explicit type conversions:

```tl
// Pointer cast (existing)
p: Ptr[Int] := c_malloc(sizeof[Int]() * 10zu)

// Integer narrowing (new)
narrow: CInt := some_int_value

// Cross-family (new)
unsigned: UInt := some_int_value

// Cross-chain (new)
fixed: CInt32 := some_cint_value

// Standalone type (new)
size: CSize := some_uint_value
```

The let-in annotation is the **only** syntax for explicit conversion. This
is a deliberate design choice: because the let-in form is visually prominent,
developers scanning code can identify every potentially unsafe conversion
point. There is no `as` keyword or cast function.

### Mutable Re-Assignment

The let-in annotation only applies at the declaration site. For re-assignment
to a mutable variable, an intermediate let-in is required if narrowing:

```tl
x: mut CInt := 0

// Later, with an Int value that needs narrowing:
narrow: CInt := some_int_value
x = narrow
```

This is intentional — it forces the developer to acknowledge the narrowing
at each conversion site, not just at the variable declaration.

### Safety Note

The let-in annotation for integers is semantically equivalent to a C cast.
For narrowing conversions, this means potential data loss. This is an
inherently unsafe operation being brought into the language syntax for
ergonomic reasons, rather than requiring `Unsafe` module calls. Developers
should scrutinize every annotated let-in binding for potential data loss.

## Weak Literals (Polymorphic Integer Literals)

### Problem

Without special treatment, integer literals would have a fixed type (`42` is
`Int`, `42u` is `UInt`). This creates ergonomic problems:

```tl
f(x: CInt) { ... }
f(42)           // Error: Int (literal) -> CInt is narrowing, needs annotation
```

Requiring `val: CInt := 42; f(val)` for every literal passed to a C-typed
parameter would be impractical.

### Solution: Weak Literal Types

Integer literals receive a **weak literal type** instead of a concrete type.
A weak literal is a constrained inference variable:

- `42` gets a **weak signed literal** type — can become any signed integer type
- `42u` gets a **weak unsigned literal** type — can become any unsigned integer type
- `42z` gets a **CPtrDiff literal** type — exact type, not polymorphic
- `42zu` gets a **CSize literal** type — exact type, not polymorphic
- `0xFF` gets weak signed; `0xFFu` gets weak unsigned (suffix determines family)

### Literal Suffixes

| Suffix | Type | Polymorphic? |
|--------|------|-------------|
| (none) | Weak signed | Yes — resolves to any signed integer; defaults to `Int` |
| `u`/`U` | Weak unsigned | Yes — resolves to any unsigned integer; defaults to `UInt` |
| `z`/`Z` | `CPtrDiff` | No — always exactly `CPtrDiff` (signed, platform-dependent) |
| `zu`/`ZU` | `CSize` | No — always exactly `CSize` (unsigned, platform-dependent) |

The suffix design is internally consistent: `u` always means "unsigned"
and `z` always means "platform-dependent size type." The combinations
mirror the base suffixes:

- `42` : `42u` :: `42z` : `42zu` (signed : unsigned :: signed-size : unsigned-size)

The `z` convention follows C tradition (`printf`'s `%zu`, C23's `uz` suffix
for `size_t` literals). Since `CSize` and `CPtrDiff` are standalone
sub-chains, their literals have a fixed type and do not need polymorphic
resolution.

```tl
c_malloc(42zu)                   // OK: 42zu is CSize, c_malloc expects CSize
arr_size: CSize := 100zu         // OK: direct CSize literal
c_malloc(sizeof[Int]() * 10zu)   // OK: CSize * CSize -> CSize
offset: CPtrDiff := -4z          // OK: -4z is CPtrDiff
```

### Unification Rules for Weak Literals

| Meets | Result |
|-------|--------|
| Concrete integer in same family | Literal takes on that type |
| Concrete integer in other family | Type error |
| Standalone type (CSize, CPtrDiff, CChar) | Type error (use `z`/`zu` suffix or annotation) |
| Type variable `T` | Remains weak; resolved when `T` is resolved |
| Another weak literal (same family) | Merge; both resolve together |
| Another weak literal (other family) | Type error |

Note: `z` and `zu` literals are not weak — they are concrete `CPtrDiff`
and `CSize` respectively from the start and follow normal concrete type
unification rules.

### Defaulting

If a weak literal is unconstrained at the end of type inference (no concrete
type context determined it), it defaults to:

- Weak signed -> `Int` (= `CLongLong`)
- Weak unsigned -> `UInt` (= `CUnsignedLongLong`)

### Compile-Time Range Checking

When a weak literal resolves to a concrete type, the compiler can verify
that the literal value fits in that type's range. For example, `256` resolving
to `CInt8` is a compile-time error (overflow), even though the type family
matches. This is a natural bonus of the weak literal design.

## Directionality in Unification

### Core Change

The unifier currently treats its two operands symmetrically. This design
introduces **directionality** for integer type comparisons: one side is
the **expected** type and the other is the **actual** type. Conversion
flows from actual to expected.

### Directionality at Language Constructs

| Construct | Expected | Actual |
|-----------|----------|--------|
| `let x: T := expr` | `T` (annotation) | `typeof(expr)` |
| `f(arg)` where `f(x: P)` | `P` (parameter) | `typeof(arg)` |
| `return expr` in `f() -> R` | `R` (return type) | `typeof(expr)` |
| `x = expr` (assignment) | `typeof(x)` | `typeof(expr)` |

### When Directionality Applies

Directional width checking **only applies when both sides are concrete
integer types** (i.e., both are resolved to specific integer type
constructors, not type variables).

When either side is a **type variable**, standard symmetric unification
applies. This means:

- Generic parameters bind to whatever type is passed (no conversion)
- Monomorphisation creates the exact specialization for that type
- No implicit widening or narrowing through generics

### Width Check Logic

When both types are concrete integers:

1. If they are the **same type**: unification succeeds (no conversion needed)
2. If they are in the **same sub-chain**: check ordering
   - Actual is narrower or equal to expected: **implicit widening** — OK
   - Actual is wider than expected: **narrowing error** — explicit annotation required
3. If they are in **different sub-chains**: **cross-chain error** — explicit annotation required

When one or both types are **weak literals**: the literal adopts the concrete
type (or they merge if both weak); no width check needed since there is no
"width" to check.

## Generics: Exact Match

Type variables require exact type match. No implicit widening through generics:

```tl
f(x: T, y: T) -> T { ... }

a: CInt := 1
b: CShort := 2

f(a, b)     // Error: T bound to CInt from first arg, CShort != CInt
```

This avoids order-dependent behavior where `f(a, b)` and `f(b, a)` could
produce different results, and keeps the monomorphisation story predictable.

### Operators

Arithmetic and comparison operators are typed as `(T, T) -> T` (or
`(T, T) -> Bool` for comparisons), with exact match on `T`:

```tl
a: CInt := 1
b: CShort := 2
c := a + b      // Error: CInt != CShort

// Fix: widen explicitly or use same type
wide: CInt := b
c := a + wide   // OK: CInt + CInt -> CInt

// Or with literals (weak literals resolve to CInt from context):
c := a + 2      // OK: 2 resolves to CInt via T binding from a
```

### Conditionals and Match Expressions

The result type of a conditional or match expression is determined by the
**first arm**. All subsequent arms must have the exact same type — no
implicit conversion:

```tl
x := if cond { cint_val } else { int_val }
// Error: first arm is CInt, second arm is Int, no implicit conversion

// Fix: widen the first arm, or narrow the second
x := if cond { wide: Int := cint_val; wide } else { int_val }
```

## Codegen

### Implicit Widening

Even though C handles integer promotion implicitly, Tess emits explicit
C casts for all implicit widenings to make the generated C code more
readable and to avoid compiler warnings:

```c
// Tess: y: Int := cint_val (implicit widening)
// Generated C:
long long y = (long long)cint_val;
```

### Explicit Narrowing / Cross-Chain (Let-In Annotation)

Explicit conversions emit a C cast:

```c
// Tess: narrow: CInt := int_val
// Generated C:
int narrow = (int)int_val;
```

### Debug Bounds Checking

When compiling in debug mode (default ON for `CONFIG=debug`, controlled by
a compiler flag), narrowing conversions emit a bounds check before the cast:

```c
// Tess: narrow: CInt := int_val (debug mode)
// Generated C (conceptual):
assert(int_val >= INT_MIN && int_val <= INT_MAX);  // or a Tess-specific macro
int narrow = (int)int_val;
```

The exact mechanism (assert macro, helper function, compiler intrinsic) is
an implementation detail to be decided.

## Impact on Existing Code

This design is strictly more restrictive than the current behavior. Code
that currently compiles may be rejected under the new rules:

- **FFI call sites** where `Int` values are passed to C functions expecting
  `CInt`, `CLong`, etc. will require explicit let-in annotations (narrowing)
- **Mixed-width arithmetic** will require explicit widening
- **Cross-family** code already requires `Unsafe` functions and will migrate
  to let-in annotations

Polymorphic literals will mitigate much of the impact — most literal-heavy
code will continue to work since literals adapt to their context.

Breaking existing code is acceptable; this is a correctness improvement.

## Implementation Phases

### Phase 1: Sub-Chain Ordering Data Structure
Define the seven sub-chains and width ordering in the type system.
Add query functions: `same_subchain(a, b)`, `is_wider_or_equal(a, b)`.

### Phase 2: Weak Literal Types
Add a new type kind for weak signed/unsigned literals. Implement
unification rules: literal meets concrete type, literal meets literal,
literal meets type variable. Add defaulting at end of inference.

### Phase 3: Directionality in Unification

The largest phase. Add expected/actual semantics to `constrain()` and
the unification functions. Audit every call site to determine which
operand is expected vs actual. Implement directional width checking
for concrete integer pairs.

#### Sub-Phase 3A: Tests (TDD)

Write comprehensive tests before implementation. Expected-failure tests go
to `TL_KNOWN_FAIL_FAILURES` (compiler doesn't reject them yet):

- `test_fail_integer_narrowing_funcall.tl` — Int value to CInt parameter
- `test_fail_integer_narrowing_let.tl` — Int assigned to CInt via let-in
- `test_fail_integer_narrowing_return.tl` — returns Int, declared -> CInt
- `test_fail_integer_narrowing_reassign.tl` — Int assigned to mut CInt var
- `test_fail_integer_cross_chain.tl` — CInt to CInt32 (C-named vs fixed-width)
- `test_fail_integer_exact_operator.tl` — CInt + CShort
- `test_fail_integer_exact_conditional.tl` — branches: CInt vs CShort
- `test_fail_integer_exact_case.tl` — case arms: CInt vs CShort

Expected-passing tests go to `TL_TESTS`:

- `test_integer_widening.tl` — CInt→Int, CShort→CInt, function call widening
- `test_integer_same_type.tl` — same-type operators, comparisons, conditionals

#### Sub-Phase 3B: Direction Enum and Parameter Plumbing

Add `tl_unify_direction` enum with three values:

```c
TL_UNIFY_SYMMETRIC = 0   // legacy: no directional integer checking
TL_UNIFY_DIRECTED  = 1   // left=expected, right=actual; widening OK
TL_UNIFY_EXACT     = 2   // same concrete integer type required
```

Thread direction parameter through `constrain()` → `constrain_mono()` →
`tl_type_subs_unify_mono()` → `unify_list()` / `unify_type_constructor()`.
All callers initially pass `TL_UNIFY_SYMMETRIC`. Zero behavior change.

Propagation rules:
- Arrow element unification: propagate received direction
- Type constructor args: always `TL_UNIFY_SYMMETRIC` (invariant)
- All other internal recursion (TVs, weak vars, unions, tuples): `SYMMETRIC`

#### Sub-Phase 3C: Directional Width Checking Logic

Modify the integer unification block in `tl_type_subs_unify_mono()`:
- `SYMMETRIC`: keep current behavior (same-family → success)
- `DIRECTED`: use `tl_monotype_compare_integer_width()` — widening OK,
  narrowing/cross-chain is an error
- `EXACT`: different concrete types → error (even same sub-chain)

#### Sub-Phase 3D: Annotate Call Sites

Change call sites incrementally, testing after each batch:

1. **Operators → EXACT**: arithmetic, bitwise, relational operand constraints
2. **Conditionals → EXACT**: if-then-else branch, case arm constraints
3. **Let-in / reassignment → DIRECTED**: name=expected, value=actual
4. **Function application → DIRECTED**: callee=expected, callsite=actual
5. **Return → DIRECTED**: declared return type=expected, value=actual

All other calls (TV binding, bool/nil, env_insert, type predicates, struct
access, specialization) remain `SYMMETRIC`.

#### Sub-Phase 3E: Fix Regressions and Graduate Tests

- Remove CSize→UInt and CPtrDiff→Int sub-tests from `test_integer_families.tl`
  (these are cross-chain and require Phase 4 cast annotations)
- Graduate known-fail-failure tests to regular fail tests
- Full test suite verification (release + debug builds)

### Phase 4: Generalize Let-In Cast
Extend `is_ptr_cast_annotation` (or replace with a broader
`is_cast_annotation`) to recognize integer type annotations as explicit
casts. Suppress narrowing/cross-chain errors when the annotation is
present.

### Phase 5: Codegen
Emit explicit C casts for all integer conversions (widening and narrowing).
Implement debug bounds checking for narrowing conversions. Add compiler
flag to control bounds checking.

### Phase 6: Test Migration
Update existing tests to comply with new rules. Add comprehensive new
tests for: implicit widening, narrowing rejection, explicit cast
annotation, weak literals, cross-chain rejection, generic exact match,
operator exact match, conditional type matching, debug bounds checking.

### Phase 7: Unsafe Module Migration (Optional, Lower Priority)
Migrate `Unsafe.unsigned_to_signed`, `Unsafe.signed_to_unsigned`,
`Unsafe.float_to_int`, `Unsafe.int_to_float` to use the let-in
annotation form. Deprecate or remove the `Unsafe` functions.

## Open Questions

1. **Bool**: `Bool` is currently not in any integer family. Should `0`/`1`
   literals be assignable to `Bool`? Currently `Bool` has its own type
   with no integer conversion. Probably leave as-is.

2. **CChar signedness**: `CChar` is currently classified as unsigned in
   the type system. This design moves it to standalone. Need to verify
   that existing code that treats `CChar` as unsigned still compiles.

3. **Float sub-chain**: `CFloat`, `CDouble`, `CLongDouble` currently unify
   freely. Should they follow the same directional widening rules? Probably
   yes for consistency, but this is out of scope for the initial implementation.

4. **Operator overloading for mixed types**: If exact match on operators
   proves too restrictive in practice, we could introduce overloaded
   operators that accept different-width same-chain operands and return the
   wider type. Defer unless ergonomics are unacceptable.

5. **CChar literals**: Character literals already exist (`'a'`, `'\n'`, etc.)
   and produce `CChar` values. No integer literal suffix for `CChar` is
   planned since character literals serve this purpose.
