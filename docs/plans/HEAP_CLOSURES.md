# Heap-Based Closures for Tess: Design Analysis

## Executive Summary

This document analyzes the feasibility and design options for adding heap-based closures to Tess. The current stack-based closure implementation is efficient but restricts closures from being returned from functions. Heap-based closures would enable functional programming patterns but introduce memory management complexity that must be balanced against Tess's philosophy of explicitness.

**Recommendation:** Implement boxed closures with capture-by-value semantics and explicit allocation, preserving Tess's predictable, C-like memory model.

---

## Current Implementation Analysis

### How Stack Closures Work Today

From `transpile.c`, closures are implemented via:

1. **Context Struct Generation** - For each unique set of free variables, a struct is generated:
   ```c
   typedef struct tl_ctx_7968878838695696149 {
       int* tl_x1_v3;  // POINTER to captured variable
   } tl_ctx_7968878838695696149;
   ```

2. **Lambda Signature Transformation** - Lambdas receive context as first parameter:
   ```c
   static int tl_lambda(tl_ctx_7968878838695696149* tl_ctx) {
       return (*tl_ctx->tl_x1_v3);  // Dereference to access
   }
   ```

3. **Context Initialization** - Takes addresses of stack variables:
   ```c
   tl_ctx_7968878838695696149 ctx = {.tl_x1_v3 = &(tl_x1_v3)};
   ```

### Why Returning Closures Fails

The context struct is stack-allocated and contains **pointers to other stack locations**. When the function returns:
- The context struct is deallocated
- The captured variables are deallocated
- Any pointers into them become dangling

This is enforced in `infer.c` with `tl_err_cannot_return_lambda`.

### Current Workaround: Function Pointers

Named functions without captures can be returned:
```tl
add1(x) { x + 1 }
get_fn() { add1/1 }  // OK: returns function pointer
```

But this doesn't work when state must be captured.

---

## The Fundamental Tension

Tess has these design values that are in tension:

| Value | Current Closures | Heap Closures Would... |
|-------|------------------|------------------------|
| **Explicit allocation** | No allocation | Require allocation |
| **Capture by reference** | Enables mutation visibility | Create dangling pointers if kept |
| **Stack-based efficiency** | No cleanup needed | Require cleanup strategy |
| **C-like predictability** | Clear memory model | Add hidden complexity |

You cannot have all of: (1) capture-by-reference, (2) heap allocation, (3) safe returns, and (4) no lifecycle management. Something must give.

---

## Design Alternatives

### Alternative A: Boxed Closures with Capture-by-Value (Recommended)

**Syntax:**
```tl
make_adder(n) -> Box((Int) -> Int) {
  box((x) { x + n })  // n is COPIED into the closure
}

// Usage
adder := make_adder(5)
result := adder(10)   // returns 15
Box.free(adder)       // explicit cleanup
```

**Implementation:**
```tl
// New built-in type
Box(F) : {
  fn:  Ptr(any),   // Function pointer
  ctx: Ptr(any),   // Heap-allocated context (owned)
}
```

**Compiler Changes:**
- `box(lambda)` allocates context via `c_malloc`
- Captured variables are **copied** into context (not referenced)
- Generated code:
  ```c
  // For: box((x) { x + n })
  typedef struct tl_box_ctx_123 {
      int tl_n;  // VALUE, not pointer
  } tl_box_ctx_123;

  tl_box_ctx_123* ctx = malloc(sizeof(tl_box_ctx_123));
  ctx->tl_n = n;  // Copy value
  Box result = {.fn = tl_lambda, .ctx = ctx};
  ```

**Pros:**
- Safe: No dangling pointers possible
- Explicit: Allocation and cleanup are visible
- Predictable: Copy semantics are clear
- Compatible: Works with existing type system

**Cons:**
- Mutations to captured variables not visible (by design)
- Large captures are expensive to copy
- Different type from stack closures
- Manual cleanup required

**Effort Estimate:** Medium - requires parser, type system, and transpiler changes.

---

### Alternative B: Allocator-Parameterized Closures

**Syntax:**
```tl
make_adder(n, alloc: Ptr(Allocator)) -> Fn((Int) -> Int) {
  fn(alloc, (x) { x + n })
}

// Usage
adder := make_adder(5, Alloc.context.managed)
result := Fn.call(adder, 10)
Fn.free(adder)

// Or with arena
arena := Alloc.bump_create(Alloc.context.managed, 4096)
adder := make_adder(5, arena)
// ... use ...
Alloc.bump_destroy(arena)  // adder freed with arena
```

