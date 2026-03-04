# Specialization System Simplification

## Problem Statement

The type argument resolution and specialization system has accumulated complexity
across three files (`infer_constraint.c`, `infer_specialize.c`, `infer_alpha.c`).
Type argument resolution is implemented three times with different fallback chains,
type argument information flows through two parallel channels that must agree, and
the shared `hot_parse_ctx` singleton requires manual reentrancy guarding.

This plan proposes incremental simplifications, each independently valuable and
testable.

---

## Background: Current Architecture

### The 3-site type argument resolution problem

Converting a type argument AST node to a monotype happens in three places. Each
has a different fallback chain AND different post-resolution requirements:

| Site | File | Phase | Fallback order | Post-resolution |
|------|------|-------|----------------|-----------------|
| `traverse_ctx_assign_type_arguments` | infer_constraint.c | Phase 3 + Phase 5 re-infer | ctx lookup → node->type → fresh parse | Accepts type variables (Phase 3 needs fresh TVs for unification) |
| `make_instance_key` | infer_specialize.c | Phase 5 specialization | fresh parse → node->type fallback | Requires concrete; nulls out non-concrete + sets cache-skip flag |
| `concretize_params` | infer_alpha.c | Phase 5 cloning | symbol ctx lookup → node->type → fresh parse | Only accepts concrete (substitutes + concrete-gates) |

Bugs arise when one path handles a case another doesn't (e.g., `type_literal_specialize`
renaming a node so the registry can't parse it — the node->type fallback wasn't in
all three sites until recently).

### Why the orderings differ

**Site 1 needs ctx-first + accepts variables.** In Phase 3, `ctx->type_arguments`
contains fresh type variables created by `traverse_ctx_load_type_arguments`. These
must be returned as-is for HM unification to work. Requiring concreteness would
break constraint generation. In Phase 5 re-inference, `ctx->type_arguments` is
populated from `concretize_params` (via `traverse_ctx_load_type_arguments` seeing
the concrete types set on type parameter nodes), so the ctx lookup returns concrete
types.

**Site 2 needs parse-first.** `node->type` may contain an unresolved type variable
from a prior inference pass. Fresh parse produces the structurally correct type
from the AST node's shape + outer context. Only when parse fails (because
`type_literal_specialize` renamed the node to a specialized name the registry
doesn't know) does it fall back to `node->type`. After resolution, non-concrete
results are nulled out with a cache-skip flag.

**Site 3 needs concrete-gating.** If the callsite type arg's type is an unresolved
variable, using it would seed the clone with a variable that re-inference can't
resolve. The substitution + concrete check ensures only fully resolved types are
accepted. If all three tries fail, the clone's type parameter gets no binding and
re-inference creates a fresh TV — which is the correct fallback.

### The 2-channel type argument threading problem

Explicit type arguments travel via two parallel channels:

1. **`ctx->type_arguments` hashmap** — maps alpha-converted param names (`tl_T_v4`)
   to resolved monotypes. Set by `traverse_ctx_assign_type_arguments` and
   `concretize_params`.

2. **`callsite_type_arguments` AST node array** — the raw type argument AST nodes
   from the NFA node, threaded explicitly through `specialize_arrow` →
   `clone_generic_for_arrow` → `concretize_params`.

Both are necessary because the hashmap uses alpha-converted names (for type
environment correctness) while the AST array preserves structure for
`make_instance_key` hashing. When one channel fails, it falls back to the other.

### The `hot_parse_ctx` singleton

A single shared `tl_type_registry_parse_type_ctx` is reinitialized before each use
rather than allocating fresh. A `hot_parse_ctx_guard` flag detects reentrancy
(assertion failure). Every call site must reinit → capture result → clear guard,
with no intervening calls that might re-enter. The singleton exists for performance
(profiling showed excessive allocation from per-call context creation).

### AST state at each phase

Understanding when `node->type` is null vs set vs stale is critical:

- **Phase 3 (generic constraint generation):** All types are null.
  `rename_variables` erased them. `node->type` is never set on type arg nodes.

- **Phase 5 re-inference (post_specialize):** The clone was just produced by
  `clone_generic_for_arrow`. `rename_variables` erased all types to null.
  `concretize_params` set types on parameter and type-parameter nodes, but NOT
  on type-argument nodes inside NFA call sites within the body. So type arg nodes
  in call sites within the body are still null.

