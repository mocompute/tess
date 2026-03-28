# Const Values Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `Const[T]` value bindings that reject reassignment, transpile to C `const`, and preserve const safety through address-of operations.

**Architecture:** Extend the existing `Const[T]` type wrapper (currently only meaningful inside `Ptr`) to also enforce reassignment rejection when applied to value bindings. Three compiler phases are touched: inference (reassignment + address-of checks), transpilation (C const generation), and parsing (for-loop desugaring). Unification is unchanged — `Const[T]` already strips freely for value types.

**Tech Stack:** C11 (compiler), Tess `.tl` (tests)

**Spec:** `docs/plans/CONST_VALUES.md`

---

### Task 1: Fail tests — reassign a const value binding

**Files:**
- Create: `src/tess/tl/test/fail/test_fail_const_value_reassign.tl`
- Create: `src/tess/tl/test/fail/test_fail_const_value_reassign_inferred.tl`

- [ ] **Step 1: Write the fail tests — one with explicit type, one with bare Const**

`test_fail_const_value_reassign.tl`:
```tl
#module main

main() {
    x: Const[Int] := 5
    x = 10
    x
}
```

`test_fail_const_value_reassign_inferred.tl`:
```tl
#module main

main() {
    x: Const := 5
    x = 10
    x
}
```

- [ ] **Step 2: Run the tests — expect them to fail (compiler should reject but currently doesn't)**

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_reassign.tl`
Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_reassign_inferred.tl`
Expected: Currently compiles successfully (the test itself fails the test harness because the compiler doesn't reject it yet). Move to `test/known_fail_failures/` if needed.

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/fail/test_fail_const_value_reassign.tl src/tess/tl/test/fail/test_fail_const_value_reassign_inferred.tl
git commit -m "test: add fail tests for const value reassignment"
```

---

### Task 2: Reject reassignment of Const[T] bindings

**Files:**
- Modify: `src/tess/src/infer_constraint.c` — `infer_reassignment()` (around line 1607)

- [ ] **Step 1: Add const value check in `infer_reassignment()`**

After the existing `check_const_violation` call (line 1617-1620), add a new check for direct const value reassignment. Insert after line 1620, before the "reassignment nodes have void type" comment:

```c
    // Check for const value reassignment: the LHS binding itself is Const[T]
    if (node->assignment.name->type && node->assignment.name->type->type) {
        tl_polytype_substitute(self->arena, node->assignment.name->type, self->subs);
        tl_monotype *lhs_type = node->assignment.name->type->type;
        if (tl_monotype_is_const(lhs_type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_const_violation, .node = node}));
            return 1;
        }
    }
```

Note: the `tl_polytype_substitute` call on line 1629 already substitutes this type later, but we need it resolved NOW for the const check. The duplicate substitute is harmless (idempotent).

- [ ] **Step 2: Run the fail test to verify it now fails at compile time**

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_reassign.tl`
Expected: Compiler error (const violation)

- [ ] **Step 3: Run full test suite**

Run: `make -j test`
Expected: All tests pass (no regressions)

- [ ] **Step 4: Commit**

```
git add src/tess/src/infer_constraint.c
git commit -m "feat: reject reassignment of Const[T] value bindings"
```

---

### Task 3: Fail test — compound assignment on const value

**Files:**
- Create: `src/tess/tl/test/fail/test_fail_const_value_compound_assign.tl`

- [ ] **Step 1: Write the fail test**

```tl
#module main

main() {
    x: Const[Int] := 5
    x += 1
    x
}
```

- [ ] **Step 2: Verify it fails at compile time** (should already work from Task 2)

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_compound_assign.tl`
Expected: Compiler error (const violation) — compound assignment also goes through `infer_reassignment`.

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/fail/test_fail_const_value_compound_assign.tl
git commit -m "test: add fail test for const value compound assignment"
```

---

### Task 4: Fail test — struct field mutation through const binding

**Files:**
- Create: `src/tess/tl/test/fail/test_fail_const_value_field_mutation.tl`

- [ ] **Step 1: Write the fail test**

```tl
#module main

Point: { x: Int, y: Int }

main() {
    p: Const[Point] := Point(x = 1, y = 2)
    p.x = 10
    0
}
```

