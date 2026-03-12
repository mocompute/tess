# UFCS Receiver Coercion — Design Plan

> **Status: Proposed.**

## Motivation

### 1. Mutating APIs require verbose `arr.&` at every call site

Many standard library APIs require a `Ptr[T]` parameter because they mutate their
argument — `Array.push`, `Array.sort`, `Array.free`, `Array.reserve`, etc. When these
are called with UFCS syntax on a value, the caller must explicitly take the address:

```tl
arr := Array.empty[Int]()
Array.push(arr.&, 1)       // module-qualified
Array.push(arr.&, 2)
Array.sort(arr.&, (a, b) { a - b })
Array.free(arr.&)
```

With UFCS, the receiver moves to the left of the dot, but the `&` is still needed:

```tl
arr.push(1)   // ← wrong: UFCS prepends arr, not arr.&
```

Today, there is no way to write `arr.push(1)` when `push` expects `Ptr[Array[T]]`
as its first parameter. The user must fall back to non-UFCS syntax with `arr.&`, or
bind a pointer first:

```tl
p := arr.&
p.push(1)
```

This friction is especially visible in code that chains multiple mutating calls or
builds up arrays element-by-element — the `arr.&` noise obscures the intent.

### 2. `->` UFCS has broken semantics

Currently, `ptr->f(args)` and `ptr.f(args)` both desugar to the same thing in UFCS:
`f(ptr, args)`. This is wrong — the `->` operator means "dereference then access,"
so `ptr->f(args)` should desugar to `f(ptr.*, args)`.

Without this distinction, `->` is redundant with `.` in UFCS context. The two
operators should have different semantics:

| Syntax          | UFCS desugaring   | Receiver passed as |
|-----------------|-------------------|--------------------|
| `val.f(args)`   | `f(val, args)`    | value              |
| `ptr->f(args)`  | `f(ptr.*, args)`  | dereferenced value |

### Goals

**Goal 1 — Implicit address-of on `.` UFCS.** When `val.f(args)` is rewritten to
`f(val, args)`, and `f`'s first parameter is `Ptr[T]` while `val` is a value of
type `T`, implicitly wrap `val` in address-of — as if the user wrote `f(val.&, args)`.

**Goal 2 — Fix `->` UFCS to dereference.** When `ptr->f(args)` triggers UFCS,
desugar to `f(ptr.*, args)` instead of `f(ptr, args)`. Then, if `f` expects
`Ptr[T]`, apply the same implicit address-of from Goal 1 (which round-trips back
to passing the pointer — but through a principled path).

**After this feature:**

```tl
arr := Array.empty[Int]()
arr.push(1)                         // implicit &: push(arr.&, 1)
arr.push(2)
arr.sort((a, b) { a - b })          // implicit &: sort(arr.&, ...)
arr.free()                          // implicit &: free(arr.&)

p := arr.&
p->push(3)                          // deref + implicit &: push(p.*.&, 3) = push(p, 3)
p->free()                           // same
```

The explicit `arr.&` and module-qualified syntax continue to work unchanged.

---

## Design

### Scope

UFCS receiver coercion applies **only** to the UFCS receiver — the first argument
that is prepended during the UFCS rewrite. It does not apply to:

- Regular (non-UFCS) function calls
- Non-first arguments

The coercion has two parts:

1. **`->` dereference**: `ptr->f(args)` prepends `ptr.*` (not `ptr`) as receiver
2. **Implicit address-of**: if the function's first param is `Ptr[S]` and the
   receiver is `S` (a value, not a pointer), wrap in `&`

### Type matching rule

At the UFCS rewrite point, after determining the receiver to prepend:

1. Look up the function's first parameter type
2. If first param is `Ptr[S]` and receiver type is not `Ptr[_]`:
   wrap receiver in implicit address-of
3. If first param is `S` (not `Ptr`) and receiver type is `S`:
   pass as-is (current behavior for `.` UFCS)

**This works before specialization.** Even with unresolved type variables, the `Ptr`
constructor is structural:

- `push[T](self: Ptr[Array[T]], x: T)` — first param is `Ptr[Array[tv]]`
- Receiver `arr` has type `Array[tv]`
- The `Ptr` wrapper is visible regardless of what `T` resolves to

### What this does NOT do

- No general implicit `T` → `Ptr[T]` coercion anywhere in the type system
- No changes to the unification algorithm
- No effect on non-UFCS calls — `Array.push(arr, 1)` still fails; you must write
  `Array.push(arr.&, 1)` as before

---

## Implementation

### Where: `infer_struct_access()` in `infer_constraint.c`

The UFCS rewrite already happens here (approx. lines 2599–2621). The current flow:

```
1. Field not found on struct
2. Right side is an NFA (named function application)
3. Mangle name for arity+1, look up function
4. Prepend receiver (left) to args
5. Rewrite node to NFA
6. Call infer_named_function_application()
```

The changes insert between steps 3 and 4:

```
3a. Look up the function's polytype, extract first parameter's monotype
3b. Coerce receiver based on operator and first param type:
      "."  + first param is Ptr[S] + receiver is S     → wrap in &
      "->" + first param is S     + receiver is Ptr[S] → wrap in * (deref)
      otherwise                                        → pass as-is
```

### Fix: `->` UFCS receiver selection (step 3a)

Currently, both `.` and `->` UFCS prepend `left` as-is. For `->`, the receiver
should depend on what the function expects:

```c
if (is_arrow_op) {
    if (first_param_is_ptr) {
        // f expects Ptr[T], left is Ptr[T] — pass as-is
        // ptr->push(1) → push(ptr, 1)
    } else {
        // f expects T, left is Ptr[T] — dereference
        // ptr->length_sq() → length_sq(ptr.*)
        ast_node *star  = ast_node_create_sym_c(self->arena, "*");
        ast_node *deref = ast_node_create_unary_op(self->arena, star, left);
        set_node_file(self, deref);
        left = deref;
    }
}
```