**Pros:**
- Fits existing allocator pattern in `Alloc.tl`
- Flexible: Different allocators for different lifetimes
- Arena-friendly: Bulk deallocation possible

**Cons:**
- Verbose: Allocator must be threaded through
- Still requires capture-by-value for safety
- More complex implementation

**Effort Estimate:** Medium-High - requires all of Alternative A plus allocator integration.

---

### Alternative C: Reference-Counted Closures

**Syntax:**
```tl
make_counter() -> Rc(() -> Int) {
  count := rc(0)  // count must also be Rc for mutation to work
  rc(() {
    count.set(count.get() + 1)
    count.get()
  })
}

counter := make_counter()
counter.call()  // 1
counter2 := Rc.clone(counter)
counter2.call()  // 2
// Freed when last reference dropped
```

**Pros:**
- Automatic memory management
- Shared mutable state possible (via `Rc(T)` for captured values)
- No explicit free needed

**Cons:**
- Runtime overhead (refcount operations)
- Cycles cause leaks
- Requires `Rc(T)` wrapper for mutable captured values
- Significant departure from Tess philosophy

**Effort Estimate:** High - requires reference counting infrastructure.

---

### Alternative D: Escape Analysis (Implicit Boxing)

**Syntax:**
```tl
make_adder(n) {
  (x) { x + n }  // Compiler detects escape, auto-boxes
}
```

The compiler analyzes whether a closure escapes its defining scope and automatically heap-allocates if needed.

**Pros:**
- Clean syntax: No annotation needed
- Optimal: Only heap-allocates when necessary
- Backward compatible

**Cons:**
- Hidden allocation (violates explicitness)
- Complex implementation (escape analysis is hard)
- Unpredictable: Small code changes can change allocation
- Deallocation problem: Who frees? Requires GC or Rc.

**Effort Estimate:** Very High - requires sophisticated analysis and automatic memory management.

---

### Alternative E: No Language Change (Manual Pattern)

Continue with current restrictions. Users implement closures manually:

```tl
Adder : {
  n: Int,
}

adder_call(self: Ptr(Adder), x: Int) -> Int {
  self->n + x
}

make_adder(n: Int) -> Ptr(Adder) {
  a : Ptr(Adder) := c_malloc(sizeof(Adder))
  a->n = n
  a
}

// Usage
adder := make_adder(5)
result := adder_call(adder, 10)
c_free(adder)
```

**Pros:**
- No implementation effort
- Fully explicit
- Already works today

**Cons:**
- Verbose: Lots of boilerplate
- Error-prone: Easy to forget cleanup
- Not composable: Can't easily pass to generic higher-order functions

---

## Capture Semantics Deep Dive

### By Reference (Current)
```tl
x := 1
f := () { x }
x = 2
f()  // returns 2 - mutation visible
```
- Enables mutation visibility
- Creates dangling pointer risk for heap closures
- Only safe for stack-local closures