- [ ] **Step 2: Run — expect it to NOT be rejected yet**

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_field_mutation.tl`
Expected: Currently compiles (the `check_const_violation` function only checks for `Ptr[Const[T]]`, not `Const[T]` on the root of a dotted LHS).

- [ ] **Step 3: Commit the test (will be fixed in next task)**

```
git add src/tess/tl/test/fail/test_fail_const_value_field_mutation.tl
git commit -m "test: add fail test for field mutation through const value binding"
```

---

### Task 5: Reject field mutation through Const[T] bindings

**Files:**
- Modify: `src/tess/src/infer_constraint.c` — `check_const_violation()` (around line 1578)

- [ ] **Step 1: Extend `check_const_violation` to walk to root binding**

First, the function signature needs `self` to be usable (currently suppressed with `(void)self`). Remove the `(void)self;` line at the top of the function.

Then, after the existing `ast_binary_op` check (after line 1602), add logic to walk the LHS chain to the root symbol and check if it's `Const[T]`:

```c
    // Walk binary-op chain (struct access / index) to root symbol.
    // If the root binding is Const[T], reject the mutation.
    {
        ast_node *cur = lhs;
        while (cur && cur->tag == ast_binary_op) {
            str         op   = ast_node_str(cur->binary_op.op);
            char const *op_s = str_cstr(&op);
            if (is_struct_access_operator(op_s) || is_index_operator(op_s))
                cur = cur->binary_op.left;
            else
                break;
        }
        if (cur && cur != lhs && cur->type && cur->type->type) {
            tl_polytype_substitute(self->arena, cur->type, self->subs);
            tl_monotype *root_type = cur->type->type;
            if (tl_monotype_is_const(root_type)) return 1;
        }
    }
```

The `tl_polytype_substitute` call ensures the root's type is fully resolved before checking for `Const`. The `cur != lhs` guard ensures we only trigger this for dotted/indexed LHS expressions, not plain symbol reassignment (which is handled by Task 2's check).

- [ ] **Step 2: Run the fail test**

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_field_mutation.tl`
Expected: Compiler error (const violation)

- [ ] **Step 3: Run full test suite**

Run: `make -j test`
Expected: All pass

- [ ] **Step 4: Commit**

```
git add src/tess/src/infer_constraint.c
git commit -m "feat: reject field mutation through Const[T] value bindings"
```

---

### Task 6: Fail test — compound field assignment on const struct

**Files:**
- Create: `src/tess/tl/test/fail/test_fail_const_value_field_compound.tl`

- [ ] **Step 1: Write the fail test**

```tl
#module main

Point: { x: Int, y: Int }

main() {
    p: Const[Point] := Point(x = 1, y = 2)
    p.x += 5
    0
}
```

- [ ] **Step 2: Verify it fails** (should already work from Task 5)

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_field_compound.tl`
Expected: Compiler error (const violation)

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/fail/test_fail_const_value_field_compound.tl
git commit -m "test: add fail test for compound field assignment on const struct"
```

---

### Task 7: Fail test — reassign const function parameter

**Files:**
- Create: `src/tess/tl/test/fail/test_fail_const_value_param_reassign.tl`

- [ ] **Step 1: Write the fail test**

```tl
#module main

foo(x: Const[Int]) {
    x = 10
    x
}

main() {
    foo(42)
}
```

- [ ] **Step 2: Verify it fails** (should already work from Task 2 — parameters go through the same reassignment path)

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_param_reassign.tl`
Expected: Compiler error (const violation)

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/fail/test_fail_const_value_param_reassign.tl
git commit -m "test: add fail test for const parameter reassignment"
```

---

### Task 8: Address-of const preservation — explicit `&`

**Files:**
- Modify: `src/tess/src/infer_constraint.c` — `infer_unary_op()` (around line 1712)

- [ ] **Step 1: Write fail test first**

Create `src/tess/tl/test/fail/test_fail_const_value_explicit_addr.tl`:

```tl
#module main

mutate(p: Ptr[Int]) {
    p.* = 99
    0
}

main() {
    x: Const[Int] := 42
    mutate(x.&)
    0
}
```

