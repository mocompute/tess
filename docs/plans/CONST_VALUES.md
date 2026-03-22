# Const Values

## Overview

Add support for const value bindings that reject reassignment. Currently, `Const[T]` exists as a type wrapper but is only meaningful inside `Ptr` (i.e., `Ptr[Const[T]]` → `const T*`). This feature makes standalone `Const[T]` meaningful: it prevents reassignment of the binding.

## Motivation

- **Safety/correctness**: Prevent accidental reassignment of bindings that shouldn't change.
- **C interop**: Generate `const` local variables and parameters in transpiled C, giving C compilers optimization hints.
- **Functional patterns**: Enable strictly functional programming patterns where bindings are immutable.

## Syntax

```tl
x: Const := 5           // Const sugar: Const[T] with T inferred
x: Const[Int] := 5      // Explicit type argument

fn foo(x: Const[Int]) -> Int {
    x    // can read, cannot reassign
}
```

`Const` without a type argument is sugar for `Const[T]` where `T` is a fresh type variable. This already works via the existing bare-generic-name instantiation path in `tl_type_registry_parse_type_()` (type.c lines 1197-1203).

## Semantics

### Core rule

A binding whose type is `Const[T]` cannot be the target of reassignment (`=`) or compound assignment (`+=`, `-=`, etc.).

### Unification

`Const[T]` continues to unify freely with `T` for value types. The existing stripping in `tl_type_subs_unify_mono()` handles this — `Const` is stripped before structural comparison. This means:

- Passing a `Const[Int]` value to a function expecting `Int` works (value is copied).
- Returning a `Const[Int]` where `Int` is expected works.
- The caller never needs to know the callee treats a parameter as const.

### Shadowing

Creating a new binding with `:=` that shadows a `Const` binding is allowed. `:=` creates a new binding in a new scope — it is not reassignment.

```tl
x: Const := 5
x := 10       // OK: new binding, shadows the const one
```

### Struct field mutation

If a binding is `Const[T]` where `T` is a struct, field mutation is rejected:

```tl
p: Const[Point] := Point(x = 1, y = 2)
p.x = 10      // ERROR: const violation
```

The check walks the LHS of a reassignment to its root binding and checks for `Const`.

### Orthogonality with pointer const

`Const` on a binding and `Const` inside `Ptr` are orthogonal, matching C semantics exactly:

| Tess | C | Reassign pointer | Mutate pointee |
|------|---|:---:|:---:|
| `x: Ptr[Int]` | `int *x` | yes | yes |
| `x: Ptr[Const[Int]]` | `const int *x` | yes | no |
| `x: Const[Ptr[Int]]` | `int * const x` | no | yes |
| `x: Const[Ptr[Const[Int]]]` | `const int * const x` | no | no |

### Function parameters

Parameters annotated with `Const[T]` cannot be reassigned within the function body. At call sites, `Const` is transparent — callers pass values normally.

```tl
fn foo(x: Const[Int]) -> Int {
    x = 10   // ERROR: const violation
    x
}

foo(42)       // OK: Int unifies with Const[Int]
```

### For-loop variables

The for-loop desugaring unconditionally emits `x: Const := iter_value(...)` for the loop variable. This means:

- **Value iterators**: Loop variable is `Const[T]` — cannot reassign (correct default).
- **Pointer iterators**: Loop variable is `Const[Ptr[T]]` — cannot reassign the pointer, but can mutate through it (correct default).

### Address-of a const binding

Taking the address of a `Const[T]` binding (both explicit `x.&` and implicit UFCS address-of) must produce `Ptr[Const[T]]`, not `Ptr[T]`. This preserves const safety through pointers:

```tl
arr: Const[Array[Int]] := ...
arr.push(42)   // ERROR: push expects Ptr[Array[Int]], gets Ptr[Const[Array[Int]]]
arr.size()     // OK: size expects Ptr[Const[Array[Int]]] or similar read-only signature
```

Without this rule, UFCS implicit address-of would silently bypass const, allowing mutation of a const binding through a method call. The check applies in two places:

1. **Implicit UFCS address-of** (`ufcs_rewrite_call()` in `infer_constraint.c`): when wrapping a value in `Ptr[T]`, if the operand type is `Const[U]`, produce `Ptr[Const[U]]` instead.
2. **Explicit `&` operator** (`infer_unary_op` in `infer_constraint.c`): same logic — if operand is `Const[T]`, result is `Ptr[Const[T]]`.

### Closures

When a lambda captures a `Const[T]` binding, the captured copy retains the `Const[T]` type. The lambda body cannot reassign the captured variable. No special machinery needed — the capture mechanism copies the binding's type, and `Const[T]` propagates naturally.

### Generics

