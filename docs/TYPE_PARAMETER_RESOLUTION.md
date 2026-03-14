# Type Parameter Resolution During Specialization

This document provides a detailed analysis of how type parameters (e.g., `T` in `reserve[T]`) are resolved to concrete types during monomorphization. It covers the full chain from cloning through re-inference, the role of annotations, and failure modes.

For the high-level specialization pipeline, see [SPECIALIZATION.md](SPECIALIZATION.md).

## Table of Contents

1. [Problem Statement](#problem-statement)
2. [Source Code Organization](#source-code-organization)
3. [The Resolution Chain](#the-resolution-chain)
4. [Step 1: Cloning and Type Erasure](#step-1-cloning-and-type-erasure)
5. [Step 2: Concretize Parameters](#step-2-concretize-parameters)
6. [Step 3: Load Type Arguments](#step-3-load-type-arguments)
7. [Step 4: Re-inference and Annotation Unification](#step-4-re-inference-and-annotation-unification)
8. [Step 5: Type Argument Propagation to Inner Calls](#step-5-type-argument-propagation-to-inner-calls)
9. [Step 6: Recursive Specialization](#step-6-recursive-specialization)
10. [Forward Declaration Annotation Merging](#forward-declaration-annotation-merging)
11. [Failure Modes](#failure-modes)
12. [Worked Example](#worked-example)

---

## Problem Statement

Consider a generic function that calls an inner generic function using an explicit type argument:

```tl
// Forward declaration
reserve[T](self: Ptr[Array[T]], alloc: Ptr[Allocator], count: CSize) -> Void

// Implementation
reserve[T](self, alloc, count: CSize) {
    if self->capacity < count {
        p := alloc->realloc(alloc, self->v, count * _type_width_aligned[T]())
        //                                                              ^^^
        // T must resolve to a concrete type for sizeof[T]() to work
    }
}
```

When `reserve` is specialized for `Array[Int]`, the type parameter `T` must resolve to `Int` so that `_type_width_aligned[T]()` (which calls `sizeof[T]()`) produces `sizeof(long long)` rather than `(size_t)0`.

The resolution of `T` involves a chain of steps across multiple source files. This document traces that chain in detail.

---

## Source Code Organization

Type parameter resolution spans several files. Here is where each piece lives:

### `infer_specialize.c` — Specialization orchestration

| Section / Function | Line range | Role |
|---|---|---|
| `specialize_arrow()` | Arrow specialization section | Entry point: clones generic, calls post_specialize |
| `clone_generic_for_arrow()` | Generic cloning section | Clone AST → rename → concretize_params |
| `post_specialize()` | Post-specialization section | Re-inference → subs → recursive specialization |
| `specialize_applications_cb()` | Per-node specialization section | Handles NFAs: builds resolved_type_args, calls specialize_arrow |

### `infer_alpha.c` — Cloning and concretization

| Section / Function | Line range | Role |
|---|---|---|
| `rename_variables()` | Variable renaming section | Alpha-converts clone, erases all types |
| `concretize_params()` | Concretize parameters section | Sets concrete types on value params and type params |

### `infer_constraint.c` — Inference and type argument management

| Section / Function | Line range | Role |
|---|---|---|
| `traverse_ctx_load_type_arguments()` | Traverse context section | Loads type param bindings into ctx->type_arguments |
| `traverse_ctx_assign_type_arguments()` | Traverse context section | Resolves explicit type args at NFA call sites |
| `traverse_ast()`, `ast_let` case | AST traversal section | Calls load_type_arguments, traverses params and body |
| `process_annotation()` | Inference handlers section | Parses parameter annotations, constrains types |
| `parse_type_annotation()` | Inference handlers section | Parses AST annotation using type_arguments context |
| `infer_named_function_application()` | Inference handlers section | Handles NFA type inference with explicit type args |
| `load_toplevel_let()` | Toplevel loading section | Copies forward declaration annotations to implementation |
| `remap_annotation_type_params()` | Before load_toplevel_let | Remaps type param names in copied annotations |

### `type.c` — Type system primitives

| Section / Function | Role |
|---|---|
| `tl_polytype_instantiate()` / `_with()` | Creates fresh TVs for quantifiers during instantiation |
| `tl_type_subs_unify_mono()` | Unification: binds type variables to concrete types |
| `tl_monotype_substitute()` | Applies substitution bindings to monotypes |

### `transpile.c` — Code generation

| Section / Function | Role |
|---|---|
| `tl_sizeof()` | Generates `sizeof(Type)` or `(size_t)0` for unresolved types |
| `resolve_nullary_type_argument()` | Extracts type from nullary call with type argument |

---

## The Resolution Chain

Here is the complete chain for resolving a type parameter during specialization:

```
specialize_arrow("reserve", concrete_arrow)
  │
  ├─ clone_generic_for_arrow()
  │    ├─ ast_node_clone()           — deep copy of generic AST
  │    ├─ rename_variables()         — alpha-convert + ERASE ALL TYPES
  │    │    └─ Also traverses into symbol.annotation (the AST annotation
  │    │       node), renaming type parameter references within it.
  │    │       Clears symbol.annotation_type (the resolved monotype).
  │    ├─ add_free_variables_to_arrow()
  │    └─ concretize_params()        — sets concrete types on VALUE params
  │         ├─ Value params: type set from callsite arrow (self → Ptr[Array[Int]])
  │         └─ Type params: tries type_arguments map + resolved_type_args
  │              Usually FAILS for both (different alpha names, no explicit type args)
  │              → Type parameter T is NOT BOUND at this point
  │
  └─ post_specialize()
       │
       ├─ traverse_ast(infer_traverse_cb)     ← RE-INFERENCE
       │    │
       │    ├─ ast_let case:
       │    │    ├─ traverse_ctx_load_type_arguments()
       │    │    │    For each type parameter:
       │    │    │    - Has type from concretize_params? → use it (rare)
       │    │    │    - No type? → create FRESH type variable tv_fresh
       │    │    │    Adds to ctx->type_arguments: { "tl_T_v278_v662" → tv_fresh }
       │    │    │
       │    │    ├─ traverse params (npos_formal_parameter):
       │    │    │    For each parameter, calls infer_traverse_cb → resolve_node
       │    │    │    → process_annotation()
       │    │    │      Parses self's annotation Ptr[Array[T]]
       │    │    │      "T" (alpha-converted) looked up in ctx->type_arguments → tv_fresh
       │    │    │      Annotation type: Ptr[Array[tv_fresh]]
       │    │    │      Unified with self's concrete type: Ptr[Array[Int]]
       │    │    │      ══════════════════════════════════════════════════
       │    │    │      THIS IS WHERE T GETS RESOLVED: tv_fresh = Int
       │    │    │      ══════════════════════════════════════════════════
       │    │    │
       │    │    └─ traverse body (npos_operand):
       │    │         When _type_width_aligned[T]() is encountered:
       │    │         ├─ traverse_ctx_assign_type_arguments()
       │    │         │    Looks up "T" in ctx->type_arguments → tv_fresh
       │    │         │    Adds _type_width_aligned's type param → tv_fresh
       │    │         │
       │    │         └─ infer_named_function_application()
       │    │              Instantiates _type_width_aligned's polytype
       │    │              using tv_fresh (which is bound to Int via subs)
       │    │
       │    └─ (other body nodes traversed normally)
       │
       ├─ tl_type_subs_default_weak_ints()
       ├─ apply_subs_to_ast_node()            ← APPLY SUBSTITUTIONS
       │    tv_fresh → Int applied throughout the AST
       │    Type argument nodes now carry concrete types
       │
       ├─ rewrite_operator_overloads()
       │
       └─ traverse_ast(specialize_applications_cb)  ← RECURSIVE SPECIALIZATION
            │
            └─ For _type_width_aligned[T]():
                 ├─ parse_type_arg() reads T's type from the AST node
                 │    After subs application, T's type is Int (concrete)
                 ├─ resolved_type_args = [Int]
                 └─ specialize_arrow("_type_width_aligned", arrow, [Int])
                      └─ Eventually: sizeof[Int]() → sizeof(long long) ✓
```

---

## Step 1: Cloning and Type Erasure

**File:** `infer_alpha.c`, `rename_variables()`

When `specialize_arrow()` is called, `clone_generic_for_arrow()` creates a fresh copy of the generic function. The critical step is `rename_variables()`, which:

1. **Alpha-converts** all bound variable names to fresh unique names (e.g., `self` → `tl_self_v279_v662`). This prevents name collisions between different specializations.

2. **Erases all types** by setting `node->type = null` on every AST node. This is essential — without it, type information from the generic phase would "pollute" the specialized phase.

3. **Clears `annotation_type`** (`node->symbol.annotation_type = null`) but **preserves `annotation`** (the AST node). The AST annotation is traversed and its symbols are alpha-converted.

4. **Traverses into annotations** (line ~243): `rename_variables(self, node->symbol.annotation, ctx, level + 1)`. This ensures type parameter references inside annotations (like the `T` in `Ptr[Array[T]]`) are renamed consistently with the clone's type parameter names.

After this step, the clone has:
- Fresh variable names
- No type information
- Preserved AST annotations with alpha-converted type parameter names

---

## Step 2: Concretize Parameters

**File:** `infer_alpha.c`, `concretize_params()`

Sets concrete types on the clone's parameters from the callsite arrow:

**Value parameters** (always succeeds):
```c
forall(i, params) {
    ast_node_type_set(param, callsite_type);  // e.g., self → Ptr[Array[Int]]
    env_insert_constrain(self, name, callsite_type, param);
    tl_type_env_insert(self->env, name, callsite_type);
}
```

**Type parameters** (usually fails for calls without explicit type args):
```c
// Try 1: Direct lookup in type_arguments (outer context)
tl_monotype *bound_type = str_map_get_ptr(type_arguments, param_name);
// Usually fails: clone's alpha-converted name differs from caller's

// Try 2: Positional fallback via resolved_type_args
if (!bound_type && i < resolved_type_args.size && resolved_type_args.v[i]) { ... }
// Fails when call site has no explicit type args (resolved_type_args is empty)
```

When both fail, the type parameter node has `type = null`. This is **normal** — the type parameter will be resolved during re-inference (Step 4).

---

## Step 3: Load Type Arguments

**File:** `infer_constraint.c`, `traverse_ctx_load_type_arguments()`

Called at the start of the `ast_let` case in `traverse_ast()`. For each declared type parameter of the function:

```c
if (type_param->type) {
    // Type was set by concretize_params → use it
    tl_type_registry_add_type_argument(..., mono, &ctx->type_arguments);
} else {
    // No type → create FRESH type variable
    tl_monotype *mono = tl_type_registry_add_fresh_type_argument(...);
    ast_node_type_set(type_param, ...);
}
```

After this step, `ctx->type_arguments` maps the type parameter's alpha-converted name to either:
- A concrete type (if `concretize_params` succeeded), or
- A fresh type variable (the common case)

---

## Step 4: Re-inference and Annotation Unification

**File:** `infer_constraint.c`, `process_annotation()` and `parse_type_annotation()`

During re-inference, each parameter is traversed with `npos_formal_parameter`. For parameters with annotations (either explicit or copied from forward declarations):

1. **`parse_type_annotation()`** parses the AST annotation node (e.g., `Ptr[Array[T]]`) using `ctx->type_arguments` as context. The type parameter `T` (alpha-converted) is resolved to the fresh type variable from Step 3.

2. **`process_annotation()`** constrains the parameter's type: the parsed annotation type is unified with the parameter's concrete type (set by `concretize_params`).

This unification is where the type parameter gets its concrete binding:
```
Ptr[Array[tv_fresh]] ~ Ptr[Array[Int]]
→ tv_fresh = Int (recorded in substitution table)
```

**This is the critical step.** Without a parseable annotation containing the type parameter, `tv_fresh` remains unconstrained.

---

## Step 5: Type Argument Propagation to Inner Calls

**File:** `infer_constraint.c`, `traverse_ctx_assign_type_arguments()`

When the body traversal encounters an NFA with explicit type arguments (like `_type_width_aligned[T]()`), `traverse_ctx_assign_type_arguments()` resolves the type arguments:

```c
if (ast_node_is_symbol(type_arg_node)) {
    tl_monotype *from_ctx = str_map_get_ptr(ctx->type_arguments, type_arg_node->symbol.name);
    if (from_ctx) parsed = from_ctx;  // Found T → tv_fresh (bound to Int via subs)
}
```

The resolved type variable is then used in `infer_named_function_application()` to instantiate the inner function's polytype with the concrete type.

---

## Step 6: Recursive Specialization

**File:** `infer_specialize.c`, `specialize_applications_cb()`

After re-inference and substitution application (`apply_subs_to_ast_node`), the type argument AST nodes carry concrete types. When `specialize_applications_cb()` processes `_type_width_aligned[T]()`:

```c
forall(i, callsite_type_args) {
    resolved_type_args.v[i] = parse_type_arg(self, outer_type_args, callsite_type_args.v[i]);
}
```

`parse_type_arg()` reads the type from the AST node (now concrete after subs application), producing `resolved_type_args = [Int]`. This is passed to `specialize_arrow()` for the inner function, which flows through `concretize_params` → `traverse_ctx_load_type_arguments` → sizeof specialization.

---

## Forward Declaration Annotation Merging

**File:** `infer_constraint.c`, `load_toplevel_let()` and `remap_annotation_type_params()`

When a function has a forward declaration with parameter annotations but the implementation omits them:

```tl
// Forward declaration (parsed first)
reserve[T](self: Ptr[Array[T]], alloc: Ptr[Allocator], count: CSize) -> Void

// Implementation (parsed second, merged with forward declaration)
reserve[T](self, alloc, count: CSize) { ... }
```

`load_toplevel_let()` copies annotations from the forward declaration onto unannotated parameters:

```c
arg->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, param_tuple->list.xs.v[j]);
arg->symbol.annotation = ast_param_tuple->tuple.elements[j]->symbol.annotation;
```

### The Namespace Mismatch Problem

The forward declaration and implementation are alpha-converted independently during Phase 1. Their type parameter `T` gets different alpha-converted names:
- Forward declaration: `T` → `tl_T_v204`
- Implementation: `T` → `tl_T_v278`

The copied annotation AST contains `tl_T_v204`, but during re-inference, `ctx->type_arguments` uses the implementation's `tl_T_v278`. The lookup fails and the type parameter is never resolved.

### The Fix: `remap_annotation_type_params()`

When copying annotations, the AST is cloned and type parameter references are remapped:

```c
if (fwd_ann && node->let.n_type_parameters > 0 && ast_arrow->arrow.n_type_parameters > 0) {
    ast_node *cloned_ann = ast_node_clone(self->arena, fwd_ann);
    remap_annotation_type_params(cloned_ann, ast_arrow->arrow.type_parameters,
                                 node->let.type_parameters, node->let.n_type_parameters);
    arg->symbol.annotation = cloned_ann;
}
```

`remap_annotation_type_params()` recursively walks the annotation AST. For each symbol, it matches the original (pre-alpha) name against the forward declaration's type parameters, then replaces the alpha-converted name with the implementation's corresponding type parameter name.

---

## Failure Modes

### 1. Missing Annotation (No Forward Declaration)

If a generic function has no forward declaration and omits parameter annotations, the type parameter may only appear as an explicit type argument in inner calls:

```tl
foo[T](self) {
    bar[T]()  // T only appears here, not in any parameter annotation
}
```

During re-inference, `T` gets a fresh type variable, but nothing constrains it. Inner calls like `bar[T]()` propagate the unconstrained variable. If `bar` eventually calls `sizeof[T]()`, the transpiler produces `(size_t)0`.

### 2. Annotation Namespace Mismatch (Fixed)

Prior to the `remap_annotation_type_params()` fix, forward declaration annotations used a different alpha-converted namespace for type parameters. `parse_type_annotation()` failed to resolve the type parameter, leaving it as a fresh type variable.

### 3. Silent sizeof Zero

When `sizeof[T]()` receives an unresolved type variable, the transpiler (`tl_sizeof` in `transpile.c`) silently emits `(size_t)0`:

```c
if (tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type))
    return S("(size_t)0");
```

This produces valid C code that computes the wrong result at runtime (allocations of size 0, leading to OOM or memory corruption). A compile-time error would be more appropriate when a type variable reaches this point during transpilation.

---

## Worked Example

Tracing `reserve[T]` specialized for `Array[Int]`, called from `_grow(self, alloc, count)`:

**Phase 5 entry:** `specialize_applications_cb` encounters `reserve(self, alloc, count)` in `_grow`'s specialized body.
- `self` has type `Ptr[Array[Int]]`, `alloc` has type `Ptr[Allocator]`, `count` has type `CSize`
- No explicit type arguments at call site → `resolved_type_args = {}`
- Callsite arrow: `(Ptr[Array[Int]], Ptr[Allocator], CSize) -> Void`

**`specialize_arrow("reserve__3", arrow, {})`:**

1. **Clone:** `ast_node_clone` copies the generic `reserve` AST
2. **Rename:** `rename_variables` erases types, gives fresh names:
   - Type param `T` → `tl_T_v278_v662`
   - `self` annotation `Ptr[Array[T]]` → annotation preserved, `T` inside renamed to `tl_T_v278_v662`
3. **Concretize:** Value params get concrete types. Type param `tl_T_v278_v662` is NOT bound (no explicit type args).

**`post_specialize`:**

4. **Load type args:** `traverse_ctx_load_type_arguments` sees `tl_T_v278_v662` has no type → creates fresh `tv_5500`. `ctx->type_arguments = { "tl_T_v278_v662" → tv_5500 }`

5. **Re-infer params:**
   - `self`'s annotation `Ptr[Array[tl_T_v278_v662]]` is parsed → `Ptr[Array[tv_5500]]`
   - Unified with `self`'s concrete type `Ptr[Array[Int]]`
   - **Result:** `tv_5500 = Int` in subs

6. **Re-infer body:**
   - `_type_width_aligned[tl_T_v278_v662]()` encountered
   - `traverse_ctx_assign_type_arguments` looks up `tl_T_v278_v662` → `tv_5500`
   - `infer_named_function_application` instantiates `_type_width_aligned` with `tv_5500`

7. **Apply subs:** `tv_5500 → Int` applied to all AST nodes. Type argument for `_type_width_aligned` is now `Int`.

8. **Recursive specialize:**
   - `specialize_applications_cb` processes `_type_width_aligned[Int]()`
   - `parse_type_arg` reads `Int` from AST node → `resolved_type_args = [Int]`
   - `specialize_arrow("_type_width_aligned", arrow, [Int])` succeeds
   - Inside: `sizeof[Int]()` → `sizeof(long long)` ✓