- [ ] **Step 2: Run — expect it to currently compile (soundness hole)**

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_explicit_addr.tl`
Expected: Currently compiles (address-of doesn't preserve const).

- [ ] **Step 3: Verify if it already works (likely yes)**

The existing `&` operator code at line 1714 does `tl_type_registry_ptr(self->registry, operand->type->type)`. If the operand `x` has type `Const[Int]`, this produces `Ptr[Const[Int]]`. At the call to `mutate(Ptr[Int])`, the existing `check_const_strip_in_call` catches `Ptr[Const[Int]]` → `Ptr[Int]` as a const stripping violation. **No code change should be needed** — the existing infrastructure already handles this because `Const[T]` is part of the type.

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_explicit_addr.tl`

If it already fails (compiler rejects): no code change needed. If it compiles, the `&` operator must be stripping `Const` somewhere — investigate and add a `Const` preservation check in `infer_unary_op`.

- [ ] **Step 4: Run full test suite**

Run: `make -j test`
Expected: All pass

- [ ] **Step 5: Commit**

```
git add src/tess/tl/test/fail/test_fail_const_value_explicit_addr.tl
git commit -m "test: add fail test for explicit address-of const value"
```

---

### Task 9: Address-of const preservation — implicit UFCS

**Files:**
- Modify: `src/tess/src/infer_constraint.c` — `ufcs_rewrite_call()` (around line 2906)
- Create: `src/tess/tl/test/fail/test_fail_const_value_ufcs_mutate.tl`

- [ ] **Step 1: Write the fail test**

Create `src/tess/tl/test/fail/test_fail_const_value_ufcs_mutate.tl`:

```tl
#module main
#import <Array.tl>

main() {
    arr: Const[Array[Int]] := Array[Int].create()
    arr.push(42)
    0
}
```

- [ ] **Step 2: Verify if it already works (likely yes, same reasoning as Task 8)**

The UFCS implicit address-of code (line 2922) does `tl_type_registry_ptr(self->registry, left->type->type)`. If `left->type->type` is `Const[Array[Int]]`, this produces `Ptr[Const[Array[Int]]]`. Then `push(self: Ptr[Array[Int]])` is called with `Ptr[Const[Array[Int]]]`, and the existing `check_const_strip_in_call` catches it.

Note: line 2909 does `tl_monotype_substitute` on the receiver, which replaces type variables but does NOT strip `Const` — only unification strips it.

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_ufcs_mutate.tl`

If it already fails: no code change needed. If it compiles: investigate whether `left->type->type` has lost the `Const` wrapper at this point, and add preservation logic.

- [ ] **Step 3: Run full test suite**

Run: `make -j test`
Expected: All pass

- [ ] **Step 4: Commit**

```
git add src/tess/tl/test/fail/test_fail_const_value_ufcs_mutate.tl
git commit -m "test: add fail test for UFCS mutating method on const binding"
```

---

### Task 10: Transpilation — const value types to C

**Files:**
- Modify: `src/tess/src/transpile.c` — `type_to_c()` (around line 3302)

- [ ] **Step 1: Write a pass test that exercises const transpilation**

Create `src/tess/tl/test/pass/test_const_value.tl`:

```tl
#module main
#import <cstdio.tl>

main() {
    // Basic const value
    x: Const[Int] := 42
    c_printf(c"x = %lld\n", x)

    // Const with inference
    y: Const := 100
    c_printf(c"y = %lld\n", y)

    // Passing const to non-const parameter
    c_printf(c"sum = %lld\n", x + y)

    // Shadowing a const binding
    z: Const := 5
    z := 10
    c_printf(c"z = %lld\n", z)

    0
}
```

- [ ] **Step 2: Run to see current behavior**

Run: `./tess run src/tess/tl/test/pass/test_const_value.tl`
Expected output:
```
x = 42
y = 100
sum = 142
z = 10
```

This might already work since standalone `Const[T]` currently strips to `T` in transpilation. The C code would just not have `const` qualifiers yet.

- [ ] **Step 3: Update `type_to_c` to emit `const` for standalone `Const[T]`**

In `transpile.c`, replace the existing `Const` stripping block (lines 3302-3307):

```c
        else if (tl_monotype_is_const(mono)) {
            tl_monotype *inner = tl_monotype_const_target(mono);
            tl_polytype  wrap  = tl_polytype_wrap(inner);
            if (tl_monotype_is_ptr(inner)) {
                // Const[Ptr[T]] -> T* const  (or const T* const for Const[Ptr[Const[T]]])
                str ptr_c = render_ptr_to_c(self, inner);
                return str_cat(self->transient, ptr_c, S(" const"));
            }
            // Const[T] -> const T
            str inner_c = type_to_c(self, &wrap);
            return str_cat(self->transient, S("const "), inner_c);
        }