A generic function parameter can be `Const[T]`: e.g., `fn id(x: Const[T]) -> T`. Since unification strips `Const` freely for value types, the caller passes `Int`, the parameter gets `Const[Int]`, and the return type unifies with `Int`. No special handling needed.

If `T` resolves to `Const[U]` from the call site, the parameter would be `Const[Const[U]]`. The unification stripping collapses this — both layers are stripped before structural comparison. This is harmless.

### Tagged unions and pattern matching

Destructuring a `Const` binding via `when` creates new bindings for the matched variants. These new bindings are NOT automatically const — they are fresh bindings created by the pattern match:

```tl
x: Const[Option[Int]] := Some(42)
when x {
    Some(val) { val = 10 }   // OK: val is a new, non-const binding
    None {}
}
```

### Return types

`Const` on a return type (e.g., `fn foo() -> Const[Int]`) is syntactically valid but semantically meaningless — the caller gets a copy. This is allowed without warning, consistent with how `Const` is stripped during unification.

## Implementation

### 1. Reassignment rejection (`infer_constraint.c`)

Two distinct checks in `infer_reassignment()`:

**(a) Direct reassignment of a const binding.** After resolving the LHS binding's type via `tl_polytype_substitute`, check if the resolved monotype is `Const[T]` using `tl_monotype_is_const()`. If so, emit `tl_err_const_violation`. This check applies to the type on `node->assignment.name->type->type` after substitution.

**(b) Field mutation through a const value binding.** Extend `check_const_violation()` to walk the LHS binary-op chain (e.g., `p.x`, `p.a.b.c`) to the root symbol. If the root binding's resolved type is `Const[T]`, reject the mutation. This handles arbitrary nesting depth.

Both plain `=` and compound operators (`+=`, `-=`, etc.) go through `infer_reassignment`, so both are covered.

### 1b. Address-of const preservation (`infer_constraint.c`)

In `ufcs_rewrite_call()` (implicit address-of) and `infer_unary_op` (explicit `&`): after constructing `Ptr[operand_type]`, check if the operand's resolved type is `Const[T]`. If so, produce `Ptr[Const[T]]` instead of `Ptr[T]`. This ensures const safety is not bypassed through pointer-taking.

### 2. Transpilation (`transpile.c`)

Stop stripping standalone `Const[T]`. Generate C `const` qualifiers:

- `Const[Int]` → `const int`
- `Const[Point]` → `const Point`
- `Const[Ptr[Int]]` → `int* const` (const after `*`)
- `Const[Ptr[Const[Int]]]` → `const int* const`
- `Ptr[Const[Int]]` → `const int*` (unchanged, already works)

The tricky case is `Const[Ptr[...]]` — C places `const` after `*` for a const pointer, so the transpiler must detect when the inner type of `Const` is a pointer and adjust placement accordingly.

### 3. For-loop desugaring (parser)

Change the for-loop desugaring to emit `x: Const := iter_value(...)` instead of `x := iter_value(...)` for the loop variable binding. In the parser, this means creating a symbol node `ast_node_create_sym_c(arena, "Const")` and attaching it as the `symbol.annotation` on the loop variable before creating the `let_in` node.

### 4. No changes needed

- **Type annotation parsing**: `Const` and `Const[T]` already parse correctly via the existing type constructor machinery.
- **Unification**: Already strips `Const` before structural comparison.
- **Function parameters**: Type carries `Const`, reassignment check handles enforcement.
- **Error tags**: Reuse `tl_err_const_violation`.
- **Error messages**: `Const[Int]` appearing in error messages is acceptable.

## Tests

### Pass tests (compile and run successfully)

- Const value binding with inference: `x: Const := 5`
- Const value binding with explicit type: `x: Const[Int] := 5`
- Const parameter: function with `Const[Int]` param, called with plain `Int`
- Const struct binding: read fields, pass to functions
- Const pointer binding: `Const[Ptr[Int]]` — can mutate through pointer
- Combined: `Const[Ptr[Const[Int]]]` — read-only through pointer, can't reassign
- Shadowing: new `:=` binding shadows a const binding
- Closure capturing const binding
- For-loop with const variable (both value and pointer iterators)
- Passing const value to non-const parameter
- Tagged union destructuring: `when` on a `Const[Option[T]]` — destructured bindings are mutable
- UFCS read-only method on const binding (method taking `Ptr[Const[T]]` self)
- Generic function with `Const[T]` parameter

### Fail tests (compiler must reject)

- Reassigning a const value binding
- Compound-assigning a const value binding (`+=`, etc.)
- Mutating a field of a const struct binding
- Compound-assigning a field of a const struct binding (`p.x += 1`)
- Reassigning a const function parameter
- Reassigning a const loop variable
- UFCS mutating method on const binding (method taking `Ptr[T]` self — implicit address-of produces `Ptr[Const[T]]`, rejected)
