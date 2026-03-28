# Explicit Type Arguments — Design Record

## Overview

Tess migrated from implicit, parenthesized type arguments to explicit, square-bracket
type arguments (Go-style syntax). This was both a **syntax change** (`()` → `[]` for
type arguments) and a **semantic change** (required type parameter declarations on
generic functions, elimination of type literals in value positions).

### Before / After

```
BEFORE                                      AFTER
──────                                      ─────
Point(a): { x: a, y: a }                    Point[a] : { x: a, y: a }
Option(T): | Some { v: T } | None           Option[T] : | Some { v: T } | None
Pt = Point(Int)                             Pt = Point[Int]

x: Ptr(Int)                                 x: Ptr[Int]
s: Ptr(Const(CChar))                        s: Ptr[Const[CChar]]
buf: CArray(CChar, 256)                     buf: CArray[CChar, 256]
arr: Array(Int)                             arr: Array[Int]

empty(T: Type) -> Array(T) { ... }          empty[T]() -> Array[T] { ... }
with_capacity(T: Type, n: Int)              with_capacity[T](n: Int)
arr := empty(Int)                           arr := empty[Int]()
map(f, arr) { ... }                         map[a, b](f: (a)->b, arr: Array[a]) -> Array[b]

sizeof(T)                                   sizeof[T]()
alignof(T)                                  alignof[T]()
sizeof(Ptr(Void))                           sizeof[Ptr[Void]]()
sizeof(x)                                   sizeof(x)           // value form unchanged
Constructor(x = 1, y = 2)                   Constructor(x = 1, y = 2)  // unchanged
Variant(42)                                 Variant(42)                // unchanged (value args)
```

Note: `sizeof` and `alignof` have two forms: `sizeof[T]()` (type parameter, arity 0)
and `sizeof(x)` (value argument, arity 1). The type-as-value form `sizeof(T)` is removed.

### Indexing Syntax

Pointer/CArray indexing uses `.[]` syntax: `ptr.[i]`, `arr.[0] = 10`.
This means `[]` is **unambiguously** type arguments — no disambiguation needed.

The `.[]` syntax fits naturally with the existing postfix family: `.field`, `.*`,
`.&`, `->`. See `test_carray.tl` for examples.

---

## Design Principles

1. **Square brackets for all type arguments** — no exceptions. `[]` always means
   type arguments; `()` always means value arguments or constructors.

2. **No type literals in value positions.** Types may not appear as function arguments
   or on the RHS of binding expressions. This eliminates the `T: Type` pattern entirely.
   Built-ins like `sizeof` and `alignof` use type parameters (`sizeof[T]()`) instead of
   value arguments (`sizeof(T)`).

3. **No disambiguation needed.** Indexing uses `.[]` syntax, so bare `[]` is unambiguously
   type arguments in all positions.

4. **Required type parameter declarations.** All generic functions must explicitly
   declare their type parameters with `[T, U, ...]`. Implicit type variable creation
   (type variable sugar) is removed — unknown symbols in type annotations are errors.

5. **Struct constructors unchanged.** `Point(x = 1, y = 2)` keeps `()` (value args).

6. **Tagged union value constructors unchanged.** `Some(42)` keeps `()` (value args).

7. **Type definitions use `[]`.** `Point[a] : { ... }`, `Option[T] : | ...`

8. **All built-in type constructors use `[]`.** `Ptr[T]`, `CArray[T, N]`, `Const[T]`,
   `Array[T]`.

---

## Case Expressions and Tagged Unions

Tagged unions with type parameters require explicit type arguments in case expressions:

```tl
case x: Option[Int] {
  s: Some { ... }
  n: None { ... }
}
```

The type arguments are specified on the scrutinee's type annotation, not on individual
variant patterns.

---

## Remaining Migration

Several test files still use old `()` syntax for type parameters in definitions and
type annotations. These need to be migrated to `[]` syntax, and the parser should be
updated to reject the old syntax.

### Tests needing migration to `[]` syntax

