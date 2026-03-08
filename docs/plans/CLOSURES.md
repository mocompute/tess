# Allocated Closures for Tess

## Summary

All closures in Tess produce a `Closure[F]` type — a struct containing a function pointer and a context pointer. The calling convention is unified: the context is always passed as the first argument.

The difference between stack and allocated closures is **where the context lives** and **how captures work**:

1. **Stack closures** (existing) — implicit capture by reference, context on the stack, cannot be returned.
2. **Allocated closures** (new) — explicit capture by value via `[[alloc, capture(...)]]` attributes, context on the heap, can be returned and stored freely.

Because both kinds share the same `Closure[F]` type and calling convention, higher-order functions accept closures uniformly — no overloading or wrapping needed.

---

## Design

### Syntax

```tl
// Stack closure (existing) — implicit capture by reference
f := (x) { x + n }

// Allocated closure — explicit capture by value, default allocator
g := [[alloc, capture(n)]] (x) { x + n }

// Allocated closure — explicit allocator
h := [[alloc(my_arena), capture(n)]] (x) { x + n }

// Allocated closure — no captures
k := [[alloc]] () { 42 }
```

### Rules

| Rule | Stack Closure | Allocated Closure |
|------|---------------|-------------------|
| Attribute | None | `[[alloc]]` required |
| Capture declaration | Implicit | `[[capture(...)]]` required (unless no captures) |
| Capture semantics | By reference (pointer to stack var) | By value (copied into heap struct) |
| Type | `Closure[F]` | `Closure[F]` (same) |
| Can be returned | No | Yes |
| Context lifetime | Bound to enclosing scope | Until freed (manually or via arena) |
| Memory management | Automatic (stack) | Explicit (allocator / arena) |

### Compiler enforcement

- `[[alloc]]` without `[[capture(...)]]` — allowed only if the body references no outer variables
- Body references a variable not listed in `capture(...)` and not a parameter — error
- `[[capture(...)]]` without `[[alloc]]` — error
- Returning a stack closure — error (unchanged)
- Returning an allocated closure — allowed

---

## The Closure Type

`Closure[F]` is a **phantom-typed** struct. The type parameter `F` is checked by the Tess type system but erased at runtime. The C representation is a single monomorphic struct regardless of `F`:

```tl
// Conceptual definition
Closure[F] : {
  fn:  Ptr[any],   // pointer to the generated function
  ctx: Ptr[any],   // pointer to captured state (stack or heap)
}
```

At the C level, both fields are `void*`. The compiler retains `F` during transpilation to generate correct casts:
- At the storage site: the concrete function pointer is cast to `void*`
- At the call site: `fn` is cast back to the correct function pointer type derived from `F`

Type safety is enforced entirely by the Tess type checker.

### Unified calling convention

All closures — stack and allocated — are called the same way:

```tl
f: Closure[(Int) -> Int] = ...
result := f(10)
```

The compiler generates an indirect call, passing the context as the first argument:

```c
// Generated C
result = ((int (*)(void*, int))f.fn)(f.ctx, 10);
```

This means higher-order functions work uniformly with both kinds of closures:

```tl
map[T, R](list: List[T], f: Closure[(T) -> R]) -> List[R] { ... }

// Works with stack closures
map(my_list, (x) { x + 1 })

// Works with allocated closures
adder := [[alloc, capture(n)]] (x) { x + n }
map(my_list, adder)
```

### Migration from current implementation

Currently, stack closures have arrow types like `(Int) -> Int` and use direct calls with inlined context passing. Under the unified model:

- All closures get type `Closure[F]` instead of bare arrow types
- All closure calls go through the `{ fn, ctx }` struct indirection
- The cost is one extra pointer dereference per closure call
- The C compiler will often inline or devirtualize this in practice

---

## Capture Semantics

### Stack closures: capture by reference (unchanged)

```tl
n := 5
f := (x) { x + n }
n = 10
f(1)   // returns 11 — n captured by reference, mutation visible
```

### Allocated closures: capture by value

```tl
n := 5
f := [[alloc, capture(n)]] (x) { x + n }
n = 10
f(1)   // returns 6 — n was copied at closure creation
```

### Capturing pointers for shared mutable state

To share mutable state between multiple closures, capture a pointer:

```tl
make_counter(start: Int, arena: Ptr[Allocator]) -> CounterOps {
  state: Ptr[Int] := Alloc.create(arena, Int)
  state.* = start

  inc := [[alloc(arena), capture(state)]] () {
    state.* = state.* + 1
    state.*
  }
  get := [[alloc(arena), capture(state)]] () { state.* }

  CounterOps(inc = inc, get = get)
}

CounterOps : {
  inc: Closure[() -> Int],
  get: Closure[() -> Int],
}

// Usage
c := make_counter(0, arena)
c.inc()   // 1
c.inc()   // 2
c.get()   // 2
// Destroy arena -> frees state + both closures
```

