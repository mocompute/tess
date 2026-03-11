# Float Conversions — Design Document

**Status: Design** — not yet implemented.

**Prerequisite**: Integer sub-chain system (see [INTEGER_CONVERSIONS.md](INTEGER_CONVERSIONS.md)),
which is fully implemented and provides the infrastructure this design extends.

## Problem Statement

The current float type system has the same disconnect that the integer type
system had before the integer sub-chain work: all float types unify freely
and bidirectionally.

- `CFloat` and `CDouble` unify freely, but they are different C types with
  different precisions (32-bit vs 64-bit). `Array[CFloat]` and `Array[CDouble]`
  are different specializations with different memory layouts.
- Narrowing conversions (e.g., `CDouble` -> `CFloat`) happen silently, which
  can lose precision without the developer's knowledge.
- Float-to-integer conversion is only available through the `Unsafe` module
  (`Unsafe.float_to_int`, `Unsafe.int_to_float`), which is unnecessarily
  verbose for what is a common operation.
- Float literals (`3.14`) always receive the fixed type `Float` (= `CDouble`),
  requiring explicit annotation when passing to functions expecting `CFloat`
  or `CLongDouble`.

## Design Goals

1. **Implicit widening**: safe conversions (narrow -> wide) happen automatically
2. **Explicit narrowing**: unsafe conversions (wide -> narrow) require developer intent
3. **Monomorphisation correctness**: distinct C float types produce distinct specializations
4. **Ergonomic literals**: float literals adapt to their context without annotation
5. **Predictable generics**: type variables require exact match, no implicit conversion
6. **Float-integer conversion**: explicit, ergonomic syntax replacing `Unsafe` functions
7. **Consistency**: follow the same patterns established by the integer sub-chain system

## Float Sub-Chain

Float types form a single sub-chain with a width ordering (left = narrowest,
right = widest):

```
CFloat < CDouble (= Float) < CLongDouble
```

Since `Float` is defined as an alias for `CDouble` (in `builtin.tl`), they
are the same type — no conversion needed between them.

Unlike integers, there is:
- No signed/unsigned distinction
- No fixed-width alternative chain (no `CFloat32`, `CFloat64`)
- No standalone types (no float equivalent of `CSize` or `CPtrDiff`)
- Only one chain, not seven

This simplicity means the float sub-chain implementation is substantially
smaller than the integer one.

### Data Structure Extension

The existing `integer_subchain` field in `tl_type_constructor_def` is
extended with a new enum value:

```c
TL_INTEGER_SUBCHAIN_FLOAT = 8   // CFloat < CDouble < CLongDouble
```

Despite the field name (`integer_subchain`), this is pragmatic: the
infrastructure (width comparison, directional checking) is shared between
integer and float types. The field and associated functions are reused
rather than duplicated.

The BUILTIN table entries for float types change from:

```c
BUILTIN("CFloat",      0, 0, 0, 1, TL_INTEGER_SUBCHAIN_NONE,  -1, "float",       ...)
BUILTIN("CDouble",     0, 0, 0, 1, TL_INTEGER_SUBCHAIN_NONE,  -1, "double",      ...)
BUILTIN("CLongDouble", 0, 0, 0, 1, TL_INTEGER_SUBCHAIN_NONE,  -1, "long double", ...)
```

to:

```c
BUILTIN("CFloat",      0, 0, 0, 1, TL_INTEGER_SUBCHAIN_FLOAT,  0, "float",       ...)
BUILTIN("CDouble",     0, 0, 0, 1, TL_INTEGER_SUBCHAIN_FLOAT,  1, "double",      ...)
BUILTIN("CLongDouble", 0, 0, 0, 1, TL_INTEGER_SUBCHAIN_FLOAT,  2, "long double", ...)
```

## Conversion Rules

### Within the Float Sub-Chain

| Direction | Rule | Syntax |
|-----------|------|--------|
| Widening (narrow -> wide) | Implicit | `y: CDouble := cfloat_val` |
| Narrowing (wide -> narrow) | Explicit required | `y: CFloat := cdouble_val` (let-in annotation) |
| Same type | Always OK | `y: CDouble := cdouble_val` |

### Float <-> Integer

All float-to-integer and integer-to-float conversions require explicit
let-in annotation:

```tl
// Float to integer (truncation)
i: Int := some_float

// Integer to float (potential precision loss for large values)
f: Float := some_int

// Float to specific integer type
n: CInt := some_float

// Integer to specific float type
cf: CFloat := some_int
```

This replaces the current `Unsafe.float_to_int` and `Unsafe.int_to_float`
functions. The let-in annotation is the only syntax for float-integer
conversion — there is no `as` keyword or conversion function.

### Safety Notes