The `op` string is already available at the UFCS rewrite point (line 2491).

### AST transformation

To wrap the receiver in address-of:

```c
ast_node *ampersand  = ast_node_create_sym_c(self->arena, "&");
ast_node *addr_of    = ast_node_create_unary_op(self->arena, ampersand, left);
set_node_file(self, addr_of);    // copy source location
// use addr_of instead of left when building new_args
new_args[0] = addr_of;
```

The existing type inference for `&` (in `infer_unary_op`, lines ~1447–1455) will
handle constraining the result type to `Ptr[T]` when `infer_named_function_application`
processes the rewritten node.

### Detecting `Ptr[S]` on the first parameter

At the UFCS rewrite point, the function's polytype is available via:

```c
tl_polytype *fn_type = tl_type_env_lookup(self->env, ufcs_name);
if (!fn_type) fn_type = tl_type_registry_get(self->registry, ufcs_name);
```

This lookup already happens (to verify the function exists). The polytype's body is an
arrow type whose parameter list gives us the first parameter's monotype. Check whether
it is a `Ptr` type constructor:

```c
tl_monotype *first_param = /* extract from arrow */;
int is_ptr = first_param->tag == tl_type_cons_inst
          && str_eq(first_param->cons_inst.def->name, S("Ptr"));
```

For the receiver, check that it is NOT already a pointer (to avoid double-wrapping):

```c
tl_monotype *recv_type = left->type->type;
int recv_is_ptr = recv_type->tag == tl_type_cons_inst
               && str_eq(recv_type->cons_inst.def->name, S("Ptr"));
```

If `is_ptr && !recv_is_ptr`, insert the implicit address-of.

### Edge case: receiver type not yet resolved

The receiver's monotype may still be a type variable at this point (before unification
has fully resolved it). In that case, we cannot determine whether implicit address-of
is needed. Two options:

**Option A — Defer decision.** Skip implicit address-of when receiver type is a
type variable. Let unification fail normally, producing an error. This is conservative
and simple.

**Option B — Always wrap, let unification decide.** Insert the address-of
speculatively, and let the type checker reject it if the types don't match. This is
more complex and could produce confusing error messages.

**Recommendation: Option A.** In practice, UFCS is used on variables with known
struct types (e.g., `arr.push(1)` where `arr` is `Array[Int]`). The receiver type
is almost always resolved before field lookup occurs.

### Combined `.` and `->` receiver coercion

The UFCS rewrite checks the function's first parameter type and the operator to
decide what to prepend:

| Operator | First param   | Receiver             | Action                |
|----------|---------------|----------------------|-----------------------|
| `.`      | `T`           | `val` (type `T`)     | Pass as-is            |
| `.`      | `Ptr[T]`      | `val` (type `T`)     | Wrap in `&`           |
| `->`     | `T`           | `ptr` (type `Ptr[T]`)| Wrap in `*` (deref)   |
| `->`     | `Ptr[T]`      | `ptr` (type `Ptr[T]`)| Pass as-is            |

No double-wrapping (`*.&`) occurs — the check is done once and the right
transformation is applied directly.

---

## Testing

### New test file: `test/pass/test_ufcs_implicit_addr.tl`

Test cases:

1. **Basic implicit address-of**: `arr.push(1)` where `push` expects `Ptr[Array[T]]`
2. **Multiple mutating calls**: `push`, `sort`, `free` in sequence via UFCS
3. **Generic functions**: UFCS with generic receiver, e.g., `arr.push(1)` on `Array[Int]`
4. **Chaining after mutation**: `arr.push(1)` then `arr.size` (verify arr was mutated)
5. **Mixed explicit and implicit**: `arr.push(1)` alongside `Array.push(arr.&, 2)` —
   both work
6. **Non-Ptr first param unchanged**: `arr.map(f)` where `map` takes `Array[T]` (value)
   — no address-of inserted, works as before
7. **Value receiver with value param**: UFCS on functions that take `T` not `Ptr[T]` —
   should work unchanged (existing test_ufcs.tl tests)

### New test file: `test/pass/test_ufcs_arrow_deref.tl`

Test cases for `->` UFCS fix:

1. **`->` with Ptr param**: `ptr->push(1)` where `push` expects `Ptr[T]` — deref
   then implicit address-of round-trips to passing the pointer
2. **`->` with value param**: `ptr->length_sq()` where `length_sq` expects `T` —
   deref passes the value (existing behavior should be preserved)
3. **`.` vs `->` distinction**: same function called both ways, correct receiver
   type in each case

### Negative test: `test/fail/test_ufcs_no_implicit_addr_non_ufcs.tl`

Verify that implicit address-of does NOT apply to regular calls:
```tl
arr := Array.empty[Int]()
Array.push(arr, 1)    // should fail: arr is not Ptr[Array[Int]]
```

---

## Future considerations

- **Lvalue requirement**: Address-of requires an lvalue. UFCS receivers are typically
  local variables (lvalues), but a chained expression like `f().push(1)` would need
  the temporary to be addressable. This may need compiler support for materializing
  temporaries, or it could simply be disallowed (the compiler already rejects `&` on
  non-lvalues).

- **Diagnostics**: When implicit address-of is inserted, consider noting it in verbose
  mode (`-v`) so users can see the transformation.

- **Module-qualified UFCS**: The parser's `maybe_mangle_binop` handles `arr.Array.push(1)`
  style calls. These may need the same treatment if they go through a similar UFCS path
  in inference. Needs investigation.
