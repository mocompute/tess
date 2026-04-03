# C Callbacks

Demonstrates passing Tess functions, lambdas, and closures to C code as
callbacks. Covers four callback patterns:

1. **Non-capturing callbacks** — named functions and non-capturing lambdas
   passed as raw C function pointers (qsort-style)
2. **Generic function pointers** — polymorphic functions specialized with
   explicit type arguments (`identity[CLongLong]/1`, `identity[CDouble]/1`)
3. **Stack-based capturing closures** — lambdas that capture local variables,
   passed to C as `tl_closure*` and called synchronously
4. **Heap-allocated capturing closures** — `[[alloc, capture(...)]]` closures
   passed to C as `tl_closure*`, safe to store and call later

## Project structure

```
callbacks/
  src/main.tl              Tess source — demos and C bindings
  c/callbacks/callbacks.h  C header — function declarations
  c/callbacks/callbacks.c  C implementation — sort, apply, store/run
  Makefile                 Build script
```

## Building and running

```bash
make run
```

This transpiles the Tess source to C, compiles the C callback library,
links them together, and runs the result.

Expected output:

```
--- 1a. Named function as callback ---
sorted: 1, 2, 3, 4, 5
--- 1b. Inline lambda as callback ---
sorted descending: 5, 4, 3, 2, 1
--- 1c. Lambda as local binding ---
sorted: 1, 2, 3, 4, 5
--- 2. Generic function pointer (explicit type args) ---
apply_int_fn(42, identity[CLongLong]/1) = 42
apply_float_fn(3.14, identity[CDouble]/1) = 3.14
--- 3. Stack-based capturing closure ---
apply_op(3, 4, (a,b) a+b+100) = 107
--- 4. Heap-allocated capturing closure ---
run_stored_callback() = 42
```

## Key concepts

### `fun/N` vs `var.&`

Named functions and lambdas use different syntax to obtain a callable pointer:

- **`compare_asc/2`** — takes a raw C function pointer to a named function
  (the `/2` disambiguates by arity). Use when the C parameter is a plain
  function pointer like `int (*cmp)(const void*, const void*)`.

- **`op.&`** — takes the address of a `tl_closure` value (a struct pairing a
  function pointer with a captured-variable context). Use when the C parameter
  is `tl_closure*`.

The Tess binding type determines which mechanism the compiler uses:

```tl
// Arrow type → raw C function pointer (thunk generated, ctx = NULL)
c_sort_ints(arr: Ptr[CLongLong], len: CInt,
            compare: (Ptr[Const[any]], Ptr[Const[any]]) -> CInt) -> Void

// Ptr[any] → closure passed by address (tl_closure* with fn + ctx)
c_apply_op(a: CLongLong, b: CLongLong, op: Ptr[any]) -> CLongLong
```

### Type annotations on closures for C

Because `Ptr[any]` carries no type information, the compiler cannot infer
lambda parameter types from the C function signature. Annotate them explicitly:

```tl
op := (a: CLongLong, b: CLongLong) -> CLongLong { a + b + offset }
```

### The `tl_closure` struct

C code that receives closures calls through the struct:

```c
long long apply_op(long long a, long long b, tl_closure *op) {
    return ((long long (*)(void*, long long, long long))op->fn)(op->ctx, a, b);
}
```

For non-capturing closures, `ctx` is `NULL`. For capturing closures, `ctx`
points to the captured variables (stack-allocated or heap-allocated depending
on whether `[[alloc, capture(...)]]` was used).

## What it demonstrates

- **`#include`** — including a C header from Tess (the header must not define
  `tl_closure`, only forward-declare it)
- **`c_` bindings** — declaring C functions with typed parameters
- **`fun/N`** — function pointer syntax for named and generic functions
- **`var.&`** — address-of on closure bindings for C interop
- **`[[alloc, capture(...)]]`** — heap-allocated closures that outlive their
  creating function