**Float narrowing** (e.g., `CDouble` -> `CFloat`): values outside `CFloat`'s
range become infinity; values within range may lose precision. Both are
potential surprises, making this an explicit-only operation.

**Float to integer**: the fractional part is truncated (C semantics). Values
outside the target integer's range are undefined behavior in C. Debug bounds
checking addresses this.

**Integer to float**: exact for small values, but large integers (beyond
2^24 for `CFloat`, beyond 2^53 for `CDouble`) may lose precision. This is
the standard IEEE 754 trade-off.

## Explicit Cast Syntax: Let-In Annotation

The same let-in annotation mechanism used for integer casts and pointer
casts is used for float conversions:

```tl
// Float narrowing
narrow: CFloat := some_double

// Float widening (implicit, annotation optional)
wide: CDouble := some_cfloat

// Float to integer
i: Int := some_float

// Integer to float
f: Float := some_int
```

### Re-Assignment

As with integers, the let-in annotation only applies at the binding
site. For re-assignment of an existing variable, an intermediate let-in
is required if the conversion needs an explicit cast:

```tl
x: CFloat := 0.0
// Later:
narrow: CFloat := some_double
x = narrow
```

## Weak Float Literals

### Problem

Without special treatment, float literals have a fixed type (`3.14` is
`Float` = `CDouble`). This creates ergonomic problems:

```tl
f(x: CFloat) { ... }
f(3.14)           // Error: Float (= CDouble) -> CFloat is narrowing, needs annotation
```

Requiring `val: CFloat := 3.14; f(val)` for every literal passed to a
`CFloat` parameter would be impractical.

### Solution: Weak Float Literal Type

Float literals receive a **weak float literal type** instead of a concrete
type. A weak float literal is a constrained inference variable:

- `3.14` gets a **weak float literal** type — can become any float type
- `0.0`, `1e10`, `-2.5` — all weak float literals

There is no signed/unsigned distinction for floats, so only one weak float
tag is needed (unlike integers which have `tl_weak_int_signed` and
`tl_weak_int_unsigned`).

### Unification Rules for Weak Float Literals

| Meets | Result |
|-------|--------|
| Concrete float type (CFloat, CDouble, CLongDouble) | Literal takes on that type |
| Integer type (any) | Type error |
| Type variable `T` | Remains weak; resolved when `T` is resolved |
| Another weak float literal | Merge; both resolve together |
| Weak integer literal | Type error |

### Defaulting

If a weak float literal is unconstrained at the end of type inference (no
concrete type context determined it), it defaults to:

- Weak float -> `Float` (= `CDouble`)

This parallels weak signed integers defaulting to `Int` (= `CLongLong`).

### Precision and Representation

Float literals are parsed by `strtod()` and stored as `f64` (double) in
the AST. When a weak float literal resolves to `CFloat`, the value is
narrowed from double to float precision. This matches C behavior (`float x
= 3.14;` silently narrows the double literal). We accept this trade-off
for pragmatism.

## Directionality in Unification

### Extension of Existing System

The existing directional unification modes (`TL_UNIFY_SYMMETRIC`,
`TL_UNIFY_DIRECTED`, `TL_UNIFY_EXACT`) apply to float types with the same
semantics as integers. No new direction modes are needed.

### Float Width Check Logic

When both types are concrete float types:

1. If they are the **same type**: unification succeeds (no conversion needed)
2. They must be in the **same sub-chain** (all floats are, since there is
   only one chain):
   - Actual is narrower or equal to expected: **implicit widening** — OK
   - Actual is wider than expected: **narrowing error** — explicit annotation required
3. Mixed float and integer: always an error (different type families)

When one or both types are **weak float literals**: the literal adopts the
concrete type (or they merge if both weak); no width check needed.

### Unification Code Change

The current code at `type.c:2867-2868`:

```c
// float-convertible types always unify
if (tl_monotype_is_float_convertible(left) && tl_monotype_is_float_convertible(right)) return 0;
```

becomes directional, following the same pattern as the integer block above it:

```c
if (tl_monotype_is_float_convertible(left) && tl_monotype_is_float_convertible(right)) {
    if (dir != TL_UNIFY_SYMMETRIC) {
        int rc = check_float_direction(left, right, dir, cb, user);
        if (rc >= 0) return rc;
    }
    return 0;
}
```

Where `check_float_direction` is analogous to `check_integer_direction`
but simpler (one chain, no cross-chain logic):

```c
static int check_float_direction(tl_monotype *expected, tl_monotype *actual,
                                 tl_unify_direction dir,
                                 type_error_cb_fun cb, void *user) {
    if (expected->cons_inst->def == actual->cons_inst->def) return 0; // same type
    if (dir == TL_UNIFY_EXACT) {
        if (cb) cb(user, expected, actual);
        return 1;
    }
    // TL_UNIFY_DIRECTED: check width ordering
    int cmp = tl_monotype_compare_integer_width(expected, actual); // reuses existing function
    if (cmp >= 0) return 0; // expected wider or equal → widening OK
    if (cb) cb(user, expected, actual);
    return 1;
}
```