Here `state` is a `Ptr[Int]`. Each closure captures the pointer by value (copies the pointer), so both closures point to the same heap-allocated integer.

### Capturing a Closure

A closure can capture another closure by value:

```tl
partial(f: Closure[(Int, Int) -> Int], x: Int) -> Closure[(Int) -> Int] {
  [[alloc, capture(f, x)]] (y) { f(x, y) }
}
```

This copies the entire `Closure` struct (function pointer + context pointer) into the outer context. The outer closure now holds its own copy of the inner closure's `ctx` pointer. If the inner closure's context is freed independently, the outer closure's copy becomes dangling. In practice, using an arena for both closures avoids this — they share the same lifetime.

---

## Memory Management

The `Closure` struct itself is a value type (typically stack-allocated or embedded in another struct). Only the `ctx` field points to an allocation. For stack closures, `ctx` points to the stack and needs no cleanup. For allocated closures, `ctx` points to the heap and must be freed.

### Arena pattern (recommended)

In practice, allocated closures should use an arena. This avoids tracking individual closure lifetimes:

```tl
arena := Alloc.arena_create(4096)
f := [[alloc(arena), capture(x)]] (y) { x + y }
g := [[alloc(arena), capture(x, z)]] (y) { x + z + y }
// ... use f and g ...
Alloc.arena_destroy(arena)   // frees all context allocations at once
```

### Default allocator

`[[alloc]]` with no argument uses the default allocator from `Alloc.context`:

```tl
f := [[alloc, capture(x)]] (y) { x + y }
```

The context can be freed individually. The exact API is TBD — it may be a compiler-recognized `Closure.free(f)` that emits `free(f.ctx)`, or it may use the existing `Alloc` interfaces on `f.ctx` directly.

### Explicit allocator

`[[alloc(expr)]]` where `expr` evaluates to a `Ptr[Allocator]`. The expression is evaluated at closure creation time in the enclosing scope — it is **not** a captured variable (it is used for allocation, not stored in the context).

```tl
f := [[alloc(my_arena), capture(x)]] (y) { x + y }
// Freed when arena is destroyed
```

The allocator expression must have type `Ptr[Allocator]`. This is enforced during type inference.

---

## Generated C Code

### Stack closure

```tl
n := 5
f := (x) { x + n }
f(10)
```

```c
// Context struct — pointer fields (references stack variables)
typedef struct tl_ctx_<hash> {
    int* tl_n;
} tl_ctx_<hash>;

// The function
static int tl_closure_<hash>(void* tl_ctx_raw, int tl_x) {
    tl_ctx_<hash>* tl_ctx = (tl_ctx_<hash>*)tl_ctx_raw;
    return tl_x + (*tl_ctx->tl_n);  // dereference pointer
}

// Context on stack, points to stack variable
tl_ctx_<hash> ctx = { .tl_n = &tl_n };
tl_Closure f = { .fn = (void*)tl_closure_<hash>, .ctx = (void*)&ctx };

// Call site
((int (*)(void*, int))f.fn)(f.ctx, 10);
```

### Allocated closure

```tl
n := 5
f := [[alloc, capture(n)]] (x) { x + n }
f(10)
```

```c
// Context struct — value fields (copies of captured values)
typedef struct tl_closure_ctx_<hash> {
    int tl_n;
} tl_closure_ctx_<hash>;

// The function
static int tl_closure_<hash>(void* tl_ctx_raw, int tl_x) {
    tl_closure_ctx_<hash>* tl_ctx = (tl_closure_ctx_<hash>*)tl_ctx_raw;
    return tl_x + tl_ctx->tl_n;  // direct access
}

// Context on heap, values copied in
tl_closure_ctx_<hash>* ctx = alloc(sizeof(tl_closure_ctx_<hash>));
ctx->tl_n = tl_n;
tl_Closure f = { .fn = (void*)tl_closure_<hash>, .ctx = (void*)ctx };

// Call site — identical to stack closure
((int (*)(void*, int))f.fn)(f.ctx, 10);
```

The context struct hash must incorporate the closure kind (stack vs heap) to avoid collisions, since a stack closure capturing `n: Int` produces `int* tl_n` while an allocated closure produces `int tl_n`.

---

## What This Enables

### Returning closures from functions
```tl
make_adder(n: Int) -> Closure[(Int) -> Int] {
  [[alloc, capture(n)]] (x) { x + n }
}

add5 := make_adder(5)
add5(10)   // 15
```

### Uniform higher-order functions
```tl
// One function accepts both stack and allocated closures
map[T, R](list: List[T], f: Closure[(T) -> R]) -> List[R] { ... }
```

