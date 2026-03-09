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
tl_closure f = { .fn = (void*)tl_closure_<hash>, .ctx = (void*)&ctx };

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
tl_closure f = { .fn = (void*)tl_closure_<hash>, .ctx = (void*)ctx };

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

## Implementation Status

### Phase 1: Unified calling convention (DONE)

Migrated all closure codegen to the `tl_closure` struct. Commit `81df1ebb`.

**What was done:**
- All Tess functions now receive `void* tl_ctx_raw` as their first parameter
- `tl_closure` struct (`typedef struct tl_closure { void* fn; void* ctx; } tl_closure;`) emitted in all transpiled output
- Arrow-typed toplevel functions wrapped as `(tl_closure){ .fn = (void*)name, .ctx = NULL }`
- Closure calls go through indirect dispatch: `((ret(*)(void*, params...))f.fn)(f.ctx, args...)`
- Stack closures still work with by-reference captures (pointer fields in context struct)
- C FFI integration preserved via `want_raw_fn_ptr` flag (raw function pointers for C calls)
- Closure-to-nil comparison extracts `.fn` field
- Generic closures, HOF passing all work

**Files changed:** `src/tess/src/transpile.c`

### Phase 2: Parser and AST (DONE)

Parse `[[alloc]]` and `[[capture(...)]]` attributes on lambda expressions. Commit `d92a35b6`.

**What was done:**
- Added `attributes` field to `ast_lambda_function` in `ast.h`
- Grammar rule: `[[...]] (params) { body }` parsed as attributed lambda
- `capture(...)` and `alloc(...)` parse naturally since `a_attribute_set` already handles funcall-like syntax
- `maybe_wrap_lambda_function_in_let_in` preserves attributes through wrapping
- `ast_node_clone` handles attribute fields (for generic specialization)
- Type predicate `::` operator works with lambda attributes for runtime attribute checking

**Files changed:** `src/tess/include/ast.h`, `src/tess/src/parser.c`, `src/tess/src/ast.c`, `src/tess/src/infer_constraint.c`

### Phase 3: Type inference and validation (IN PROGRESS)

Enforce capture list rules and modify escape analysis to allow allocated closures.

#### Overview

The key insight: the existing free variable collection (`collect_free_variables_cb` in `infer_alpha.c`) already detects which variables a closure captures. Phase 3 leverages this to validate `[[capture(...)]]` lists and conditionally relaxes escape checking for `[[alloc]]` closures.

#### 3A: Helper — extract alloc/capture attributes from lambda AST (DONE)

Add a utility function to extract structured information from lambda attributes:

```c
// In a new or existing header/source
typedef struct {
    int  has_alloc;          // [[alloc]] or [[alloc(expr)]] present
    int  has_capture;        // [[capture(...)]] present
    ast_node *alloc_expr;    // allocator expression (NULL for default allocator)
    str *capture_names;      // array of captured variable names
    u8   n_capture_names;    // count
} lambda_closure_attrs;

// Parse attributes from ast_lambda_function.attributes
lambda_closure_attrs lambda_get_closure_attrs(allocator *alloc, ast_node *attributes);
```

This walks the `attribute_set` nodes looking for NFA nodes named `alloc` and `capture`. The attribute parser already produces funcall-like AST nodes for `alloc(expr)` and `capture(a, b)`, so this is straightforward extraction.

**Location:** `src/tess/src/infer_constraint.c` (static helper, used in validation and later in transpile)
— or `src/tess/src/ast.c` if it's useful across files.

#### 3B: Validate capture list against free variables (DONE)

After `add_free_variables_to_arrow()` runs (Phase 3 of inference, in `infer_alpha.c`), the arrow type's `fvs` field contains the detected free variables. For lambdas with `[[capture(...)]]`:

1. **Every name in `capture(...)` must be a detected free variable** — error if a listed name is not actually used in the body (prevents dead captures, catches typos).

2. **Every detected free variable must be listed in `capture(...)`** — error if the body references a variable not in the capture list and not a parameter. This is the core safety check.

**Implementation approach:**
- In `add_free_variables_to_arrow()` or a new post-pass, check if the lambda node has `[[capture(...)]]` attributes
- Compare the `capture(...)` names against the collected `fvs` set
- New error codes: `tl_err_capture_missing_variable` (free var not listed), `tl_err_capture_unused_variable` (listed but not free)

**Files:** `src/tess/src/infer_alpha.c`, `src/tess/include/error.h`

#### 3C: Validate alloc/capture attribute combinations (DONE)

Add a validation pass (can run during constraint generation or as a separate post-pass):

| Condition | Result |
|-----------|--------|
| `[[alloc]]` without `[[capture(...)]]` | OK only if body has no free variables (fvs empty) |
| `[[alloc, capture(...)]]` | OK (normal allocated closure) |
| `[[capture(...)]]` without `[[alloc]]` | Error: `tl_err_capture_without_alloc` |
| `[[alloc]]` with free variables but no `[[capture(...)]]` | Error: `tl_err_alloc_missing_capture` |

**Implementation approach:**
- During `infer_lambda_function()` in `infer_constraint.c`, after making the arrow type, extract closure attrs and validate combinations
- Alternatively, validate in a dedicated pass after free variable collection

**Files:** `src/tess/src/infer_constraint.c`, `src/tess/include/error.h`

#### 3D: Validate captured variables are in scope (DONE)

Checks that every name in `[[capture(...)]]` is actually defined in the enclosing lexical scope. Implemented as `check_capture_scope_walk()` in `infer_update.c`, using a custom recursive walk that tracks source-level (pre-alpha-conversion) names.

