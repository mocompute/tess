# Name Mangling in Tess

This document describes the name mangling system in the Tess compiler. Name mangling transforms identifiers to support features that require unique naming in the generated C code.

## Table of Contents

1. [Overview](#overview)
2. [Arity Mangling](#arity-mangling)
3. [Module Mangling](#module-mangling)
4. [Generic Specialization Naming](#generic-specialization-naming)
5. [Two-Pass Parsing](#two-pass-parsing)
6. [Key Data Structures](#key-data-structures)
7. [Function Reference](#function-reference)
8. [Examples](#examples)

---

## Overview

The Tess compiler uses three distinct types of name mangling, applied in sequence:

| Stage | Purpose | Format | Example |
|-------|---------|--------|---------|
| 1. Arity | Function overloading by parameter count | `name__N` | `add__2` |
| 2. Module | Namespace separation | `Module_name` | `Math_add__2` |
| 3. Specialization | Unique names for monomorphized generics | `name_N` | `Math_add__2_0` |

### Order of Operations

Mangling is applied in a specific order during compilation:

```
Source: Math.add/2       (user syntax for function pointer)
     |
     v
Arity:  Math.add__2      (internal arity format)
     |
     v
Module: Math_add__2      (module prefix applied)
     |
     v
Specialization: Math_add__2_0   (if generic, specialized instance)
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

### Key Functions

**`unmangle_arity()`** (`parser.c:1620`)
Parses user `/N` syntax and returns the arity, or -1 if not arity-qualified.

```c
static int unmangle_arity(str name) {
    // Returns -1 if name is not mangled with format `foo/X`
    // Otherwise returns X (range 0-255)
    char const *p = strrchr(s, '/');
    // ... validation and parsing ...
}
```

**`mangle_str_for_arity()`** (`parser.c:1709`)
Converts a name to internal arity format.

```c
str mangle_str_for_arity(allocator *alloc, str name, u8 arity) {
    return str_fmt(alloc, "%s__%i", str_cstr(&name), (int)arity);
}
```

**`mangle_name_for_arity()`** (`parser.c:1741`)
Applies arity mangling to an AST node, with special case handling.

```c
static void mangle_name_for_arity(parser *self, ast_node *name, u8 arity, int is_definition) {
    if (!ast_node_is_symbol(name)) return;
    if (str_eq(name_str, S("main"))) return;        // Never mangle main
    if (is_c_symbol(name_str)) return;               // Never mangle c_* symbols
    // For calls (not definitions), only mangle known module functions
    if (!is_definition && !symbol_is_module_function(self, name, arity)) return;
    ast_node_name_replace(name, mangle_str_for_arity(...));
}
```

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
Module_name
```

Examples:
- `Math.add` → `Math_add`
- `Array.push` → `Array_push`
- `Alloc.alloc` → `Alloc_alloc`

### Key Functions

**`mangle_str_for_module()`** (`parser.c:1705`)

```c
static str mangle_str_for_module(parser *self, str name, str module) {
    return str_cat_3(self->ast_arena, module, S("_"), name);
}
```

**`mangle_name_for_module()`** (`parser.c:1729`)

```c
static void mangle_name_for_module(parser *self, ast_node *name, str module) {
    if (ast_node_is_symbol(name) && !str_is_empty(module)) {
        ast_node_name_replace(name, mangle_str_for_module(self, name->symbol.name, module));
        name->symbol.is_mangled = 1;
        name->symbol.module     = str_copy(self->ast_arena, module);
    }
}
```

**`mangle_name()`** (`parser.c:1763`)
Main entry point for module mangling, with all special case checks.

```c
static void mangle_name(parser *self, ast_node *name) {
    // Skip if no current module (we're in `main` module)
    if (str_is_empty(self->current_module)) return;
    // Skip if already mangled
    if (name->symbol.is_mangled) return;
    // Skip intrinsics (_tl_*)
    if (is_intrinsic(name->symbol.name)) return;
    // Skip known types
    if (tl_type_registry_get(self->opts.registry, name_str)) return;
    // Skip c_* symbols
    if (is_c_symbol(name_str)) return;
    // Skip builtin module symbols
    if (str_eq(self->current_module, S("builtin"))) return;
    if (str_hset_contains(self->builtin_module_symbols, name_str)) return;
    // Skip symbols not defined in current module
    if (!str_hset_contains(self->current_module_symbols, name_str)) return;

    mangle_name_for_module(self, name, self->current_module);
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
    Math.add(1, 2)     // Becomes Math_add__2(1, 2)
}
```

The parser resolves `Math.add` by:
1. Looking up `Math` in `module_symbols`
2. Finding the arity-mangled name `add__2` in that module's symbol table
3. Applying module prefix: `Math_add__2`

---

## Generic Specialization Naming

When a generic function or type is instantiated with concrete types, it receives a unique name. See [SPECIALIZATION.md](SPECIALIZATION.md) for the full specialization process.

### Format

```
name_N
```

Where `N` is a monotonically increasing counter.

### Key Function

**`next_instantiation()`** (`infer.c:3092`)

```c
static str next_instantiation(tl_infer *self, str name) {
    char buf[128];
    snprintf(buf, sizeof buf, "%.*s_%u", str_ilen(name), str_buf(&name),
             self->next_instantiation++);
    return str_init(self->arena, buf);
}
```

### Instance Caching

To avoid creating duplicate specializations, the compiler maintains an instance cache:

```c
hashmap *instances;      // (name_hash, type_hash) => specialized_name
```

Before creating a new specialization, `instance_lookup_arrow()` checks if this combination of function name and concrete type already exists:

```c
static str *instance_lookup_arrow(tl_infer *self, str generic_name, tl_monotype *arrow) {
    name_and_type key = {
        .name_hash = str_hash64(generic_name),
        .type_hash = tl_monotype_hash64(arrow),
    };
    return instance_lookup(self, &key);
}
```

### Example

```tl
identity(x: a) -> a { x }

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

## Function Reference

| Function | File | Line | Purpose |
|----------|------|------|---------|
| `unmangle_arity` | parser.c | 1620 | Parse user `/N` syntax |
| `unmangle_arity_qualified_name` | parser.c | 1636 | Extract name from arity-qualified symbol |
| `unmangle_name` | parser.c | 1646 | Restore original name from mangled |
| `symbol_is_module_function` | parser.c | 1663 | Check if symbol is a module function |
| `mangle_str_for_module` | parser.c | 1705 | Create module-prefixed string |
| `mangle_str_for_arity` | parser.c | 1709 | Create arity-suffixed string |
| `mangle_name_for_module` | parser.c | 1729 | Apply module mangling to AST node |
| `mangle_name_for_arity` | parser.c | 1741 | Apply arity mangling to AST node |
| `mangle_name` | parser.c | 1763 | Main module mangling entry point |
| `ast_node_name_replace` | ast.c | 455 | Replace name, preserving original |
| `next_instantiation` | infer.c | 3092 | Generate unique specialization name |
| `instance_lookup_arrow` | infer.c | 2167 | Look up cached specialization |
| `instance_add` | infer.c | 2181 | Cache a new specialization |
| `is_c_symbol` | infer.c | 4101 | Check for `c_` prefix |
| `is_intrinsic` | infer.c | 4097 | Check for `_tl_` prefix |

---

## Examples

### Arity Overloading

```tl
#module Math

add(a, b) { a + b }
add(a, b, c) { a + b + c }

main() {
    Math.add(1, 2)       // Calls Math_add__2
    Math.add(1, 2, 3)    // Calls Math_add__3

    fp := Math.add/2     // Function pointer to Math_add__2
}
```

Generated C names:
- `add(a, b)` → `Math_add__2`
- `add(a, b, c)` → `Math_add__3`

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
    Array.push(arr, 42)    // Calls Array_push__2
}
```

### Generic Specialization

```tl
#module Utils

identity(x: a) -> a { x }
swap(a: t, b: t) -> (t, t) { (b, a) }

// main.tl
#module main
#import <Utils.tl>

main() {
    Utils.identity(42)       // Creates Utils_identity__1_0 : Int -> Int
    Utils.identity("hi")     // Creates Utils_identity__1_1 : CString -> CString
    Utils.swap(1, 2)         // Creates Utils_swap__2_0 : (Int, Int) -> (Int, Int)
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

Option(a) : Some(a) | None

// Generated internal names:
// _OptionTag_  → Types__OptionTag_
// _OptionUnion_ → Types__OptionUnion_
```

See [TAGGED_UNIONS.md](TAGGED_UNIONS.md) for details on tagged union implementation.