## Generics: Exact Match

Type variables require exact type match. No implicit widening through generics:

```tl
f(x: T, y: T) -> T { ... }

a: CFloat := 1.0
b: CDouble := 2.0

f(a, b)     // Error: T bound to CFloat from first arg, CDouble != CFloat
```

This is consistent with the integer generic rules.

### Operators

Arithmetic and comparison operators are typed with exact match on `T`:

```tl
a: CFloat := 1.0
b: CDouble := 2.0
c := a + b      // Error: CFloat != CDouble

// Fix: widen explicitly or use same type
wide: CDouble := a
c := wide + b   // OK: CDouble + CDouble -> CDouble

// Or with literals (weak float resolves to CFloat from context):
c := a + 1.0    // OK: 1.0 resolves to CFloat via T binding from a
```

### Conditionals and Match Expressions

The result type of a conditional or match expression requires exact same
type from all arms:

```tl
x := if cond { cfloat_val } else { cdouble_val }
// Error: first arm is CFloat, second arm is CDouble

// Fix: widen the first arm
x := if cond { wide: CDouble := cfloat_val; wide } else { cdouble_val }
```

## Literal Suffixes (Optional, Lower Priority)

Float literal suffixes provide a way to directly specify a concrete float
type without relying on weak literal resolution:

| Suffix | Type | Polymorphic? |
|--------|------|-------------|
| (none) | Weak float | Yes — resolves to any float type; defaults to `Float` |
| `f`/`F` | `CFloat` | No — always exactly `CFloat` |
| `L` | `CLongDouble` | No — always exactly `CLongDouble` |

These suffixes are optional and lower priority because the weak literal
mechanism handles most use cases. They provide convenience for cases where
the developer knows the exact target type:

```tl
cfloat_val: CFloat := 3.14f    // direct CFloat literal, no weak resolution
long_val: CLongDouble := 3.14L // direct CLongDouble literal
```

The suffix `f` is the same as C's suffix for float literals. The suffix `L`
matches C's suffix for long double literals.

**Implementation note**: These suffixes require changes to `str_parse_cnum()`
in `src/mos/src/str.c`, new AST handling for suffixed float literals, and
new return codes from the parser.

## Codegen

### Implicit Widening

Tess emits explicit C casts for all implicit float widenings to make the
generated C code clear and suppress compiler warnings:

```c
// Tess: y: CDouble := cfloat_val (implicit widening)
// Generated C:
double y = (double)cfloat_val;
```

### Explicit Narrowing (Let-In Annotation)

Explicit float conversions emit a C cast:

```c
// Tess: narrow: CFloat := cdouble_val
// Generated C:
float narrow = (float)cdouble_val;
```

### Float-Integer Conversions

Both directions emit a C cast:

```c
// Tess: i: Int := some_float
// Generated C:
long long i = (long long)some_float;

// Tess: f: Float := some_int
// Generated C:
double f = (double)some_int;
```

### Debug Bounds Checking

In debug mode, conversions that can produce undefined behavior or
surprising results emit runtime checks:

**Float narrowing** (e.g., `CDouble` -> `CFloat`):

```c
// Check that the narrowed result is finite (not overflow to inf)
tl_float_narrowing_assert(cdouble_val, "double", "float", __FILE__, __LINE__);
float narrow = (float)cdouble_val;
```

**Float to integer**:

```c
// Check that the float value is within the target integer's range
tl_float_to_int_assert(some_float, LLONG_MIN, LLONG_MAX, "double", "long long", __FILE__, __LINE__);
long long i = (long long)some_float;
```

**Integer to float**: no bounds check needed. The conversion is always
defined (may lose precision for large values, but this is not undefined
behavior).

## Impact on Existing Code

This design is strictly more restrictive than the current behavior. Code
that currently compiles may be rejected under the new rules:

- **Mixed-precision float code** where `CFloat` values are used where
  `CDouble` is expected (or vice versa) will require explicit widening
  or let-in annotations
- **Float-integer conversion** code using `Unsafe.float_to_int` /
  `Unsafe.int_to_float` can migrate to let-in annotations

Breaking existing code is acceptable; this is a correctness improvement.

The impact is expected to be smaller than the integer changes because:
- Most Tess code uses `Float` (= `CDouble`) exclusively
- `CFloat` and `CLongDouble` are primarily used at C FFI boundaries
- The weak literal mechanism prevents breakage for literal-heavy code

## Implementation Phases

### Phase 1: Float Sub-Chain Data