- **Phase 5 specialization (specialize_applications_cb):** By this point,
  `type_literal_specialize` may have renamed type arg nodes and set their types.
  `traverse_ctx_assign_type_arguments` (from re-inference) may have set types
  via `ast_node_type_set`. These types are generally concrete but may be stale
  type variables if set during an earlier generic pass on the original AST.

### Mutually recursive functions

When function A calls B[T] and B calls A[U]:
1. `instance_add` registers key_A → `A_0` BEFORE `post_specialize` runs.
2. When recursive specialization reaches back to A, `instance_name_exists` at
   line 945 catches it (checked before cache lookup).
3. If type args are non-concrete at that point, the cache-skip flag prevents
   incorrect cache sharing. The `instance_name_exists` early-out handles the
   recursion break independently.

---

## Phase 1: Extract shared parse-with-fallback helper

**Goal:** Extract the "fresh parse with context, fall back to node->type" pattern
into a shared function. This is the common sub-operation across all three sites —
each site wraps it with its own pre-checks (ctx lookup) and post-processing
(concrete-gating, substitution).

A single `resolve_type_arg` with one canonical ordering is NOT possible because
the three sites have genuinely different requirements (see "Why the orderings
differ" above). Instead, extract the shared fallback that was recently buggy.

### Design

```c
// infer_internal.h

// Parse a type argument node, using type_arguments as context for resolving
// type variables (e.g., K → Int). Falls back to node->type when fresh parse
// fails (e.g., after type_literal_specialize renamed the node).
// Returns null if neither path produces a result.
tl_monotype *parse_type_arg(tl_infer *self, hashmap *type_arguments,
                            ast_node *node);
```

Implementation:

```c
tl_monotype *parse_type_arg(tl_infer *self, hashmap *type_arguments,
                            ast_node *node) {
    // Try fresh parse with context. This is the structurally correct
    // interpretation of the AST node.
    tl_monotype *parsed;
    if (type_arguments) {
        hot_parse_ctx_reinit(self, type_arguments);
        parsed = tl_type_registry_parse_type_with_ctx(
            self->registry, node, &self->hot_parse_ctx);
        self->hot_parse_ctx_guard = 0;
    } else {
        parsed = tl_type_registry_parse_type(self->registry, node);
    }
    if (parsed) return parsed;

    // Fallback: use type already set on node (e.g., after
    // type_literal_specialize renamed the node so the registry
    // can't parse it, but the type was propagated onto the node).
    if (node->type) return node->type->type;

    return null;
}
```

### Changes to each site

**Site 1 (`traverse_ctx_assign_type_arguments`):** Path 1 (ctx lookup) stays
as-is — it's site-specific. Paths 2+3 are replaced with `parse_type_arg()`.
The ordering changes from (ctx → node->type → parse) to (ctx → parse →
node->type), which is correct because: in Phase 3 node->type is always null
(no behavior change), and in Phase 5 re-inference the ctx lookup handles the
common case while `parse_type_arg` handles complex type args like `Point[K]`.

```c
// Before: three hand-coded paths
if (ast_node_is_symbol(type_arg_node)) {
    tl_monotype *from_ctx = str_map_get_ptr(ctx->type_arguments, ...);
    if (from_ctx) parsed = from_ctx;
}
if (!parsed && type_arg_node->type) {
    parsed = type_arg_node->type->type;
}
if (!parsed) {
    hot_parse_ctx_reinit(self, ctx->type_arguments);
    parsed = tl_type_registry_parse_type_with_ctx(...);
    self->hot_parse_ctx_guard = 0;
}

// After:
if (ast_node_is_symbol(type_arg_node)) {
    tl_monotype *from_ctx = str_map_get_ptr(ctx->type_arguments, ...);
    if (from_ctx) parsed = from_ctx;
}
if (!parsed) {
    parsed = parse_type_arg(self, ctx->type_arguments, type_arg_node);
}
```

**Site 2 (`make_instance_key`):** Already uses parse-first ordering. Replace the
two-step (parse → node->type fallback) with `parse_type_arg()`. Post-resolution
concrete-gating stays as-is.

```c
// Before:
type_arg_types.v[i] = tl_type_registry_parse_type_with_ctx(...);
if (!type_arg_types.v[i] && type_arg->type) {
    type_arg_types.v[i] = type_arg->type->type;
}

// After:
type_arg_types.v[i] = parse_type_arg(self, outer_type_arguments, type_arg);
```

**Site 3 (`concretize_params`):** Path 1 (symbol ctx lookup) stays as-is.
Paths 2+3 are replaced with `parse_type_arg()`. The concrete-gating (clone +
substitute + check) wraps the result as before.

```c
// Before: three hand-coded tries
if (ast_node_is_symbol(callsite_arg)) {
    bound_type = str_map_get_ptr(type_arguments, callsite_arg->symbol.name);
}
if (!bound_type && callsite_arg->type) {
    tl_monotype *resolved = tl_monotype_clone(..., callsite_arg->type->type);
    tl_monotype_substitute(..., resolved, ...);
    if (tl_monotype_is_concrete(resolved)) bound_type = resolved;
}
if (!bound_type) {
    hot_parse_ctx_reinit(self, type_arguments);
    tl_monotype *parsed = tl_type_registry_parse_type_with_ctx(...);
    self->hot_parse_ctx_guard = 0;
    if (parsed) {
        tl_monotype_substitute(..., parsed, ...);
        if (tl_monotype_is_concrete(parsed)) bound_type = parsed;
    }
}

// After:
if (ast_node_is_symbol(callsite_arg)) {
    bound_type = str_map_get_ptr(type_arguments, callsite_arg->symbol.name);
}
if (!bound_type) {
    tl_monotype *resolved = parse_type_arg(self, type_arguments, callsite_arg);
    if (resolved) {
        resolved = tl_monotype_clone(self->arena, resolved);
        tl_monotype_substitute(self->arena, resolved, self->subs, null);
        if (tl_monotype_is_concrete(resolved)) bound_type = resolved;
    }
}
```

### Testing

All existing tests must pass unchanged — this is a pure refactor. Run with both
release and asan builds. The `test_generic_nested_sizeof` test exercises the most
complex path (nested generic sizeof with forwarded type vars).

---

## Phase 2: Consolidate `hot_parse_ctx` usage

**Goal:** Reduce `hot_parse_ctx` call sites to a single location (`parse_type_arg`),
making the reentrancy guard easier to reason about.

**Non-goal:** Do NOT replace `hot_parse_ctx` with stack-local contexts. The shared
singleton exists for performance — profiling showed excessive allocation from
per-call context creation. The singleton with `map_reset` avoids this.

### Design

After Phase 1, `parse_type_arg` is the primary consumer of `hot_parse_ctx` for
type argument resolution. Any remaining direct callers of `hot_parse_ctx_reinit`
outside of `parse_type_arg` should be audited:

- If they're resolving type args, they should call `parse_type_arg` instead.
- If they're doing something genuinely different (e.g., parsing a type alias
  target), they keep direct access.