- `test_static_init_generic_function_pointer.tl`: `Processor(T):`, `Processor(a)`
- `test_struct_field_ptr_cast.tl`: `Foo(T):`
- `test_struct_field_ptr_cast_multi.tl`: `Arr(a):`
- `test_regress_type_cons.tl`: `Span(T):`
- `test_fail_reserved_type_tu.tl`: `TU(T):`
- `test_fail_tagged_union_existing_type_bad_type_arg.tl`: `Pair(a):`, `Shape(a):`
- `test_fail_const_strip_nested.tl`: mixed syntax `Ptr[Ptr(Int)]`, `Ptr[Ptr(Const[Int])]`

### Parser: reject old `()` syntax for type arguments

After migration is complete, the parser should reject `()` in type argument positions.
New `test_fail_*` tests should be added to verify the parser rejects:
- `Foo(T):` in type definitions
- `Ptr(Int)` in type annotations
- `sizeof(T)` as type-as-value (but `sizeof(x)` for value expressions remains valid)

---

## Testing Gaps

### Missing error tests

- **Type argument count mismatch.** No `test_fail_*` for passing the wrong number of
  type arguments (e.g., `foo[Int, Float]()` when `foo[T]()` is declared).

- **Undeclared type parameter.** No `test_fail_*` for using an undeclared type variable
  in an annotation (e.g., `foo(x: T)` where `T` is not declared with `[T]`). Relevant
  once type variable sugar is removed.

- **Old syntax rejection.** No `test_fail_*` for `Foo(T):` or `Ptr(Int)` (see
  "Parser: reject old syntax" above).

### Missing positive tests

- **Type parameter forwarding.** No test where a generic function forwards its type
  parameter to another generic function: e.g., `foo[T]() { bar[T]() }`. All existing
  cross-generic calls use concrete types at call sites.

- **Type predicates with complex types.** All type predicate tests use simple types
  (`T :: Int`, `T :: Float`). No coverage for `T :: Ptr[Int]` or `T :: Array[Int]`.

- **sizeof/alignof with deeply nested types.** Only `sizeof[Ptr[Void]]()` and
  `sizeof[Point[Int]]()` are tested. No coverage for deeply nested cases like
  `sizeof[Ptr[Const[CChar]]]()`.

- **Multiple explicit specializations in one scope.** No test that calls the same
  generic function with different explicit type args in the same function body
  (e.g., `empty[Int]()` and `empty[Float]()` side by side).

---

## Open Design Questions

### Explicit type arguments for lambda functions

There is currently no syntax for declaring type parameters on lambdas. This is a
language design regression: the old `T: Type` pattern allowed lambdas to be
parameterized over types:

```tl
// Old syntax (no longer valid):
(x: Type) { sizeof(x) } (Int)
```

A new syntax is needed. The test `test_lambda_immediate_type_argument.tl` still uses
the old pattern and needs to be redesigned once lambda type parameter syntax is decided.

Additionally, generic lambdas currently rely on implicit type variable creation for
specialization:

```tl
f := (a, b) { a + b }
x1 := f(1, 2)       // Int specialization
x2 := f(1.0, 0.5)   // Float specialization
```

With required type parameter declarations (design principle 4), this implicit
mechanism is removed. Lambda type parameter syntax must also address this case.

### Function pointer notation with type arguments

Function pointers currently use `/arity` notation (e.g., `double/1`, `empty/0`). It is
unclear how this interacts with explicit type arguments. For example, how would one
reference a specific specialization of a generic function as a pointer?

### ~~Indexing syntax: `.[]` instead of `.()`~~ (Resolved)

Resolved: indexing now uses `.[]` syntax (`ptr.[i]`, `arr.[0] = 10`). The `.`
prefix fully disambiguates: `.[]` is always indexing; bare `[]` after a name is
always type arguments.

### Eliding empty parentheses after type arguments

Expressions like `sizeof[T]()` where the value argument list is empty could
potentially be written as just `sizeof[T]`. Under consideration; not yet decided.

---

## Future Work

### Inferred type arguments in case expressions

Currently, case expressions require the full parameterized type: `case x: Option[Int]`.
Ideally, the type arguments should be inferrable from the scrutinee's known type,
allowing the shorter form: `case x: Option { ... }`.

Note: inference already works in generic contexts. Inside a generic function, the type
arguments can be omitted when they match the enclosing function's type parameters:

```tl
my_unwrap[a](opt: T[a], default: a) -> a {
    case opt: T {          // T[a] inferred from opt's type
        s: MySome { s.value }
        n: MyNone { default }
    }
}
```
