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