```

This handles:
- `Const[Int]` → `const int` (prepend `const `)
- `Const[Ptr[Int]]` → `int* const` (append ` const` after pointer)
- `Const[Ptr[Const[Int]]]` → `const int* const` (`render_ptr_to_c` gives `const int*`, then append ` const`)

- [ ] **Step 4: Run the pass test and verify C output**

Run: `./tess c src/tess/tl/test/pass/test_const_value.tl --no-line-directive`
Check the generated C for `const int` or `const long long` qualifiers on the local variables.

Run: `./tess run src/tess/tl/test/pass/test_const_value.tl`
Expected: Same output as before, but now with proper C `const` in generated code.

- [ ] **Step 5: Run full test suite**

Run: `make -j test`
Expected: All pass

- [ ] **Step 6: Commit**

```
git add src/tess/src/transpile.c src/tess/tl/test/pass/test_const_value.tl
git commit -m "feat: transpile Const[T] to C const qualifiers"
```

---

### Task 11: Pass test — const struct, const pointer, combined, and UFCS read-only

**Files:**
- Create: `src/tess/tl/test/pass/test_const_value_struct.tl`

- [ ] **Step 1: Write the pass test**

```tl
#module main
#import <cstdio.tl>
#import <cstdlib.tl>
#import <Array.tl>

Point: { x: Int, y: Int }

read_point(p) {
    p.x + p.y
}

// Read-only method taking Ptr[Const[Point]]
point_sum(p: Ptr[Const[Point]]) {
    p->x + p->y
}

main() {
    // Const struct: can read fields
    p: Const[Point] := Point(x = 10, y = 20)
    c_printf(c"sum = %lld\n", read_point(p))

    // Const pointer binding: can mutate pointee, can't reassign pointer
    raw: Ptr[Int] := c_malloc(8zu)
    raw.* = 42
    cp: Const[Ptr[Int]] := raw
    cp.* = 99
    c_printf(c"val = %d\n", cp.*)

    // Combined: Const[Ptr[Const[Int]]] — can't reassign, can't mutate pointee, can read
    cpc: Const[Ptr[Const[Int]]] := raw
    c_printf(c"cpc = %d\n", cpc.*)

    c_free(raw)

    // UFCS read-only method on const binding
    arr: Const[Array[Int]] := Array[Int].create()
    c_printf(c"size = %zu\n", arr.size)

    0
}
```

- [ ] **Step 2: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_const_value_struct.tl`
Expected output:
```
sum = 30
val = 99
cpc = 99
size = 0
```

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/pass/test_const_value_struct.tl
git commit -m "test: add pass tests for const struct, const pointer, combined, and UFCS read-only"
```

---

### Task 12: Pass test — const parameter, closure, generics

**Files:**
- Create: `src/tess/tl/test/pass/test_const_value_advanced.tl`

- [ ] **Step 1: Write the pass test**

```tl
#module main
#import <cstdio.tl>

// Const parameter: can't reassign inside, transparent to caller
add_one(x: Const[Int]) {
    x + 1
}

// Generic with const
identity(x: Const) {
    x
}

main() {
    // Const parameter called with plain Int
    c_printf(c"add_one = %lld\n", add_one(5))

    // Generic with Const
    c_printf(c"identity = %lld\n", identity(42))

    // Closure capturing const
    val: Const := 10
    f := fn() { val }
    c_printf(c"closure = %lld\n", f())

    0
}
```

- [ ] **Step 2: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_const_value_advanced.tl`
Expected output:
```
add_one = 6
identity = 42
closure = 10
```

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/pass/test_const_value_advanced.tl
git commit -m "test: add pass tests for const parameters, generics, and closures"
```

---

### Task 13: For-loop desugaring — make loop variables const

**Files:**
- Modify: `src/tess/src/parser_expr.c` — for-loop desugaring (around line 498)

- [ ] **Step 1: Write a fail test for loop variable reassignment**

Create `src/tess/tl/test/fail/test_fail_const_value_loop_reassign.tl`:

```tl
#module main
#import <Array.tl>