The reentrancy guard remains, but becomes easier to reason about because
`parse_type_arg` is the main entry point — you only need to verify that
`parse_type_arg` doesn't call anything that re-enters it.

### Changes

- Audit all `hot_parse_ctx_reinit` call sites after Phase 1.
- Move any type-arg-resolution callers to use `parse_type_arg`.
- Document the remaining direct callers (if any) and why they need direct access.

### Testing

All tests must pass unchanged — this is a refactor of call site organization.

---

## Phase 3: Resolved monotypes in `make_instance_key`

**Goal:** Have `make_instance_key` receive already-resolved monotypes instead of
re-parsing AST nodes.

### Design

Change the signature:

```c
// Before:
name_and_type make_instance_key(tl_infer *self, str generic_name,
    tl_monotype *arrow, ast_node_sized type_arguments,
    hashmap *outer_type_arguments);

// After:
name_and_type make_instance_key(tl_infer *self, str generic_name,
    tl_monotype *arrow, tl_monotype_sized resolved_type_args);
```

The caller (`specialize_arrow` / `specialize_applications_cb`) resolves type args
via `parse_type_arg` before calling `make_instance_key`, then passes the
resolved monotype array.

### Changes

- `make_instance_key`: remove all AST parsing logic. Simply hash the resolved
  monotypes. The `type_args_non_concrete` flag becomes a simple check:
  `has_non_concrete = !tl_monotype_is_concrete(resolved_type_args.v[i])`.
