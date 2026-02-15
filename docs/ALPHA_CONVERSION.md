# Alpha Conversion in Tess

This document describes the alpha conversion (variable renaming) system in the Tess compiler. Alpha conversion ensures all bound variables have globally unique names, which is critical for preventing type pollution across generic and specialized type inference phases.

## Table of Contents

1. [Overview](#overview)
2. [The Renaming Process](#the-renaming-process)
3. [Fresh Name Generation](#fresh-name-generation)
4. [Scope Handling](#scope-handling)
5. [Integration with Type Inference](#integration-with-type-inference)
6. [Critical Invariants](#critical-invariants)
7. [Key Functions Reference](#key-functions-reference)
8. [Debugging](#debugging)
9. [Potential Bug Areas](#potential-bug-areas)
10. [Compilation Pipeline Overview](#compilation-pipeline-overview)
11. [Arena Management](#arena-management)
12. [Type Attachment Timeline](#type-attachment-timeline)
13. [Violation Detection Checklist](#violation-detection-checklist)
14. [Key Source Locations](#key-source-locations)

---

## Overview

Alpha conversion runs as **Phase 1** of type inference in `tl_infer_run()`, before any type analysis begins. It transforms all bound variable names into globally unique identifiers.

**Example:**

```tess
identity[T](x: T) -> T { x }
map[U](f: (a) -> b, xs: Array[a]) -> Array[b] { ... }
```

After alpha conversion:

```
identity's T  → tl_T_v0
identity's x  → tl_x_v1
map's U       → tl_U_v2
map's f       → tl_f_v3
map's a       → tl_a_v4
map's b       → tl_b_v5
map's xs      → tl_xs_v6
```

### Why Alpha Conversion is Necessary

Without unique names, type inference would conflate variables with the same name:

```tess
identity[T](x: T) -> T { x }
swap[T](a: T, b: T) -> (T, T) { (b, a) }
```

Both functions have a type parameter `T`, but they represent different type variables. Alpha conversion ensures `identity`'s `T` (→ `tl_T_v0`) is distinct from `swap`'s `T` (→ `tl_T_v7`).

This is especially critical during specialization, where the same generic function is cloned and re-analyzed with concrete types. Each clone gets fresh names, preventing type pollution between specializations.

---

## The Renaming Process

### Entry Point

Alpha conversion occurs in `tl_infer_run()` (infer.c:4950) as a two-stage process:

```c
// Phase 1: Alpha conversion
rename_let_in(self, node, &rename_ctx);      // Toplevel let-in only
rename_variables(self, node, &rename_ctx, 0); // All remaining nodes
```

### Stage 1: Toplevel Let-In (`rename_let_in`)

Processes only toplevel `let-in` bindings, keeping their renamed symbols in the global lexical scope throughout the program. This ensures toplevel definitions are visible everywhere.

### Stage 2: Full Traversal (`rename_variables`)

Recursively traverses the entire AST, renaming all bound variables:

1. **Erases all existing types** (line 3859) - critical for specialization
2. **Clears type argument types** - prevents generic→specialized pollution
3. **Renames bound variables** using lexical scope tracking

### Key Function: `rename_one_function_param()`

This function (infer.c:3763) handles the actual renaming of function parameters and type parameters:

```c
static void rename_one_function_param(tl_infer *self, ast_node *param,
                                      rename_variables_ctx *ctx, int level) {
    str *found;

    // 1. Check if name already exists in lexical scope - reuse it
    if ((found = str_map_get(ctx->lex, param->symbol.name))) {
        ast_node_name_replace(param, *found);
    }
    // 2. Check for mangled name with original in scope
    else if (param->symbol.is_mangled &&
             (found = str_map_get(ctx->lex, param->symbol.original))) {
        ast_node_name_replace(param, *found);
    }
    // 3. First occurrence - create fresh name
    else {
        str newvar = next_variable_name(self, param->symbol.name);
        ast_node_name_replace(param, newvar);
        str_map_set(&ctx->lex, param->symbol.name, &newvar);
    }
}
```

**Critical detail:** The function checks the lexical scope *before* creating fresh names. This ensures that type parameters from outer scopes (e.g., a function's `T` used inside a nested lambda) are not re-renamed.

---

## Fresh Name Generation

### `next_variable_name()` (infer.c:4199)

```c
static str next_variable_name(tl_infer *self, str name) {
    char buf[64];
    if (0 == str_cmp_nc(name, "tl_", 3))
        snprintf(buf, sizeof buf, "%s_v%u", str_cstr(&name), self->next_var_name++);
    else
        snprintf(buf, sizeof buf, "tl_%s_v%u", str_cstr(&name), self->next_var_name++);
    return str_init(self->arena, buf);
}
```

**Naming pattern:**
- Format: `tl_<original_name>_v<counter>`
- Examples: `tl_T_v0`, `tl_x_v5`, `tl_result_v12`
- Counter (`self->next_var_name`) increments globally, ensuring uniqueness
- Names allocated in permanent arena, surviving all phases

### Distinction from Specialization Naming

| Purpose | Function | Format | Example |
|---------|----------|--------|---------|
| Alpha conversion | `next_variable_name` | `tl_<name>_v<N>` | `tl_T_v4` |
| Generic instantiation | `next_instantiation` | `<name>_<N>` | `identity_0` |

---

## Scope Handling

### Data Structure: `rename_variables_ctx`

```c
typedef struct {
    hashmap *lex;       // Maps original name (str) -> renamed name (str)
    int      is_field;  // Skip renaming for struct field names
} rename_variables_ctx;
```

### Save/Restore Pattern

Lexical scope boundaries are preserved using a save/restore pattern:

**For Lambda Functions:**
```c
case ast_lambda_function: {
    hashmap *save = rename_function_params(self, node, ctx, level);
    rename_variables(self, node->lambda_function.body, ctx, level + 1);
    map_destroy(&ctx->lex);
    ctx->lex = save;  // Restore outer scope
} break;
```

**For Let Bindings:**
```c
case ast_let: {
    hashmap *save = rename_function_params(self, node, ctx, level);
    rename_variables(self, node->let.body, ctx, level + 1);
    map_destroy(&ctx->lex);
    ctx->lex = save;  // Restore outer scope
} break;
```

### Nested Scope Example

```tess
let f[T](x: T) -> T {
    let g[U](y: U) -> U {
        (a: T) -> T { a }  // lambda capturing f's T
    }
    g(x)
}
```

Renaming sequence:
1. `f`'s `T` → `tl_T_v0`, added to `ctx.lex`
2. `f`'s `x` → `tl_x_v1`, added to `ctx.lex`
3. Enter `g`, save `ctx.lex`
4. `g`'s `U` → `tl_U_v2`, added to `ctx.lex`
5. `g`'s `y` → `tl_y_v3`, added to `ctx.lex`
6. Lambda's `a: T` - `T` found in scope as `tl_T_v0`, reused
7. Lambda's `a` → `tl_a_v4`
8. Exit `g`, restore `ctx.lex` (U, y disappear; T, x remain)
9. Exit `f`, restore `ctx.lex`

---

## Integration with Type Inference

### Type Environment Uses Renamed Names

**Critical principle:** After alpha conversion, all type environment operations must use alpha-converted names, never original names.

```c
// CORRECT: Use alpha-converted name
str param_name = let->let.type_parameters[i]->symbol.name;  // e.g., "tl_T_v0"
tl_monotype *bound_type = str_map_get_ptr(type_arguments, param_name);

// WRONG: Would fail - original name not in environment
str param_name = let->let.type_parameters[i]->symbol.original;  // e.g., "T"
```

### Type Erasure

During alpha conversion, all AST types are erased (infer.c:3858-3876):

```c
// Ensure all types are removed - important for post-clone rename
ast_node_type_set(node, null);

// Also clear types attached to explicit type arguments
if (ast_node_is_let(node)) {
    for (u32 i = 0; i < node->let.n_type_parameters; i++)
        ast_node_type_set(node->let.type_parameters[i], null);
}
// Similar for NFAs and UTDs...
```

**Why this matters:**
- Types inferred during the generic phase must not leak into specializations
- Cloned generic functions start with null types
- Fresh type variables are created during each specialization phase

### Integration with Specialization

When `specialize_arrow()` clones a generic function:

1. The clone inherits alpha-converted names from the generic
2. `rename_variables()` runs again on the clone (inside `post_specialize`)
3. New fresh names are generated (e.g., `tl_T_v0` → `tl_T_v15`)
4. Type environment entries use the new names
5. No pollution between generic and specialized inference

---

## Critical Invariants

1. **Alpha conversion happens first** - Phase 1, before any type inference
2. **Fresh names are globally unique** - `_v<counter>` suffix ensures no collisions
3. **Lexical scope is preserved** - Nested scopes properly save/restore name mappings
4. **Type environment uses renamed names** - Never original names
5. **Types are erased** - All AST types set to null during alpha conversion
6. **Scope boundaries respected** - Outer scope names not re-renamed in inner scopes
7. **Mangling is handled** - Toplevel name conflicts respect lexical scope override

---

## Key Functions Reference

| Function | Location | Purpose |
|----------|----------|---------|
| `rename_variables` | infer.c:3852 | Core recursive traversal for alpha conversion |
| `rename_one_function_param` | infer.c:3763 | Rename individual parameter with scope checking |
| `rename_function_params` | infer.c:3813 | Process all function/lambda parameters |
| `rename_let_in` | infer.c:3680 | Handle toplevel let-in bindings |
| `collect_annotation_type_vars` | infer.c:3703 | Extract type vars from forward declarations |
| `next_variable_name` | infer.c:4199 | Generate fresh unique name |

### Call Graph

```
tl_infer_run()
├── rename_let_in() [all nodes]
└── rename_variables() [all nodes, level=0]
    ├── case ast_symbol: lookup in ctx->lex
    ├── case ast_let:
    │   └── rename_function_params()
    │       ├── rename_one_function_param() [type parameters]
    │       ├── collect_annotation_type_vars()
    │       └── rename_one_function_param() [value parameters]
    ├── case ast_lambda_function:
    │   └── rename_function_params()
    │       └── recurse on body
    ├── case ast_let_in:
    │   └── recurse with scope management
    └── [other cases: recurse on children]
```

---

## Debugging

### Debug Flags

Enable at compile time by defining these macros:

| Macro | Purpose |
|-------|---------|
| `DEBUG_RENAME` | Log individual rename operations |
| `DEBUG_EXPLICIT_TYPE_ARGS` | Log type argument assignment |
| `DEBUG_RESOLVE` | Log annotation processing and type resolution |

### Common Issues

**"Failed to parse type variable"** - Usually indicates:
- Original name used instead of alpha-converted name in type lookup
- Type parameter not collected from forward declaration annotation

**Type pollution across specializations** - Check that:
- Types are erased during alpha conversion
- Each specialization clone gets fresh names via re-running `rename_variables`

**Scope leak** - Verify that:
- `save = map_copy(ctx->lex)` happens before entering scope
- `ctx->lex = save` happens after processing body

---

## Potential Bug Areas

### 1. Type Variable Detection Heuristic

In `clone_generic_for_arrow()` (infer.c:1809-1837), type variables are detected by convention:

```c
// Type variables are typically lowercase letters
char first = str_len(original) > 0 ? str_buf(&original)[0] : 0;
if (first >= 'a' && first <= 'z' && str_len(original) <= 3) {
    // Add as type parameter
}
```

**Risk:** Non-standard type variable names may not be recognized.

### 2. Forward Declaration Type Variables

The `collect_annotation_type_vars()` function extracts type variables from forward declaration annotations. Complex nested structures may be missed.

### 3. Name Mangling Interaction

When a name is mangled to avoid toplevel conflicts, the lexical scope check must use the original name:

```c
if (param->symbol.is_mangled &&
    (found = str_map_get(ctx->lex, param->symbol.original))) {
    // Use lexical scope override, not mangled name
}
```

Failure to check this causes type environment mismatches.

---

## Compilation Pipeline Overview

Alpha conversion is Phase 1 of a 7-phase type inference pipeline. Understanding the full pipeline is essential for understanding why alpha conversion's invariants matter.

### Pipeline Phases

```
Parsing Phase
    ↓ (AST with null types)
Phase 1: Alpha Conversion
    ↓ (Alpha-converted names, all types erased)
Phase 2: Load Top-level Definitions
    ↓ (Top-level map populated)
Phase 3: Generic Type Inference
    ↓ (Generic quantified types in environment)
Phase 4: Check Free Variables & Apply Substitutions
    ↓ (Substitutions applied)
Phase 5: Specialization
    ↓ (Concrete monomorphic functions)
Phase 6: Tree Shaking
    ↓ (Unreachable code removed)
Phase 7: Type Specialization Updates
    ↓ (Type registry finalized)
Transpilation Phase
    ↓ (Generate C code)
```

### Phase-by-Phase Invariants

#### Phase 1: Alpha Conversion (infer.c:4953-4965)

| Pre-condition | Post-condition |
|---------------|----------------|
| Fresh AST from parser | All bound variables renamed (`tl_T_v0`) |
| No types attached | ALL AST types erased (set to null) |
| Original variable names | Lexical scope preserved |

**Critical operations:**
```c
rename_let_in(self, node, &ctx);           // Toplevel let-in only
rename_variables(self, node, &ctx, 0);     // All nodes
```

#### Phase 2: Load Top-level (infer.c:4967-4976)

| Pre-condition | Post-condition |
|---------------|----------------|
| Alpha conversion complete | `self->toplevels` map populated |
| All names globally unique | Type aliases registered |
| All types null | Forward declarations processed |

**Critical:** Names in toplevels map are alpha-converted (e.g., `tl_identity_v0`).

#### Phase 3: Generic Inference (infer.c:4977-4998)

| Pre-condition | Post-condition |
|---------------|----------------|
| Top-level map ready | Each function has quantified polytype |
| Types erased | Type schemes: `∀a. a→a` |
| Type environment empty | Environment populated with schemes |

**Key function:** `add_generic()` - infers type for one function, then generalizes (quantifies free type variables).

#### Phase 4: Free Variables & Substitutions (infer.c:4989-5009)

| Pre-condition | Post-condition |
|---------------|----------------|
| Generic types inferred | All free variables verified |
| Substitutions collected | Substitutions applied to env |
| | Substitutions applied to AST |

#### Phase 5: Specialization (infer.c:5021-5089)

| Pre-condition | Post-condition |
|---------------|----------------|
| Generic types quantified | All reachable generics specialized |
| Main function identified | Each specialization: unique name + concrete type |
| | Nested calls recursively specialized |

**Specialization flow for each call:**
```c
// 1. Create concrete callsite arrow from argument types
callsite = make_arrow_with(self, traverse_ctx, node, type);

// 2. Check cache
if (instance_lookup_arrow(self, name, arrow)) return cached;

// 3. Create unique name
inst_name = next_instantiation(self, name);  // "identity_0"

// 4. Clone generic AST
clone = clone_generic_for_arrow(self, generic, arrow, inst_name);

// 5. Add to toplevel
toplevel_add(self, inst_name, clone);

// 6. Process specialized body (CRITICAL)
post_specialize(self, traverse_ctx, clone, arrow);
```

**Inside `clone_generic_for_arrow()`:**
- Clone AST node
- Types already null (from Phase 1)
- Run `rename_variables()` AGAIN → fresh names for this specialization
- Concretize parameters from callsite arrow

**Inside `post_specialize()`:**
- Re-run type inference on specialized body
- Apply substitutions
- Recursively specialize nested calls

#### Phase 6: Tree Shaking (infer.c:5107-5116)

| Pre-condition | Post-condition |
|---------------|----------------|
| All specializations created | Only reachable functions remain |
| Main function processed | Dead code eliminated |

#### Phase 7: Type Updates (infer.c:5118-5134)

| Pre-condition | Post-condition |
|---------------|----------------|
| Specialization complete | Type registry finalized |
| Tree shaking complete | All types resolved |
| | Ready for codegen |

---

## Arena Management

### Arena Types

| Arena | Allocation Lifetime | Reset Frequency |
|-------|---------------------|-----------------|
| `self->arena` (persistent) | Entire compilation | Never |
| `self->transient` | Per-phase working data | After each phase |

### What Goes Where

**Persistent arena (`self->arena`):**
- Alpha-converted variable names
- Type environment
- Type substitutions
- Cloned AST nodes for specializations
- Final output structures

**Transient arena (`self->transient`):**
- Phase 1: Name mapping during alpha conversion
- Phase 3: Type inference working data
- Phase 5: Specialization context, cloning scratch
- Temporary hash tables, arrays

**Critical rule:** Any data that must survive `arena_reset(self->transient)` must be allocated in `self->arena`.

---

## Type Attachment Timeline

| Phase | AST Type Status | Purpose |
|-------|-----------------|---------|
| Parser | null | Parser doesn't infer types |
| Phase 1 | **ALL ERASED** | Prevents pollution |
| Phase 2 | null | Inherited from Phase 1 |
| Phase 3 | Types attached | Generic type variables |
| Phase 4 | Substitutions applied | Some types made concrete |
| Phase 5 (clone) | **ERASED again** | Each specialization independent |
| Phase 5 (post_specialize) | Types re-attached | Specialized monomorphic types |
| Phase 6-7 | Concrete types | Final types for codegen |

**Critical pattern:** Types are erased TWICE:
1. Phase 1: On original AST
2. Phase 5: When cloning for specialization (inherited null from generic, but `rename_variables` is called again which re-erases)

---

## Violation Detection Checklist

### Type Pollution Red Flags

- [ ] Type attached to AST node after Phase 1 but before Phase 3
- [ ] Original names (`T`) used in type environment instead of alpha-converted (`tl_T_v0`)
- [ ] Specialized clones sharing `ctx.lex` (should be independent)
- [ ] `post_specialize()` not called for a cloned generic function
- [ ] Generic function appears in final transpiled code

### Lexical Scope Red Flags

- [ ] Type variable from outer scope re-renamed in inner scope
- [ ] `map_copy(ctx.lex)` not called before entering lambda/let scope
- [ ] `ctx->lex = save` not called after exiting scope
- [ ] Mangled name checked but original name in scope not consulted

### Specialization Red Flags

- [ ] Two specializations with different types have same instance name
- [ ] Type variables remain in final function types (should be concrete)
- [ ] Function pointer type arguments not specialized
- [ ] Nested generic calls not recursively specialized

### Arena Red Flags

- [ ] Data accessed after arena reset (use-after-free)
- [ ] Persistent data allocated in transient arena (lost on reset)
- [ ] Missing `ast_node_clone()` before arena reset

---

## Key Source Locations

| Component | File | Lines |
|-----------|------|-------|
| Main compilation flow | tess_exe.c | 762-911 |
| Alpha conversion entry | infer.c | 4953-4965 |
| `rename_variables` | infer.c | 3852 |
| `rename_one_function_param` | infer.c | 3763 |
| Generic inference | infer.c | 4442-4555 |
| Specialization entry | infer.c | 5021-5089 |
| `clone_generic_for_arrow` | infer.c | 1800-1868 |
| `post_specialize` | infer.c | 3230-3252 |
| All 7 phases | infer.c | 4950-5156 |