### Callback registration
```tl
register_handler(event, [[alloc, capture(state)]] (e) {
  handle_with_state(state, e)
})
```

### Simulating objects (closures over shared state)
```tl
// See the CounterOps example above — multiple closures
// sharing a pointer to mutable state
```

### Partial application
```tl
partial(f: Closure[(Int, Int) -> Int], x: Int) -> Closure[(Int) -> Int] {
  [[alloc, capture(f, x)]] (y) { f(x, y) }
}
```

---

## Comparison: Before and After

### Before (manual pattern, works today)

```tl
Adder : { n: Int }

adder_call(self: Ptr[Adder], x: Int) -> Int { self->n + x }

make_adder(n: Int) -> Ptr[Adder] {
  a: Ptr[Adder] := c_malloc(sizeof(Adder))
  a->n = n
  a
}

// Usage
adder := make_adder(5)
result := adder_call(adder, 10)
c_free(adder)
```

### After (allocated closures)

```tl
make_adder(n: Int) -> Closure[(Int) -> Int] {
  [[alloc, capture(n)]] (x) { x + n }
}

// Usage
adder := make_adder(5)
result := adder(10)
// freed via arena or Closure.free(adder)
```

---

## Implementation Concerns

### Parser: `[[...]]` on lambdas requires a new grammar production

The existing `[[attribute]]` syntax attaches to identifiers only (`a_attributed_identifier`). Lambdas are a separate production in `a_value`. A new grammar rule is needed so that `[[...]] (params) { body }` is parsed as an attributed lambda. The `capture(...)` and `alloc(...)` forms should parse naturally since `a_attribute_set` already handles funcall-like syntax inside `[[...]]`.

### AST: `ast_lambda_function` needs attribute fields

The current `ast_lambda_function` struct has no field for attributes. It needs to be extended to store the attribute set. The `maybe_wrap_lambda_function_in_let_in` transformation must preserve these attributes through the wrapping.

### Transpiler: unified calling convention (migration)

The current stack closure implementation uses direct calls with a known C function name and inlined context passing. Under the unified model, all closures go through the `{ fn, ctx }` indirection. This requires modifying the existing stack closure code generation to:
- Emit `tl_Closure` structs instead of raw context + direct calls
- Use indirect calls at all closure call sites
- Cast function pointers through `void*`

### Transpiler: two context struct modes

Stack closures generate context structs with **pointer fields** (`int* tl_n`) and **dereference access** (`*tl_ctx->tl_n`). Allocated closures generate **value fields** (`int tl_n`) and **direct access** (`tl_ctx->tl_n`). These are parallel code paths in:
- Context struct generation (`generate_context_struct`)
- Context initialization (`generate_context`)
- Field access in the function body

### Type inference: generic captures

A closure inside a generic function may capture a value of generic type `T`. The context struct cannot be generated until specialization resolves `T` to a concrete type. The capture list attributes must survive AST cloning during specialization — this requires `ast_node_clone` to handle the new attribute fields on `ast_lambda_function`.

---

## Future Work (Deferred)

### Vtable ergonomics

Self-referential structs where closures capture a pointer to their containing struct — essentially objects with methods and state. The allocated closure foundation supports this pattern (see CounterOps example), but dedicated syntax for construction could be explored later.

### Capture-by-reference for allocated closures

Currently not supported. All allocated captures are by value. If needed, the user can capture a pointer (which is copied by value) to achieve reference semantics. A `.&` capture mode could be added later but introduces lifetime concerns.

### Rc captures

Shared ownership via reference counting (`capture(x.rc)`). Deferred until there is clear demand — requires Rc infrastructure.

### Closure.free API

The exact mechanism for individually freeing a closure's context needs to be designed. Options include a compiler-recognized `Closure.free(f)`, exposing `f.ctx` directly, or relying on arenas for all lifetime management.

---

## Files That Would Need Changes

| File | Changes |
|------|---------|
| `src/tess/src/parser.c` | Parse `[[alloc]]` and `[[capture(...)]]` attributes on lambdas |
| `src/tess/include/ast.h` | Add attribute fields to `ast_lambda_function` node |
| `src/tess/src/infer.c` | Produce `Closure[F]` for all closures (unified type) |
| `src/tess/src/infer_constraint.c` | Allow returning allocated closures, enforce capture list rules |
| `src/tess/src/type.c` | Add `Closure` type constructor |
| `src/tess/src/transpile.c` | Unified calling convention, heap allocation, value copies, two context struct modes |
| `src/tl/std/` | `Closure` type definition, integration with `Alloc` |
| `docs/LANGUAGE_REFERENCE.md` | Document allocated closures |