- `specialize_applications_cb`: build a `tl_monotype_sized` array from the
  resolved type args before calling `specialize_arrow`.
- `specialize_arrow`: accept `tl_monotype_sized resolved_type_args` instead of
  the raw AST node array for cache key purposes.
- `instance_lookup_arrow`: same signature change.

### Impact

- Removes the `outer_type_arguments` parameter from `make_instance_key` and
  `instance_lookup_arrow`.
- Removes `parse_type_arg` / `hot_parse_ctx` usage from cache key generation
  entirely.
- Makes the non-concrete detection trivial and correct by construction.

### Risk

Must verify that by the time `specialize_applications_cb` runs, the type arg
nodes have been resolved (by `traverse_ctx_assign_type_arguments` during
re-inference or by `type_literal_specialize`). For the recursive case inside
`post_specialize`, the re-inference pass populates types on all NFA type arg
nodes before the specialization pass runs.

### Testing

All tests must pass. The `test_generic_nested_sizeof` test exercises the case
that `type_args_non_concrete` was added for.

---

## Phase 4: Drop the raw AST array channel

**Goal:** Eliminate the `callsite_type_arguments` AST node array from
`specialize_arrow` and `clone_generic_for_arrow`, leaving only the resolved
monotype channel.

### Design

After Phase 3, `make_instance_key` no longer needs the raw AST array. The only
remaining consumer is `concretize_params`, which uses it as a fallback when the
hashmap lookup fails.

Change `concretize_params` to accept a `tl_monotype_sized resolved_type_args`
array for positional lookup:

```c
// Before: three-path fallback from AST nodes
// After: direct positional access
tl_monotype *arg_type = resolved_type_args.v[i];
```

### Changes

- `specialize_arrow`: remove `callsite_type_arguments` parameter. Pass
  `resolved_type_args` (from Phase 3) to `clone_generic_for_arrow` instead.
- `clone_generic_for_arrow`: accept `tl_monotype_sized` instead of
  `ast_node_sized callsite_type_arguments`.
- `concretize_params`: replace the fallback logic with direct positional
  lookup into the resolved array.
- `specialize_applications_cb`: stop passing `callsite_type_arguments` to
  `specialize_arrow`.

### Risk

Medium. Must verify that `concretize_params` never needs AST-level information
that isn't captured in the resolved monotype. The key case is when a type arg
is a parameterized type (e.g., `Point[Int]`) — the resolved monotype must carry
the full `cons_inst` structure, not just a bare type variable. This should
already be the case after `parse_type_arg` returns.

Also must handle the case where resolution produced null (all three tries
failed). `concretize_params` currently handles this by leaving the type
parameter unbound and letting re-inference create a fresh TV. The resolved
array must preserve this null-means-unresolved semantic.

### Testing

All tests must pass. Pay special attention to:
- `test_generic_nested_sizeof` — nested generic types as type args
- `test_generic_fn_ref_explicit_type_args` — function references
- `test_hashmap` — the full HashMap test (known failure, but should not regress)

---

## Not Included (Future Consideration)

### Explicit AST tag for type applications

Introducing `ast_type_application` vs `ast_named_function_application` to
disambiguate type literals from value constructors at parse time. This would
eliminate the assignment-args heuristic in `type_literal_specialize`.

Deferred because:
- The parser change is invasive (touches tokenizer, parser, all AST consumers).
- The current heuristic works and is well-guarded.
- The benefit is primarily aesthetic — no known bugs from the current approach.

Can be revisited if the heuristic causes problems in future language features.

### Replacing `hot_parse_ctx` with stack-local contexts

Deferred. The shared singleton with `map_reset` exists due to profiling data
showing excessive allocation from per-call context creation. The reentrancy
guard is a necessary cost of this optimization. Phase 1+2 reduce the number of
call sites to minimize reentrancy risk.

---

## Dependency Order

```
Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4
(extract)   (consolidate) (resolved)  (drop AST)
```

Each phase is independently committable and testable. Phase 1 is prerequisite
for the others because it centralizes the parse+fallback logic that the later
phases modify.