Extend the sub-chain enum and BUILTIN table to give float types a sub-chain
and width rank. Add `TL_INTEGER_SUBCHAIN_FLOAT = 8`. Verify that
`tl_monotype_compare_integer_width()` works for float types (it should,
since it only reads `integer_subchain` and `integer_width_rank` fields).

**Files**: `type.h` (enum), `type.c` (BUILTIN table)

### Phase 2: Tests (TDD)

Write tests before implementation. Expected-failure tests go to
`TL_KNOWN_FAIL_FAILURES`:

- `test_fail_float_narrowing_funcall.tl` — CDouble value to CFloat parameter
- `test_fail_float_narrowing_let.tl` — CDouble assigned to CFloat via let-in
  without annotation
- `test_fail_float_narrowing_return.tl` — returns CDouble, declared -> CFloat
- `test_fail_float_exact_operator.tl` — CFloat + CDouble
- `test_fail_float_exact_conditional.tl` — branches: CFloat vs CDouble
- `test_fail_float_int_implicit.tl` — Float to Int without annotation (and
  vice versa)

Expected-passing tests go to `TL_TESTS`:

- `test_float_widening.tl` — CFloat -> CDouble, CFloat -> CLongDouble,
  function call widening, return widening
- `test_float_same_type.tl` — same-type operators, comparisons, conditionals

### Phase 3: Weak Float Literal Type

Add `tl_weak_float` tag to the monotype enum. Implement:
- `tl_monotype_create_weak_float()` / `tl_monotype_create_fresh_weak_float()`
- `tl_monotype_is_weak_float()`
- `unify_weak_float_concrete()` — resolves weak float to concrete float type
- `unify_weak_float_other()` — handles all other cases (TV, other weak, etc.)
- Extend `tl_type_subs_default_weak_ints()` → `tl_type_subs_default_weak()`
  (or add separate `tl_type_subs_default_weak_floats()`)
- Extend `tl_monotype_is_any_weak()` and `tl_monotype_is_concrete_no_weak()`
  to include `tl_weak_float`
- Change `ast_f64` inference from `infer_literal_type()` to a new
  `infer_weak_float_literal()` function

**Files**: `type.h`, `type.c`, `infer_constraint.c`, `infer_specialize.c`

### Phase 4: Directional Width Checking for Floats

Replace the symmetric float unification line with directional checking
(see "Unification Code Change" section above). Add `check_float_direction()`.

**Files**: `type.c`

### Phase 5: Float Cast Annotations

Extend `is_cast_annotation()` to recognize float types. Add float branch
in `cast_constrain_let_in()`. Add float-integer cast support (both
directions).

**Files**: `infer_constraint.c`

### Phase 6: Codegen

Extend `generate_let_in()` to emit C casts for float types (widening and
narrowing). Add `is_float_narrowing_cast()` detection. Add debug bounds
check macros (`tl_float_narrowing_assert`, `tl_float_to_int_assert`).

**Files**: `transpile.c`, runtime header for assert macros

### Phase 7: Graduate Tests

Move tests from known-fail-failures to regular tests. Full test suite
verification (release + debug builds). Update existing `test_types_float.tl`
if it relies on the old symmetric behavior.

### Phase 8: Literal Suffixes (Optional, Lower Priority)

Add `f`/`F` and `L` suffix detection to `str_parse_cnum()`. Add new AST
handling and return codes. Add new inference paths for suffixed float
literals (concrete type, not weak).

**Files**: `str.c`, `ast.h`, `ast_tags.h`, `parser.c`, `infer_constraint.c`

### Phase 9: Unsafe Module Migration (Lower Priority)

Migrate `Unsafe.float_to_int` and `Unsafe.int_to_float` to use the let-in
annotation form. Deprecate or remove the `Unsafe` functions.

**Files**: `src/tl/std/Unsafe.tl`

## Open Questions

1. **`long double` on macOS**: `sizeof(long double) == sizeof(double)` on
   macOS (Apple Silicon). This means `CDouble` -> `CLongDouble` widening is
   a no-op on macOS but a real widening on Linux (80-bit or 128-bit). The
   sub-chain ordering is correct regardless — the language semantics should
   reflect the logical type hierarchy, not platform-specific sizes.

2. **Compound float types** (`Complex` etc.): out of scope. If C complex
   types are ever supported, they would form their own sub-chain(s).

3. **`NaN` propagation**: float arithmetic can produce `NaN` values. This
   is orthogonal to the conversion system — `NaN` propagation follows IEEE
   754 rules regardless of type widening/narrowing.

4. **Precision loss warnings for literals**: when a weak float literal
   resolves to `CFloat`, the literal value (parsed as double) may lose
   precision. This matches C behavior and we accept it. A future lint pass
   could optionally warn about this, but it is out of scope.
