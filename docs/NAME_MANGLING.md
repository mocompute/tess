# Name Mangling in Tess

This document describes the name mangling system in the Tess compiler. Name mangling transforms identifiers to support features that require unique naming in the generated C code.

## Table of Contents

1. [Overview](#overview)
2. [Arity Mangling](#arity-mangling)
3. [Module Mangling](#module-mangling)
4. [Generic Specialization Naming](#generic-specialization-naming)
5. [Two-Pass Parsing](#two-pass-parsing)
6. [Key Data Structures](#key-data-structures)
7. [Examples](#examples)

---

## Overview

The Tess compiler uses three distinct types of name mangling, applied in sequence:

| Stage | Purpose | Format | Example |
|-------|---------|--------|---------|
| 1. Arity | Function overloading by parameter count | `name__N` | `add__2` |
| 2. Module | Namespace separation | `Module__name` | `Math__add__2` |
| 3. Specialization | Unique names for monomorphized generics | `name_N` | `Math__add__2_0` |

### Order of Operations

Mangling is applied in a specific order during compilation:

```
Source: Math.add/2       (user syntax for function pointer)
     |
     v
Arity:  Math.add__2      (internal arity format)
     |
     v
Module: Math__add__2     (module prefix applied)
     |
     v
Specialization: Math__add__2_0   (if generic, specialized instance)
```

### What Gets Mangled

| Symbol Type | Arity | Module | Specialization |
|-------------|-------|--------|----------------|
| Regular functions | Yes | Yes | If generic |
| `main` function | No | No | No |
| `c_*` symbols | No | No | No |
| `_tl_*` intrinsics | No | No | No |
| Builtin module symbols | Yes | No | No |
| Types | No | Yes | If generic |

### Reserved Identifier Pattern

Because the compiler uses `__` (double underscore) as a separator in mangled names, user-defined identifiers and module names containing `__` are rejected by the parser. This prevents accidental collisions with compiler-generated names.

Two exceptions are allowed:
- **`__init`** - The compiler-recognized module initialization function
- **`c_*` prefixed symbols** - C interop bindings (e.g., `c__Exit` wrapping C's `_Exit`)

---

## Arity Mangling

Arity mangling enables **function overloading by parameter count**. Multiple functions can share the same name if they have different numbers of parameters.

### User Syntax

When referencing a function as a value (function pointer), use `/N` syntax:

```tl
add(a, b) { a + b }
add(a, b, c) { a + b + c }

fp := add/2              // Pointer to two-argument version
```

### Internal Format

The compiler transforms `/N` syntax to the internal `__N` format:

| User Syntax | Internal Name |
|-------------|---------------|
| `add/2` | `add__2` |
| `process/1` | `process__1` |
| `init/0` | `init__0` |

### Special Cases

The following are **never arity-mangled**:

1. **`main`** - Entry point must have fixed C name
2. **`c_*` symbols** - Direct C interop, names must match C declarations
3. **Local variables/parameters** - Only module-level functions are mangled

The distinction between module functions and local variables is critical. Consider:

```tl
apply(f, x) { f(x) }    // f is a parameter, not a module function
```

Without proper handling, `f(x)` would be incorrectly mangled to `f__1(x)`. The parser uses `symbol_is_module_function()` to check if a symbol exists in the module's function table before mangling.

---

## Module Mangling

Module mangling prevents name collisions between modules by prefixing names with their module.

### Format

```
Module__name
```

Examples:
- `Math.add` → `Math__add`
- `Array.push` → `Array__push`
- `Alloc.alloc` → `Alloc__alloc`

Dotted module names have dots replaced with `__`:
- `Array.Indexed.iter_init` → `Array__Indexed__iter_init`

### Key Function

**`mangle_str_for_module()`** (parser.c)

```c
static str mangle_str_for_module(parser *self, str name, str module) {
    str safe_module = str_replace_char_str(self->ast_arena, module, '.', S("__"));
    return str_cat_3(self->ast_arena, safe_module, S("__"), name);
}
```

### Special Cases

The following are **never module-mangled**:

1. **`main` module symbols** - The `main` module sets `current_module` to empty, disabling module mangling
2. **Intrinsics (`_tl_*`)** - Compiler-internal functions
3. **C symbols (`c_*`)** - Must match external C names
4. **`builtin` module symbols** - Built-in functions like `sizeof`, `alignof`
5. **Type names** - Types use the type registry instead

### Cross-Module References

When referencing a symbol from another module:

```tl
#module main
#import <Math.tl>

main() {
    Math.add(1, 2)     // Becomes Math__add__2(1, 2)
}
```

The parser resolves `Math.add` by:
1. Looking up `Math` in `module_symbols`
2. Finding the arity-mangled name `add__2` in that module's symbol table
3. Applying module prefix: `Math__add__2`

### Nested Type Dot Syntax

The dot operator is also used to access nested types (nested structs and tagged union variants). The parser resolves `Type.Child` by:

1. Checking if the left side is a known module (handled by module mangling above)
2. If not, checking if the left side's original name is in `nested_type_parents`
3. Verifying the candidate child name (`Type__Child`) exists in the appropriate module's symbol table
4. If verified, rewriting to the mangled form and applying module prefix if cross-module

**Same-module example:**
```tl
Shape : | Circle { radius: Float } | Square { length: Float }
c := Shape.Circle(radius = 2.0)     // Resolves to Shape__Circle(...)
```

**Cross-module chained dots:**
```tl
c := Mod.Shape.Circle(radius = 2.0)
// Step 1: Mod.Shape → Mod__Shape (module mangling)
// Step 2: Mod__Shape.Circle → checks "Shape" in nested_type_parents
//         → builds Shape__Circle → applies module "Mod" → Mod__Shape__Circle
```

---

## Generic Specialization Naming

When a generic function or type is instantiated with concrete types, it receives a unique name. See [SPECIALIZATION.md](SPECIALIZATION.md) for the full specialization process.

### Format

```
name_N
```

Where `N` is a monotonically increasing counter.

### Instance Caching

To avoid creating duplicate specializations, the compiler maintains an instance cache:

```c
hashmap *instances;      // (name_hash, type_hash) => specialized_name
```

Before creating a new specialization, `instance_lookup_arrow()` checks if this combination of function name and concrete type already exists.

### Example

```tl
identity(x) { x }

main() {
    identity(42)        // Creates identity_0 : Int -> Int
    identity("hello")   // Creates identity_1 : CString -> CString
    identity(42)        // Reuses identity_0 (cached)
}
```

---

## Two-Pass Parsing

The parser uses two passes to correctly resolve arity mangling at call sites.

### The Problem

Consider:

```tl
add(a, b) { a + b }
apply(f, x) { f(x) }
```

When parsing `f(x)` in `apply`, how does the parser know whether `f` is:
- A module function that should be mangled to `f__1`?
- A parameter (function pointer) that should NOT be mangled?

### The Solution

**Pass 1: Symbol Collection** (`parser_parse_all_symbols()`)

The first pass collects all function definitions into `current_module_symbols`:

```c
int parser_parse_all_symbols(parser *self) {
    self->is_symbol_pass = 1;
    while (0 == parser_next(self)) {
        // Parse but don't fully process - just collect symbols
    }
    save_current_module_symbols(self);
}
```

**Pass 2: Full Parsing**

The second pass can now distinguish module functions from local variables:

```c
static int symbol_is_module_function(parser *self, ast_node *name, u8 arity) {
    // Generate the arity-mangled name to search for
    str mangled_name = mangle_str_for_arity(self->ast_arena, original_name, arity);

    // Check if it exists in current module's symbol table
    return str_hset_contains(self->current_module_symbols, mangled_name);
}
```

### Module Symbol Tables

The parser maintains three symbol tables:

| Table | Type | Purpose |
|-------|------|---------|
| `current_module_symbols` | `hashmap*` (str set) | Symbols in current module |
| `builtin_module_symbols` | `hashmap*` (str set) | Built-in symbols |
| `module_symbols` | `hashmap*` (str → hashmap*) | All modules' symbol tables |

---

## Key Data Structures

### `ast_symbol` Struct

```c
struct ast_symbol {
    str              name;       // Current (possibly mangled) name
    str              original;   // Original unmangled name
    str              module;     // Module name (set during module mangling)
    struct ast_node *annotation;
    tl_polytype     *annotation_type;
    int              is_mangled; // True if module-mangled
};
```

### Name Preservation

`ast_node_name_replace()` preserves the original name when mangling:

```c
void ast_node_name_replace(ast_node *node, str replace) {
    // Only save original once - don't overwrite on subsequent manglings
    if (str_is_empty(node->symbol.original))
        node->symbol.original = node->symbol.name;
    node->symbol.name = replace;
}
```

This ensures that after all mangling stages, the original source name is still accessible for error messages and debugging.

---

## Examples

### Arity Overloading

```tl
#module Math

add(a, b) { a + b }
add(a, b, c) { a + b + c }

main() {
    Math.add(1, 2)       // Calls Math__add__2
    Math.add(1, 2, 3)    // Calls Math__add__3

    fp := Math.add/2     // Function pointer to Math__add__2
}
```

Generated C names:
- `add(a, b)` → `Math__add__2`
- `add(a, b, c)` → `Math__add__3`

### Cross-Module Calls

```tl
// Array.tl
#module Array
push(arr, val) { ... }

// main.tl
#module main
#import <Array.tl>

main() {
    arr := Array.create()
    Array.push(arr, 42)    // Calls Array__push__2
}
```

### Generic Specialization

```tl
#module Utils

identity(x) { x }
swap(a, b) { (b, a) }

// main.tl
#module main
#import <Utils.tl>

main() {
    Utils.identity(42)       // Creates Utils__identity__1_0 : Int -> Int
    Utils.identity("hi")     // Creates Utils__identity__1_1 : CString -> CString
    Utils.swap(1, 2)         // Creates Utils__swap__2_0 : (Int, Int) -> (Int, Int)
}
```

### Function Pointers vs Local Variables

```tl
#module main

double(x) { x * 2 }

// 'f' is a parameter - NOT arity mangled
apply(f, x) { f(x) }

main() {
    // Get function pointer using /1 syntax
    fp := double/1

    // Pass to higher-order function
    apply(fp, 21)    // f(x) stays as f(x), not f__1(x)
}
```

In `apply`, the parser correctly identifies:
- `f` is a parameter, not in `current_module_symbols` → no arity mangling
- `double/1` references a module function → becomes `double__1`

### Tagged Union Helpers

Tagged unions generate internal helper types that also get mangled:

```tl
#module Types

Option[a] : | Some { v: a } | None

// User syntax for constructors:
//   Types.Option.Some(v = 42)
//   Types.Option.None()
//
// Generated internal names:
// Option__Some     → Types__Option__Some   (variant struct, scoped)
// Option__None     → Types__Option__None   (variant struct, scoped)
// __Option__Tag_   → Types____Option__Tag_ (tag enum)
// __Option__Union_ → Types____Option__Union_ (internal union)
```

The dot syntax `Type.Variant` is desugared to `Type__Variant` during parsing, before module mangling is applied.