### By Value (Proposed for Box)
```tl
x := 1
f := box(() { x })  // x is COPIED
x = 2
f()  // returns 1 - mutation NOT visible
```
- Safe for heap closures
- Mutations not visible (feature, not bug - heap closures shouldn't depend on stack state)
- Expensive for large values

### Explicit Capture (Possible Enhancement)
```tl
x := 1
y := large_struct
// Explicit capture list
f := box([x, y.&]() { x + y->field })
```
- Granular control
- More verbose
- Similar to C++ lambda captures

---

## What Would Heap Closures Enable?

### 1. Callback Registration
```tl
// Currently impossible
register_handler(event, make_handler(state))

// With heap closures
register_handler(event, box((e) { handle_with_state(state, e) }))
```

### 2. Partial Application / Currying
```tl
// Currently impossible
add(x, y) { x + y }
add5 := partial(add, 5)

// With heap closures
partial(f, x) -> Box((Int) -> Int) {
  box((y) { f(x, y) })
}
```

### 3. Iterator Factories
```tl
// Currently impossible
iter := make_iterator(collection)

// With heap closures
make_iterator(coll) -> Box(() -> Option(T)) {
  idx := 0
  box(() {
    if idx < coll.size {
      result := Option_Some(v = coll.v[idx])
      idx = idx + 1
      result
    } else {
      Option_None()
    }
  })
}
```

### 4. Builder Patterns
```tl
// Fluent builders that capture configuration
builder()
  .with_option(x)   // Each returns a closure capturing accumulated state
  .with_value(y)
  .build()
```

---

## Implementation Complexity Assessment

| Component | Alternative A | Alternative B | Alternative C | Alternative D |
|-----------|--------------|---------------|---------------|---------------|
| Parser    | Low (new keyword) | Low | Low | None |
| Type System | Medium (Box type) | Medium | High (Rc type) | High (escape analysis) |
| Inference | Low | Medium | High | Very High |
| Transpiler | Medium | Medium-High | High | Very High |
| Runtime | None | None | Medium (refcount) | High (GC or Rc) |
| **Total** | **Medium** | **Medium-High** | **High** | **Very High** |

---

## Recommendation

### Primary Recommendation: Boxed Closures with Explicit Capture Lists

Based on user preferences, implement `box()` with these properties:

1. **Explicit capture list syntax** - Like C++ lambdas: `box([x, y.&, z.rc](...) { ... })`
2. **Optional allocator** - `box(...)` uses default, `box(alloc, ...)` uses custom
3. **New `Box(F)` type** - Distinct from stack closures
4. **Same call syntax** - `boxed(args)` works like `stack_closure(args)`
5. **Manual `Box.free()`** - Explicit cleanup, fits Tess philosophy

### Explicit Capture List Syntax

```tl
x := 42
y := large_struct
counter := 0

// By value (copy): x
// By pointer: y.&  (caller ensures lifetime)
// By Rc (shared ownership): counter.rc

f := box([x, y.&, counter.rc](arg) {
  x + y->field + counter.get() + arg
})

// With custom allocator
g := box(my_alloc, [x](arg) { x + arg })
```

| Capture Syntax | Semantics | When to Use |
|----------------|-----------|-------------|
| `x` | Copy value into closure | Small values, no mutation needed |
| `x.&` | Store pointer | Large values, caller manages lifetime |
| `x.rc` | Wrap in Rc, share ownership | Shared mutable state |

### Generated C Code Example

```tl
n := 5
count := 0
f := box([n, count.rc](x) { count.set(count.get() + 1); x + n })
```

Would generate:

```c
// Rc infrastructure (simplified)
typedef struct { int value; size_t refcount; } Rc_int;

// Context struct
typedef struct tl_box_ctx_123 {
    int tl_n;              // Copied value
    Rc_int* tl_count;      // Shared reference
} tl_box_ctx_123;

// The lambda function
static int tl_lambda_123(tl_box_ctx_123* ctx, int x) {
    ctx->tl_count->value += 1;
    return x + ctx->tl_n;
}

// Box creation
tl_box_ctx_123* ctx = malloc(sizeof(tl_box_ctx_123));
ctx->tl_n = n;                    // Copy
ctx->tl_count = rc_clone(count);  // Increment refcount
Box_int_int f = {.fn = tl_lambda_123, .ctx = ctx};
```

### Allocator Syntax

```tl
// Default allocator (c_malloc)
f := box([x](y) { x + y })

// Custom allocator
f := box(my_arena, [x](y) { x + y })

// Using Alloc module
f := box(Alloc.context.managed, [x](y) { x + y })
```

### Rationale

- **Explicit captures**: Programmer controls exactly what's captured and how
- **Flexible**: By-value for simple cases, by-pointer for performance, Rc for shared state
- **Optional allocator**: Simple default, custom when needed
- **Fits Tess philosophy**: Everything is explicit and visible

### Implementation Complexity

This design is more complex than simple capture-by-value:

| Feature | Complexity |
|---------|------------|
| Capture list parsing | Medium |
| By-value capture | Low |
| By-pointer capture | Low (same as current stack closures) |
| Rc capture | High (requires Rc infrastructure) |
| Optional allocator | Medium |

**Suggested phasing:**
1. Phase 1: `box([x, y](...)` with by-value only
2. Phase 2: Add `.&` pointer captures
3. Phase 3: Add `.rc` Rc captures (requires Rc type infrastructure)

### Secondary Consideration

Given this is exploratory (low priority), I recommend starting with a minimal Phase 1 implementation to validate the design before investing in Rc infrastructure.

---

## Is This Feature Worth Implementing?

### Arguments FOR

1. **Expressiveness**: Enables functional patterns impossible today
   - Partial application, currying
   - Callback-based APIs
   - Iterator factories
   - Builder patterns

2. **Library design**: Generic higher-order functions become much more powerful
   ```tl
   // Currently: must pass data struct + function pointer separately
   // With heap closures: single closure captures everything
   ```

3. **Ergonomics**: Manual closure structs are verbose and error-prone

4. **Competitive parity**: Most modern languages (Rust, Swift, Go, C++) support this

### Arguments AGAINST

1. **Implementation complexity**: Significant compiler work
   - Parser changes for capture lists
   - Type system changes for Box type
   - Transpiler changes for heap allocation
   - Optional: Rc infrastructure

2. **Memory management burden**: Opens can of worms
   - Manual Box.free() is easy to forget
   - Rc adds runtime overhead
   - Cycles cause leaks without weak references

3. **Philosophy tension**: Tess values explicitness and C-like predictability
   - Stack closures are beautiful: no allocation, no cleanup
   - Heap closures add hidden complexity

4. **Workarounds exist**: Current approach works fine
   ```tl
   // Instead of: make_adder(n) { box([n](x) { x + n }) }
   // Can do:
   Adder : { n: Int }
   adder_call(self: Ptr(Adder), x: Int) { self->n + x }
   make_adder(n) -> Ptr(Adder) {
     a : Ptr(Adder) := c_malloc(sizeof(Adder))
     a->n = n
     a
   }
   ```

5. **Scope creep**: Once you have heap closures, users will want:
   - Automatic cleanup (Rc everywhere)
   - Trait objects / dynamic dispatch
   - More sophisticated type system features

### Assessment

**For a low-priority exploratory feature**, I recommend:

1. **Document the manual pattern** in LANGUAGE_REFERENCE.md as the idiomatic way to create "closure-like" objects

2. **Consider a minimal Phase 1** as an experiment:
   - `box([x, y](args) { body })` with by-value only
   - Manual `Box.free()`
   - Default allocator only
   - This validates the design with minimal investment

3. **Defer Rc captures** until there's clear demand - they require significant infrastructure

4. **Accept the current restriction** as a valid design choice that keeps Tess simple and predictable

---

## Files That Would Need Changes

For Alternative A:

| File | Changes |
|------|---------|
| `src/tess/src/parser.c` | Parse `box(lambda)` expression |
| `src/tess/include/ast.h` | Add AST node for boxed lambda |
| `src/tess/src/infer.c` | Type inference for Box type, remove return restriction for boxed lambdas |
| `src/tess/src/type.c` | Add Box type constructor |
| `src/tess/src/transpile.c` | Generate heap allocation, copy captures, free function |
| `src/tl/std/` | Add Box.tl with Box type and Box.free() |
| `docs/LANGUAGE_REFERENCE.md` | Document new feature |

---

## Verification Plan (if implemented)

1. Add test cases for basic boxed closures
2. Test capture-by-value semantics (mutations not visible)
3. Test returning boxed closures from functions
4. Test passing boxed closures to higher-order functions
5. Test Box.free() cleanup
6. Verify no memory leaks with valgrind/asan
7. Test interaction with generics and specialization

---

## Conclusion

Heap-based closures are **feasible** for Tess but require careful design to maintain the language's philosophy of explicitness and predictability.

**The recommended design:**
- Explicit `box([captures](args) { body })` syntax
- Capture list with by-value, by-pointer, and optionally by-Rc modes
- Optional allocator parameter
- Manual cleanup with `Box.free()`

**The trade-off:**
- **Benefit**: Enables functional programming patterns, better library design
- **Cost**: Implementation complexity, memory management burden, potential scope creep

**For exploratory purposes**, the current restriction (lambdas cannot be returned) is a valid design choice. The manual closure pattern works and keeps Tess simple. If heap closures become essential, a minimal Phase 1 with by-value-only captures would validate the design with modest investment.

**Key insight**: The fundamental tension is between capture-by-reference (which Tess currently uses for mutation visibility) and heap allocation (which requires value or Rc semantics to avoid dangling pointers). Any solution must resolve this tension explicitly.
