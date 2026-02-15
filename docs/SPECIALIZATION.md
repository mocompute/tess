# Generic Specialization in Tess

This document provides a comprehensive analysis of the generic specialization flow in the Tess compiler.

## Table of Contents

1. [Overview](#overview)
2. [The Specialization Pipeline](#the-specialization-pipeline)
3. [Function Specialization](#function-specialization)
4. [Type Constructor Specialization](#type-constructor-specialization)
5. [Key Functions Reference](#key-functions-reference)

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

The specialization happens during type inference in `src/tess/src/infer.c`.

---

## The Specialization Pipeline

Specialization is integrated into the type inference phase. The key entry point is `specialize_applications_cb()`, which is invoked during AST traversal.

### High-Level Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Type Inference Phase                                │
├─────────────────────────────────────────────────────────────────────────────┤
│  1. Parse AST                                                               │
│  2. Rename variables (alpha-conversion for unique names)                    │
│     See ALPHA_CONVERSION.md for details                                     │
│  3. Assign type variables to symbols                                        │
│  4. Collect constraints                                                     │
│  5. Satisfy constraints via unification                                     │
│  6. ══════════════════════════════════════════════════════════════════════  │
│  │  SPECIALIZATION PASS                                                     │
│  │  traverse_ast(... specialize_applications_cb ...)                        │
│  │    ├── For each function call: specialize_arrow()                        │
│  │    ├── For each type constructor: specialize_user_type()                 │
│  │    └── Recursively specialize nested calls in specialized functions      │
│  ════════════════════════════════════════════════════════════════════════   │
│  7. Update types with specialization results                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Function Specialization

### Entry Point: `specialize_applications_cb()`

When traversing the AST and encountering a named function application (NFA), this callback:

1. **Checks if function needs specialization:**
   - Already specialized? Skip.
   - Intrinsic call? Skip.
   - Not yet in toplevel? Skip (too early, will be handled later).

2. **Creates a callsite arrow type:**
   ```c
   callsite = make_arrow_with(self, traverse_ctx, node, type);
   ```
   This constructs the concrete function type based on argument types at this call site.

3. **Specializes the function:**
   ```c
   specialize_arrow_with_name(self, ctx, traverse_ctx, node->named_application.name, callsite->type);
   ```

4. **Specializes function arguments** (in case any are function pointers):
   ```c
   specialize_arguments(self, ctx, traverse_ctx, node, callsite->type);
   ```

### Core Specialization: `specialize_arrow()`

This function creates a specialized version of a generic function:

```c
static str specialize_arrow(tl_infer *self, traverse_ctx *traverse_ctx, str name, tl_monotype *arrow) {
    // 1. Check if already specialized
    if (instance_name_exists(self, name)) return name;

    // 2. Check cache for this name+type combination
    str *found = instance_lookup_arrow(self, name, arrow);
    if (found) return *found;

    // 3. Create unique instance name (e.g., "identity_0")
    str inst_name = next_instantiation(self, name);
    instance_add(self, &key, inst_name);

    // 4. Clone the generic function's AST
    ast_node *generic_node = clone_generic_for_arrow(self, toplevel_get(self, name), arrow, inst_name);

    // 5. Add to environment and toplevel
    specialized_add_to_env(self, inst_name, arrow);
    toplevel_add(self, inst_name, generic_node);

    // 6. CRITICAL: Process the specialized function body
    post_specialize(self, traverse_ctx, special, arrow);

    return inst_name;
}
```

### Post-Specialization: `post_specialize()`

After creating a specialized function, its body must be analyzed to specialize any nested generic calls:

```c
static int post_specialize(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *special,
                           tl_monotype *callsite) {
    ast_node *infer_target = get_infer_target(special);
    if (infer_target) {
        // 1. Re-run type inference on the specialized function body
        traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb);

        // 2. Apply substitutions to make types concrete
        apply_subs_to_ast_node(self, infer_target);

        // 3. Recursively specialize any generic calls in the body
        traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb);
    }
    return 0;
}
```

This recursive step is crucial: when specializing a tagged union constructor like `Some(v: Int)`, the body contains nested calls like `__Option__Union_(Some = ...)` that must also be specialized.

---

## Type Constructor Specialization

### Entry Point: `specialize_user_type()`

When a type constructor is applied (e.g., `Array[Int]` or `Point[Float]`), this function handles specialization:

```c
static int specialize_user_type(tl_infer *self, ast_node *node) {
    // 1. Must be an NFA (named function application)
    if (!ast_node_is_nfa(node)) return 0;

    str name = node->named_application.name->symbol.name;

    // 2. Check if it's a union (special handling required)
    if (is_union_struct(self, name)) {
        // Only skip non-generic unions; generic unions need specialization
        ...
    }

    // 3. Collect argument types
    tl_monotype_array arr = {.alloc = self->transient};
    // ... iterate arguments and collect concrete types ...

    // 4. Specialize the type constructor
    str name_inst = specialize_type_constructor(self, name, arr_sized, &special_type);

    // 5. Update the callsite to use the specialized name
    ast_node_name_replace(node->named_application.name, name_inst);
}
```

### Core Type Specialization: `specialize_type_constructor_()`

This creates a specialized type definition:

```c
static str specialize_type_constructor_(tl_infer *self, str name, tl_monotype_sized args,
                                        tl_polytype **out_type, hashmap **seen) {
    // 1. Skip enums (they don't need specialization)
    ast_node *utd = toplevel_get(self, name);
    if (utd && ast_node_is_enum_def(utd)) return str_empty();

    // 2. Avoid infinite recursion
    if (hset_contains(*seen, &key, sizeof key)) return str_empty();

    // 3. Specialize nested type arguments first
    forall(i, args) {
        if (tl_monotype_is_inst(args.v[i])) {
            specialize_type_constructor_(self, generic_name, args.v[i]->cons_inst->args, &poly, seen);
        }
    }

    // 4. Check if this specialization already exists
    str *existing = instance_lookup(self, &key);
    if (existing) return *existing;

    // 5. Clone the type definition and give it a unique name
    ast_node *utd = ast_node_clone(self->arena, utd);
    ast_node_name_replace(utd->user_type_def.name, name_inst);

    // 6. Register the specialized type
    toplevel_add(self, name_inst, utd);
    tl_type_registry_specialize_commit(self->registry, inst_ctx);

    return name_inst;
}
```

---

## Key Functions Reference

| Function | Location | Purpose |
|----------|----------|---------|
| `specialize_applications_cb` | infer.c | Main traversal callback for specialization |
| `specialize_arrow` | infer.c | Creates specialized function instances |
| `specialize_arrow_with_name` | infer.c | Wrapper that handles name lookup and specialization |
| `post_specialize` | infer.c | Processes body of specialized functions |
| `specialize_user_type` | infer.c | Entry point for type constructor specialization |
| `specialize_type_constructor` | infer.c | Creates specialized type definitions |
| `specialize_type_constructor_` | infer.c | Recursive helper with cycle detection |
| `specialize_arguments` | infer.c | Specializes function pointer arguments |
| `is_union_struct` | infer.c | Checks if a name refers to a union type |
| `toplevel_tagged_union` | parser.c | Desugars tagged union syntax |
| `create_variant_constructor` | parser.c | Generates constructor functions for variants |

### Instance Tracking

The specialization system uses several data structures for caching and cycle detection:

- **Instance cache**: Maps (name, type) -> specialized name to avoid duplicate specializations
- **Seen set**: Tracks types currently being specialized to prevent infinite recursion
- **Toplevel map**: Stores all function and type definitions, including specialized ones

---

## See Also

- [ALPHA_CONVERSION.md](ALPHA_CONVERSION.md) - Variable renaming system that ensures type safety across specializations
- [NAME_MANGLING.md](NAME_MANGLING.md) - Name mangling for arity, modules, and instantiation naming
