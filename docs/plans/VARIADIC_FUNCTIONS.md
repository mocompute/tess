# Variadic Functions — Design Rationale

## Status: Implemented

## Problem

Tess needed a way to write functions that accept a variable number of arguments of different types — most importantly, a type-safe `print` function. The existing C FFI variadic mechanism (`c_printf(fmt: CString, ...)`) is untyped and unsafe.

## Design: Trait-Bounded Variadics

The `...Trait` syntax declares a variadic parameter bounded by a trait. The compiler applies the trait's single unary function to each argument at the call site, converting heterogeneous arguments into a homogeneous `Slice` the function body can work with.

```tl
print(args: ...ToString) -> Void { ... }
print(42, "hello", 3.14)
// Desugars to: print([to_string(42), to_string("hello"), to_string(3.14)])
```

### Why trait-bounded?

1. **Type safety** — each argument is checked against the trait bound at compile time
2. **Homogeneous body access** — the function body works with a uniform `Slice[R]`, not heterogeneous types
3. **No runtime dispatch** — trait function calls are resolved and monomorphized at compile time
4. **Composable** — any user-defined trait that meets the constraints can be used as a variadic bound

### Why not homogeneous variadics (`...Int`)?

For same-type collections, `Array[T]` or `Slice[T]` are already the right tool. Adding `...Int` would create two ways to do the same thing without adding expressiveness. The `...Trait` mechanism is strictly more powerful.

### Why not untyped variadics?

C-style untyped variadics (`...`) are inherently unsafe — the function has no way to know the types of its arguments. Tess already supports untyped variadics for C FFI via the `c_` prefix, which is the appropriate escape hatch.

## Trait Requirements

A trait used as a variadic bound must:
1. Declare exactly **one function** (excluding inherited parent functions)
2. That function must be **unary** (one parameter)
3. The return type must be **concrete** (not the type parameter `T`)

These constraints ensure the compiler can:
- Know which function to apply (single function)
- Apply it to each argument independently (unary)
- Pack results into a homogeneous `Slice` (concrete return type)

## Implementation Architecture

### Compiler Pipeline

**Parser** (Phases 1-2):
- `a_type_identifier()` parses `...TraitName` → `NFA(name="...", type_args=[TraitName])`
- Function definitions detect variadic last param, compute arity as `n_fixed + 1`
- `variadic_symbols` hashmap stores base name → `{n_fixed_params, mangled_name, trait_name, module}`
- Call sites: when normal arity lookup fails, checks `variadic_symbols` as fallback
- Cross-module calls (`Module.func(args)`): variadic fallback added to the dot-access path

**Type System** (Phase 3):
- `tl_variadic` monotype tag stores trait name + element type
- `unify_tuple()` treats `tl_variadic` like `tl_ellipsis` (accepts 0+ remaining args)
- All 22+ monotype switch statements updated

**Inference** (Phase 4):
- `resolve_variadic_to_slice()` validates the trait and resolves to `Slice[ReturnType]`
- `infer_named_function_application()` checks trait bounds on variadic args
- `check_trait_bound()` + `find_overload_func()` find the trait implementation

**Transpiler** (Phase 6):
- `generate_funcall_variadic()` emits temporaries for each trait function call
- Builds stack-allocated C array + `Slice` literal
- Zero args: `(Slice_T){NULL, 0}`
- Specialization stores impl function names in `variadic_impl_fns` on the NFA node

### Standard Library

- `Slice[T]` in `builtin.tl` — lightweight struct: `{ v: Ptr[T], size: CSize }`
- `Slice.tl` — iterator interface + helpers (`get`, `is_empty`)
- `ToString.tl` — `ToString` trait + implementations for Int, UInt, Float, Bool, CString, String + `Print.print`/`Print.println`

## Arity Mangling

A variadic function has a fixed mangled arity: `n_fixed + 1`. The slice counts as one parameter. For `print(args: ...ToString)`, the mangled name is `print__1`.

The existence of a variadic function precludes arity overloading for that name in the same module.

## Future Work

- ~~**String interpolation** (`f"value: {x}"`)~~ — implemented (uses `ToString`, not variadics)
- **Spread operator** (`inner(args...)`) — unpack a `Slice` into variadic arguments at a call site
- **Variadic UFCS** — `42.print()` where `print` is variadic with the receiver as a variadic argument
