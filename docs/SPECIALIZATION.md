# Generic Specialization in Tess

This document provides a comprehensive analysis of the generic specialization flow in the Tess compiler.

## Table of Contents

1. [Overview](#overview)
2. [The Inference Pipeline](#the-inference-pipeline)
3. [Specialization Phase in Detail](#specialization-phase-in-detail)
4. [Function Specialization](#function-specialization)
5. [Type Constructor Specialization](#type-constructor-specialization)
6. [Clone and Rename Process](#clone-and-rename-process)
7. [Instance Cache and Naming](#instance-cache-and-naming)
8. [Closures and Lambdas](#closures-and-lambdas)
9. [Edge Cases and Special Handling](#edge-cases-and-special-handling)
10. [Key Data Structures](#key-data-structures)
11. [Key Functions Reference](#key-functions-reference)

---

## Overview

Tess uses **monomorphization** for generics, similar to C++ templates or Rust generics. Every polymorphic function or type is specialized (instantiated) into a concrete, monomorphic version for each unique set of type arguments used at call sites.

**Example:**
```tl
identity(x) { x }

main() {
    identity(42)      // Specializes to: identity_0(x: Int) -> Int
    identity("hello") // Specializes to: identity_1(x: CString) -> CString
}
```

Specialization is implemented across several files:
- **`infer.c`** — Orchestration: runs the 7-phase inference pipeline
- **`infer_specialize.c`** — Core specialization logic (all `specialize_*` functions)
- **`infer_alpha.c`** — Cloning, renaming, free variable collection
- **`infer_constraint.c`** — Type constraint collection and unification
- **`infer_update.c`** — Phase 7 type finalization and validation
- **`infer_internal.h`** — Data structures (`tl_infer`, `traverse_ctx`, etc.)

---

## The Inference Pipeline

Specialization is Phase 5 of a 7-phase pipeline orchestrated in `infer.c`. Understanding the full pipeline is important because specialization depends on earlier phases and feeds into later ones.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Inference Pipeline (infer.c)                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Phase 1: run_alpha_conversion()                                           │
│    Renames all bound variables to globally unique names (x → x_v3).        │
│    Eliminates shadowing. See ALPHA_CONVERSION.md.                          │
│                                                                             │
│  Phase 2: run_load_toplevels()                                             │
│    Loads all toplevel definitions (functions, types, traits) into the       │
│    toplevels hashmap.                                                       │
│                                                                             │
│  Phase 3: run_generic_inference()                                          │
│    Infers types for all generic functions via add_generic(), creating       │
│    provisional arrow types for polymorphic recursion.                       │
│                                                                             │
│  Phase 4: run_check_free_variables()                                       │
│    Checks for unresolved free variable references. Applies substitutions   │
│    to make types concrete.                                                  │
│                                                                             │
│  Phase 5: run_specialize()                     ◄── THIS DOCUMENT          │
│    a. Rewrites operator overloads on user-defined types to function calls  │
│    b. Defaults weak integer literals for stable instance cache keys        │
│    c. Traverses AST starting from main() (or all toplevels for libraries) │
│    d. Specializes module init functions                                     │
│    e. Applies second substitution pass (for weak ints from post_specialize)│
│    f. Removes original generic toplevels (they've been cloned)             │
│                                                                             │
│  Phase 6: run_tree_shake()                                                 │
│    Removes unused toplevels to reduce code size.                           │
│                                                                             │
│  Phase 7: run_update_types()                                               │
│    Finalizes specialized type signatures. Validates all types resolved.    │
│    Checks closure escaping rules. Validates allocator expressions.         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Specialization Phase in Detail

### Entry Point: `run_specialize()` in `infer.c`

The specialization phase starts by traversing the AST from `main()` (or all toplevels for library builds), calling `specialize_applications_cb()` for every node.

### The Traversal Callback: `specialize_applications_cb()`

Located in `infer_specialize.c`. This DFS callback handles multiple node types:

**Symbols (nullary references):**
- Skipped if it's a formal parameter (prevents premature specialization)
- Otherwise calls `specialize_operand()` to look up specialized function versions

**Let-in nodes:**
- Calls `specialize_let_in()`
- For let-in lambdas: looks up specialization created at call sites (two-pass approach)
- For other let-ins: specializes function pointer RHS

**Assignments:**
- Calls `specialize_reassignment()` to specialize function pointers being assigned

**Case statements (pattern matching):**
- Calls `specialize_case()`
- For tagged unions: specializes variant types in conditions
- For binary predicates: creates and specializes predicate arrow

**Named Function Applications (NFAs) — the main case:**
1. Skip if already specialized (`ast_node_is_specialized()` check)
2. Construct callsite arrow type via `make_arrow_with()`
3. Call `specialize_arrow_with_name()` to create specialized instance
4. Call `specialize_arguments()` for function pointer parameters
5. Call `specialize_user_type()` for type constructor arguments

**Lambda applications:**
- Constructs arrow from actual arguments
- Calls `concretize_params()` to assign concrete types to parameters
- Calls `post_specialize()` for the lambda body

---

## Function Specialization

### Arrow Construction: `make_arrow_with()`

Before specializing, the callback constructs a **concrete arrow type** representing the call site:

1. Resolves each argument node to get its concrete monotype
2. Applies substitutions to make types concrete
3. Creates arrow `(arg_types...) -> result_type`
4. Copies free variables from the generic function type to the arrow (critical for closures)

### Core: `specialize_arrow()`

```
specialize_arrow(self, traverse_ctx, name, arrow, resolved_type_args)
    │
    ├── 1. instance_name_exists(name)?  → return name (already an instance)
    ├── 2. instance_lookup_arrow(name, arrow, type_args)?  → return cached name
    ├── 3. toplevel_get(name) exists?  → error if not found
    ├── 4. verify_trait_bounds()  → check type parameter constraints
    ├── 5. next_instantiation(name)  → generate "identity_0", "identity_1", etc.
    ├── 6. instance_add(key, inst_name)  → cache for future lookups
    ├── 7. clone_generic_for_arrow()  → clone AST + erase types + concretize params
    ├── 8. specialized_add_to_env() + toplevel_add()  → register in environment
    └── 9. post_specialize()  → re-infer and recursively specialize body
```

### `specialize_arrow_with_name()` — Wrapper

Called from `specialize_applications_cb()`. Calls `specialize_arrow()` then updates the function name node at the call site to reference the specialized version.

### Post-Specialization: `post_specialize()`

After creating a specialized function, its body must be processed. This is where **recursive specialization** happens:

```
post_specialize(self, traverse_ctx, special, callsite)
    │
    ├── 1. get_infer_target(special)  → extract body from function/lambda
    ├── 2. Set traverse_ctx->result_type to expected return type
    ├── 3. traverse_ast(..., infer_traverse_cb)  → RE-RUN TYPE INFERENCE on body
    │      (with concrete parameter types, fresh type vars for generics)
    ├── 4. apply_subs_to_ast_node()  → make all types concrete
    ├── 5. rewrite_operator_overloads  → convert operators on UDTs to function calls
    └── 6. traverse_ast(..., specialize_applications_cb)  → RECURSIVELY SPECIALIZE
           (any nested generic calls in the now-concrete body)
```

This recursive step is crucial: when specializing `Some(v: Int)`, the body contains nested calls like `__Option__Union_(Some = ...)` that must also be specialized.

### Specializing Arguments: `specialize_arguments()`

When a function takes function pointer arguments, those must also be specialized. For each argument that is a function reference (not a literal value), `specialize_arguments()` specializes it based on the expected parameter type from the arrow.

---

## Type Constructor Specialization

### Entry Point: `specialize_user_type()`

Called when encountering type constructors like `Array[Int]` or `Point[Float]`. Located in `infer_specialize.c`.

**Special handling for union structs:**
- Non-generic unions are skipped entirely
- Generic unions: extract variant types from the expression's *inferred type* (not AST arguments) for correct concrete types

### Core: `specialize_type_constructor_()`

Recursive helper with cycle detection via a `seen` hashset:

```
specialize_type_constructor_(self, name, args, out_type, seen)
    │
    ├── 1. Skip enums (they don't need specialization)
    ├── 2. Normalize type aliases to canonical generic_name
    ├── 3. Cycle detection: if (name, args) in seen → return empty
    ├── 4. Recurse on nested type arguments first
    │      (e.g., Array[Option[Int]] specializes Option[Int] first)
    ├── 5. instance_lookup() → return cached name if exists
    ├── 6. next_instantiation() → generate instance name
    ├── 7. tl_type_registry_specialize_begin() → create specialized monotype
    ├── 8. Clone UTD, rename to instance name
    ├── 9. toplevel_add() + add to environment
    ├── 10. Fixup recursive self-references
    ├── 11. tl_type_registry_specialize_commit()
    └── 12. rename_variables() → erase types on cloned UTD
```

---

## Clone and Rename Process

### `clone_generic_for_arrow()` in `infer_alpha.c`

This is the heart of monomorphization — creating a concrete copy of a generic function:

1. **Clone AST** — `ast_node_clone()` performs a deep copy of the entire function AST
2. **Clear annotations** — Removes any existing type annotations from the clone
3. **Erase all types** — `rename_variables()` erases ALL type information and alpha-converts bound variables with fresh names. This ensures no "type pollution" from the original generic leaks into the specialization
4. **Recalculate free variables** — `add_free_variables_to_arrow()` re-collects free variables with the new renamed symbols
5. **Concretize parameters** — `concretize_params()` maps parameter symbols to their concrete callsite types, inserting them into the environment with resolved types and setting the result type on the function body
6. **Replace toplevel name** — Changes function name to the instance name (e.g., `identity` → `identity_0`)

**Invariant** (checked with `DEBUG_INVARIANTS`): After clone + rename, ALL types in the cloned AST must be null — no residual type information from the generic.

---

## Type Parameter Resolution During Specialization

When a generic function like `reserve[T]` is specialized, its type parameter `T` must be resolved to a concrete type (e.g., `Int`). This happens through a multi-step chain involving `concretize_params`, `traverse_ctx_load_type_arguments`, re-inference, and eventually `specialize_applications_cb` for nested generic calls. Understanding this chain is critical for diagnosing type resolution failures.

### The Two Sources of Type Parameter Bindings

`concretize_params()` tries to bind type parameters from two sources:

1. **Direct lookup** in the outer `type_arguments` map (from `traverse_ctx`). This works when the caller's type arguments use the same alpha-converted names as the clone's type parameters — which they don't, because `rename_variables` gives the clone fresh names. So this path rarely succeeds.

2. **Positional fallback** via `resolved_type_args`. This array is built from explicit type arguments at the call site (e.g., `sizeof[Int]()`). When the call site has no explicit type arguments (e.g., `reserve(self, alloc, count)` called from `_grow`), this array is empty and the fallback does nothing.

When both fail, the type parameter is **not bound** by `concretize_params`. This is the normal case for most generic functions called without explicit type arguments.

### How Type Parameters Get Resolved Anyway

The type parameter is resolved during **re-inference** in `post_specialize()`, through the following chain:

```
post_specialize
  │
  ├── traverse_ast(infer_traverse_cb)          ← RE-INFERENCE
  │     │
  │     ├── ast_let case:
  │     │     └── traverse_ctx_load_type_arguments()
  │     │           For each type parameter of the clone:
  │     │           - If type_param->type is set (by concretize_params): use it
  │     │           - Otherwise: create FRESH type variable, add to ctx->type_arguments
  │     │           Result: ctx->type_arguments has { "tl_T_v278_v662" → tv_fresh }
  │     │
  │     ├── formal parameters traversed:
  │     │     └── process_annotation(self) for each param
  │     │           Parses self's annotation (e.g., Ptr[Array[T]])
  │     │           The "T" in the annotation is looked up in ctx->type_arguments
  │     │           → resolves to tv_fresh
  │     │           Annotation type Ptr[Array[tv_fresh]] is unified with
  │     │           self's concrete type Ptr[Array[Int]] from concretize_params
  │     │           → tv_fresh = Int (recorded in subs)
  │     │
  │     ├── body traversed:
  │     │     When _type_width_aligned[T]() is encountered:
  │     │     └── traverse_ctx_assign_type_arguments()
  │     │           Looks up "T" (alpha-converted) in ctx->type_arguments
  │     │           → finds tv_fresh, which is now bound to Int via subs
  │     │           Adds _type_width_aligned's type param → tv_fresh to context
  │     │
  │     └── infer_named_function_application()
  │           Uses tv_fresh (= Int) to instantiate _type_width_aligned's type
  │
  ├── apply_subs_to_ast_node()                 ← SUBSTITUTE
  │     tv_fresh → Int applied to all AST nodes
  │
  └── traverse_ast(specialize_applications_cb) ← RECURSIVE SPECIALIZATION
        _type_width_aligned[T]() now has T = Int
        → sizeof[Int]() generates sizeof(long long)
```

### The Critical Role of Annotations

The annotation on `self` (e.g., `self: Ptr[Array[T]]`) is the bridge that connects the type parameter `T` to the concrete parameter type. Without it, `T` would remain as an unconstrained fresh type variable through the entire re-inference pass.

**Where annotations come from:** Parameter annotations can be:
- Written explicitly on the implementation: `reserve[T](self: Ptr[Array[T]], ...)`
- Copied from a forward declaration during Phase 2 (`load_toplevel_let`)

Forward declaration annotation copy (in `load_toplevel_let`):
```
Forward decl:  reserve[T](self: Ptr[Array[T]], alloc: Ptr[Allocator], count: CSize) -> Void
Implementation: reserve[T](self, alloc, count: CSize) { ... }
```
For each unannotated parameter in the implementation, `load_toplevel_let` copies both:
- `annotation_type` — the resolved monotype from the forward declaration
- `annotation` — the AST node (e.g., the `Ptr[Array[T]]` subtree)

**Type parameter namespace mismatch:** The forward declaration's `T` and the implementation's `T` are alpha-converted independently, giving different names (e.g., `tl_T_v204` vs `tl_T_v278`). When copying annotations, the type parameter references inside the AST annotation must be **remapped** from the forward declaration's namespace to the implementation's namespace. This is done by `remap_annotation_type_params()`, which matches type parameters by their original (pre-alpha) name and rewrites the alpha-converted names. Without this remapping, `parse_type_annotation` would fail to resolve the type parameter during inference, leaving it as a free variable.

### What Happens When Resolution Fails

If a type parameter is never resolved (stays as a type variable through all phases):

1. During `specialize_applications_cb`, inner calls like `_type_width_aligned[T]()` get the unresolved TV as their type argument
2. `sizeof[T]()` is specialized with a TV instead of a concrete type
3. The transpiler (`tl_sizeof` in `transpile.c`) checks the type: if it's a TV, void, or any, it emits `(size_t)0` — **a silent wrong answer**
4. At runtime, this causes allocations of size 0, leading to OOM or corruption

This failure mode is silent because the compiler doesn't error — it produces valid C code that computes the wrong thing.

---

## Instance Cache and Naming

### Naming Scheme: `next_instantiation()`

```c
str next_instantiation(tl_infer *self, str name) {
    return str_fmt(self->arena, "%.*s_%u", str_ilen(name), str_buf(&name),
                   self->next_instantiation++);
}
```

Generates globally unique names: `identity_0`, `identity_1`, `Option_2`, `Some_3`, etc. The counter is global across all specializations and never reuses numbers (except via `cancel_last_instantiation()` on failure).

### Cache Key: `name_and_type`

```c
typedef struct {
    u64 name_hash;        // Hash of generic function/type name
    u64 type_hash;        // Hash of arrow or instantiation type
    u64 type_args_hash;   // Hash of explicit type arguments [T, U, ...]
} name_and_type;
```

### Instance Functions

- **`instance_lookup_arrow(name, arrow, type_args)`** — Requires concrete arrow type. Returns cached specialized name or NULL.
- **`instance_lookup(key)`** — Low-level lookup in `instances` map by `name_and_type` key.
- **`instance_add(key, inst_name)`** — Inserts into `instances` map and `instance_names` set.
- **`instance_name_exists(name)`** — O(1) check if a name is already a specialized instance.

### Pre-defaulting Weak Integers

Before specialization begins, `run_specialize()` defaults weak integer literals. This ensures stable cache keys — without it, the same logical call could produce different hash keys depending on whether a weak int was resolved yet.

A second defaulting pass runs after specialization to handle weak ints created during `post_specialize()` re-inference.

---

## Closures and Lambdas

### Let-in Lambda Specialization

For let-in lambdas like `f := (x) { x + n }`:

1. **First pass**: When call sites are specialized (e.g., `f(42)`), `specialize_applications_cb()` calls `specialize_arrow()` to create a specialized toplevel instance of the lambda and renames the NFA node at each call site.

2. **Second pass** (`specialize_let_in`): For **monomorphic** closures (called at a single concrete type), looks up the specialization via `instance_lookup_arrow()` and renames the binding to the specialized name so that later phases and the transpiler can find it in toplevels.

   For **polymorphic** closures (called at multiple types, e.g. `f(1.0)` and `f("hello")`), the binding cannot be reduced to a single specialization — it stays as the generic name.  The transpiler looks up all specializations via the `specializations` map (generic name → `str_array` of instance names, built during `specialize_arrow`) and generates a closure binding for each (same heap-allocated context, different function pointer).

   A special case is lambdas inside generic functions that get specialized: `specialize_let_in_lambda_from_body()` creates the specialization from the now-concrete body type after the enclosing function was monomorphized.

### Allocated Closures

For allocated closures with `[[alloc, capture(...)]]` attributes:

- Alpha conversion synthesizes `Alloc__context.default` for bare `[[alloc]]` attributes
- Free variable collection skips `alloc_expr` nodes (via `skip_alloc_expr` flag in `traverse_ctx`)
- Closure attributes are preserved during AST cloning
- Context struct generation uses different naming prefixes:
  - **Stack closures**: `tl_ctx_<hash>` (pointer fields)
  - **Allocated closures**: `tl_alloc_ctx_<hash>` (value fields, copied by value)
- **Polymorphic closures**: When a closure is called at multiple types, the transpiler (`generate_let_in_lambda`) looks up all specializations via the `specializations` map and emits one `tl_closure` variable per specialization, all sharing the same context but with different `.fn` pointers

---

## Edge Cases and Special Handling

| Case | Handling |
|------|----------|
| **Intrinsics** (`_tl_*` names) | Skipped entirely by `is_intrinsic()` check |
| **Already specialized** | `ast_node_is_specialized()` prevents re-specialization |
| **Type aliases** | Normalized to canonical `generic_name` before specialization |
| **Enums** | Skipped in `specialize_type_constructor_()` — they don't need specialization |
| **Non-generic unions** | Skipped in `specialize_user_type()` |
| **Generic unions** | Use inferred type (not AST arguments) for correct concrete types |
| **Function pointers** | Specialized via `specialize_operand()` or `specialize_arguments()` |
| **Type predicates** (`::`) | Handled by `check_type_predicate()` |
| **Trait bounds** | Verified via `verify_trait_bounds()` before specialization |
| **Mutual recursion** | Handled by provisional arrow types from Phase 3, updated after specialization |
| **Formal parameters** | Guarded from premature specialization in symbol handling |
| **Explicit type args** (`map[Int, String](...)`) | Extracted from NFA, parsed via `parse_type_arg()`, passed to `specialize_arrow()` |

---

## Key Data Structures

### `tl_infer` (in `infer_internal.h`)

The main inference context. Specialization-relevant fields:

| Field | Type | Purpose |
|-------|------|---------|
| `instances` | `hashmap*` | Maps `name_and_type` → specialized name (instance cache) |
| `instance_names` | `hashmap*` | Set of all generated instance names (for dedup) |
| `specializations` | `hashmap*` | Reverse index: generic name → `str_array` of specialization names |
| `next_instantiation` | `u32` | Counter for `_0`, `_1`, `_2`... naming |
| `toplevels` | `hashmap*` | `str` → `ast_node*` — all definitions including specialized ones |
| `env` | `tl_type_env*` | Type environment mapping names to polytypes |
| `subs` | `tl_type_subs*` | Substitution table for type variables |
| `registry` | `tl_type_registry*` | Type constructor definitions |

### `traverse_ctx` (in `infer_internal.h`)

Passed through AST traversal. Specialization-relevant fields:

| Field | Type | Purpose |
|-------|------|---------|
| `lexical_names` | `hashmap*` | Set of bound variable names in scope |
| `type_arguments` | `hashmap*` | Map `str` → `tl_monotype*` (type param bindings) |
| `free_variables` | `str_sized` | Captured variables (for closures) |
| `result_type` | `tl_monotype*` | Expected result type for current function |
| `skip_alloc_expr` | `int` | Skip alloc_expr during FV collection |

---

## Key Functions Reference

| Function | File | Purpose |
|----------|------|---------|
| `run_specialize` | `infer.c` | Phase 5 orchestration |
| `specialize_applications_cb` | `infer_specialize.c` | Main DFS traversal callback |
| `specialize_arrow` | `infer_specialize.c` | Creates specialized function instances |
| `specialize_arrow_with_name` | `infer_specialize.c` | Wrapper: specialize + update call site name |
| `post_specialize` | `infer_specialize.c` | Re-infer body + recursively specialize nested calls |
| `make_arrow_with` | `infer_specialize.c` | Constructs concrete arrow type from call site |
| `specialize_arguments` | `infer_specialize.c` | Specializes function pointer arguments |
| `specialize_let_in` | `infer_specialize.c` | Handles let-in lambda specialization |
| `specialize_reassignment` | `infer_specialize.c` | Handles function pointer reassignment |
| `specialize_case` | `infer_specialize.c` | Handles pattern matching specialization |
| `specialize_operand` | `infer_specialize.c` | Specializes symbol references to functions |
| `specialize_user_type` | `infer_specialize.c` | Entry point for type constructor specialization |
| `specialize_type_constructor` | `infer_specialize.c` | Wrapper with cycle detection setup |
| `specialize_type_constructor_` | `infer_specialize.c` | Recursive type specialization with seen set |
| `clone_generic_for_arrow` | `infer_alpha.c` | Clone AST + erase types + concretize params |
| `rename_variables` | `infer_alpha.c` | Alpha-convert + erase all types in cloned AST |
| `concretize_params` | `infer_alpha.c` | Assign concrete types to cloned parameters |
| `add_free_variables_to_arrow` | `infer_alpha.c` | Recalculate FVs after renaming |
| `next_instantiation` | `infer.c` | Generate `name_N` instance names |
| `instance_lookup_arrow` | `infer_specialize.c` | Cache lookup by (name, arrow, type_args) |
| `instance_add` | `infer_specialize.c` | Add to instance cache + names set |
| `instance_name_exists` | `infer_specialize.c` | Check if name is already an instance |
| `toplevel_tagged_union` | `parser.c` | Desugars tagged union syntax |
| `create_variant_constructor` | `parser.c` | Generates constructor functions for variants |

### Debug Aids

- **`DEBUG_INVARIANTS`** — Enables post-clone invariant checking (all types null after erase)
- **`DEBUG_INSTANCE_CACHE`** — Logs all instance cache operations (lookups, hits, misses, adds)

---

## See Also

- [ALPHA_CONVERSION.md](ALPHA_CONVERSION.md) - Variable renaming system that ensures type safety across specializations
- [NAME_MANGLING.md](NAME_MANGLING.md) - Name mangling for arity, modules, and instantiation naming
- [CLOSURES.md](plans/CLOSURES.md) - Closure implementation including allocated closures