**Error code:** `tl_err_capture_not_in_scope`
**Test:** `test/fail/test_fail_capture_not_in_scope.tl`
**Files:** `src/tess/src/infer_update.c`, `src/tess/include/error.h`

#### 3E: Modify escape analysis for allocated closures (DONE)

Current behavior in `check_closure_escape()` (`infer_update.c` lines 522-568):
- Rejects returning any capturing closure (explicit `return` or implicit last expression)
- Rejects storing a capturing closure in a struct field

**New behavior:** Skip escape rejection if the closure has `[[alloc]]`:
- An allocated closure's context is on the heap, so it can safely escape
- Need to trace from the escape point back to the originating lambda to check for `[[alloc]]`

**Implementation challenge:** At the escape check points, we have an expression node (e.g., a symbol referencing the closure), not the original lambda AST. We need to find the lambda's attributes from the expression:

1. **For let-in-lambda bindings** (`f := [[alloc, capture(n)]] (x) { ... }`): The symbol `f` resolves to a `let_in` whose value is the lambda — attributes are on `let_in.value->lambda_function.attributes`
2. **For inline lambdas in returns** (`return [[alloc, capture(n)]] (x) { ... }`): The return value IS the lambda node — attributes directly available
3. **For struct fields**: Same tracing — find the originating lambda through its binding

**Approach:** Add a helper `is_allocated_closure(tl_infer *self, ast_node *node)` that:
- If node is a symbol, looks up the toplevel binding and checks for `[[alloc]]` on the lambda
- If node is a lambda directly, checks its attributes
- Returns 1 if `[[alloc]]` is present

Then modify:
- `check_closure_escape_cb()` (line 522): skip error for struct fields if allocated
- Explicit return check (line 536): skip error if allocated
- Implicit return check (line 562): skip error if allocated

**Files:** `src/tess/src/infer_update.c`

#### 3F: Allocator expression type validation (DEFER to Phase 4)

Validating that `[[alloc(expr)]]` has type `Ptr[Allocator]` requires the allocator expression to be type-inferred. Since the expression is in the attribute (not in the normal expression tree), this needs special handling. Defer to Phase 4 when the transpiler processes the allocator expression — at that point types are fully resolved.

#### Test plan

**New pass tests** (in `test/pass/`):
- `test_alloc_closure_return.tl` — return `[[alloc, capture(n)]]` closure from function (currently rejected, should pass)
- `test_alloc_closure_struct.tl` — store `[[alloc, capture(n)]]` closure in struct field (currently rejected, should pass)
- `test_alloc_closure_no_capture.tl` — `[[alloc]]` closure with no free variables (should pass)

**New fail tests** (in `test/fail/`):
- `test_fail_capture_without_alloc.tl` — `[[capture(n)]]` without `[[alloc]]`
- `test_fail_alloc_missing_capture.tl` — `[[alloc]]` on closure that has free vars but no `[[capture(...)]]`
- `test_fail_capture_missing_var.tl` — body uses `n` but `capture(...)` doesn't list it
- `test_fail_capture_unused_var.tl` — `capture(n)` but body doesn't use `n` (optional — may defer)

**Existing tests that must still pass:**
- `test_fail_closure_escape_return.tl` — stack closure return still rejected
- `test_fail_closure_escape_struct.tl` — stack closure in struct still rejected
- `test_lambda_attributes.tl` — attribute parsing and predicate checks
- All existing closure tests (`test_transitive_closure_capture.tl`, `test_closure_hof_capture.tl`, etc.)

#### New error codes needed

```c
// In error.h
X(tl_err_capture_without_alloc, "capture_without_alloc")       // [[capture(...)]] without [[alloc]]
X(tl_err_alloc_missing_capture, "alloc_missing_capture")       // [[alloc]] with free vars, no capture list
X(tl_err_capture_unlisted_var, "capture_unlisted_variable")    // body uses var not in capture list
// Optional:
X(tl_err_capture_unused_var, "capture_unused_variable")        // capture list names var not used in body
```

#### Suggested implementation order

1. **3A** — attribute extraction helper (foundational, used by everything else)
2. **Write fail tests first** — `test_fail_capture_without_alloc.tl`, etc. → put in `known_fail_failures/` initially
3. **3C** — alloc/capture combination validation (simplest validation)
4. **3B** — capture list vs free variable validation
5. **3D** — escape analysis modification (the key payoff — allocated closures can escape)
6. **Write pass tests** — `test_alloc_closure_return.tl`, etc. → initially in `known_failures/`
7. Move tests to final directories as each feature works

**Files:** `src/tess/src/infer_update.c`, `src/tess/src/infer_alpha.c`, `src/tess/src/infer_constraint.c`, `src/tess/include/error.h`

### Phase 4: Transpiler — allocated closure codegen (TODO)

Two-mode context struct generation and heap allocation.

**Tasks:**
- Allocated closure context structs: **value fields** (`int tl_n`) with **direct access** (`tl_ctx->tl_n`)
- Stack closure context structs: unchanged (**pointer fields** with dereference access)
- Heap allocation of context at closure creation (`alloc(sizeof(...))`)
- Copy captured values into heap context
- Support `[[alloc(expr)]]` for explicit allocator
- Support `[[alloc]]` for default allocator (`Alloc.context`)
- Context struct hash must incorporate closure kind to avoid collisions

**Files:** `src/tess/src/transpile.c`

### Phase 5: Standard library (TODO)

Tess-level `Closure[F]` type and allocator integration.

**Tasks:**
- `Closure` type definition in std lib (phantom-typed struct)
- Integration with `Alloc` for default allocator
- `Closure.free` API (design TBD)

**Files:** `src/tl/std/`

---

## Implementation Concerns

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