main() {
    arr := Array[Int].create()
    arr.push(1)
    arr.push(2)

    for x in arr {
        x = 99
    }

    arr.destroy()
    0
}
```

- [ ] **Step 2: Modify the for-loop desugaring to attach `Const` annotation**

In `parser_expr.c`, at the point where the loop body binding is created (around line 498), add the `Const` annotation to the loop variable:

```c
    ast_node *while_body = null;
    {
        ast_node *lhs = variable;
        // Make loop variables const: attach Const annotation
        if (!lhs->symbol.annotation) {
            lhs->symbol.annotation = ast_node_create_sym_c(self->ast_arena, "Const");
        }
        ast_node *rhs      = is_pointer ? call_iter_ptr : call_iter_value;
        ast_node *for_body = ast_node_create_let_in(self->ast_arena, lhs, rhs, user_body);
        while_body         = for_body;
    }
```

The `!lhs->symbol.annotation` guard preserves any explicit user annotation (e.g., `for x: SomeType in arr`). Note: the spec says "unconditionally" but this guard is an intentional refinement — if the user explicitly annotates a loop variable type, we don't override it with `Const`. In practice, `for x: Type in arr` is extremely rare.

- [ ] **Step 3: Run the fail test**

Run: `./tess run src/tess/tl/test/fail/test_fail_const_value_loop_reassign.tl`
Expected: Compiler error (const violation)

- [ ] **Step 4: Run full test suite**

Run: `make -j test`
Expected: All pass. Existing for-loop tests should still work since `Const` is transparent in unification and the loop variables are typically not reassigned.

**Potential issue:** If any existing test reassigns a loop variable, it will now fail. Check for this and fix if needed.

- [ ] **Step 5: Commit**

```
git add src/tess/src/parser_expr.c src/tess/tl/test/fail/test_fail_const_value_loop_reassign.tl
git commit -m "feat: make for-loop variables const by default"
```

---

### Task 14: Pass test — for-loop with const (value and pointer iterators)

**Files:**
- Create: `src/tess/tl/test/pass/test_const_value_for_loop.tl`

- [ ] **Step 1: Write the pass test**

```tl
#module main
#import <cstdio.tl>
#import <Array.tl>

main() {
    arr := Array[Int].create()
    arr.push(10)
    arr.push(20)
    arr.push(30)

    // Value iterator: loop var is Const[Int]
    sum := 0
    for x in arr {
        sum = sum + x
    }
    c_printf(c"sum = %lld\n", sum)

    // Pointer iterator: loop var is Const[Ptr[Int]], can mutate pointee
    for p.& in arr {
        p.* = p.* + 1
    }

    sum = 0
    for x in arr {
        sum = sum + x
    }
    c_printf(c"sum after increment = %lld\n", sum)

    arr.destroy()
    0
}
```

- [ ] **Step 2: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_const_value_for_loop.tl`
Expected output:
```
sum = 60
sum after increment = 63
```

- [ ] **Step 3: Commit**

```
git add src/tess/tl/test/pass/test_const_value_for_loop.tl
git commit -m "test: add pass tests for const for-loop variables"
```

---

### Task 15: Final integration — full test suite + edge cases

**Files:**
- Create: `src/tess/tl/test/pass/test_const_value_edge_cases.tl`

- [ ] **Step 1: Write edge case pass test**

Uses the built-in `Option[T]` type (auto-imported via `builtin.tl`). The `when` destructuring creates a new binding `s` for the `Some` variant — this binding should be mutable (not const), even though the original `x` is const.

```tl
#module main
#import <cstdio.tl>

main() {
    // Tagged union destructuring: when-bindings are mutable
    x: Const[Option[Int]] := Some(42)
    result := when x {
        s: Some {
            val := s.value
            val = val + 1
            val
        }
        else { 0 }
    }
    c_printf(c"result = %lld\n", result)

    0
}
```

- [ ] **Step 2: Run the edge case test**

Run: `./tess run src/tess/tl/test/pass/test_const_value_edge_cases.tl`
Expected output:
```
result = 43
```

- [ ] **Step 3: Run the full test suite one final time**

Run: `make -j test`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```
git add src/tess/tl/test/pass/test_const_value_edge_cases.tl
git commit -m "test: add edge case tests for const values (tagged unions, pattern matching)"
```

- [ ] **Step 5: Final commit — squash or leave as incremental commits per user preference**

Run: `make -j test` one more time to confirm everything is green.
