# Tagged Unions in Tess: Technical Documentation

This document provides comprehensive technical documentation about how tagged unions are implemented in the Tess compiler, including their interaction with the monomorphisation (specialization) machinery.

## Table of Contents

1. [Overview](#1-overview)
2. [Syntax and Semantics](#2-syntax-and-semantics)
3. [Parser: Desugaring Process](#3-parser-desugaring-process)
4. [Type System Representation](#4-type-system-representation)
5. [Type Inference and Specialization](#5-type-inference-and-specialization)
6. [Case Expression Type Checking](#6-case-expression-type-checking)
7. [C Code Generation](#7-c-code-generation)
8. [Key Implementation Details](#8-key-implementation-details)
9. [Common Issues and Solutions](#9-common-issues-and-solutions)

---

## 1. Overview

Tagged unions (also called discriminated unions or sum types) in Tess are algebraic data types that can hold one of several variant types. Each variant is identified by a tag (discriminator) at runtime.

**Key Files:**
- `src/tess/src/parser.c` - Parsing and desugaring (`toplevel_tagged_union()`)
- `src/tess/src/type.c` - Type representation and specialization
- `src/tess/src/infer.c` - Type inference and case expression checking
- `src/tess/src/transpile.c` - C code generation

---

## 2. Syntax and Semantics

### Basic Tagged Union

```tess
Shape : | Circle    { radius: Float }
        | Square    { length: Float }
        | Rectangle { length: Float, height: Float }
```

### Generic Tagged Union

```tess
Option(a) : | Some { value: a }
            | None

Either(a, b) : | Left  { v: a }
               | Right { v: b }
```

### Existing Types as Variants

A variant can reference a pre-existing type using module-qualified syntax:

```tess
#module Geo

Point: { x: Float, y: Float }

Shape: | Circle { radius: Float }
       | Geo.Point
       | None
```

For types defined in the `main` module, use `main.TypeName`:

```tess
#module main

Point: { x: Float, y: Float }

Shape: | Circle { radius: Float }
       | main.Point
       | None
```

The module-qualified syntax (`Module.Type`) distinguishes existing type references from new zero-field variants. A bare name (e.g., `| None`) always creates a new variant.

### Construction

There are three ways to construct tagged union values:

**1. Unscoped constructor functions (positional arguments):**

Constructor functions are generated at module level with unscoped names. They take positional arguments and return the wrapped tagged union type:

```tess
circle := Circle(2.0)           // returns Shape
some_int := Some(42)            // returns Option(Int)
left := Left("hello")           // returns Either(String, b)
```

**2. Scoped type constructors (named arguments):**

Variant structs are scoped under the tagged union type and accessed via dot syntax. They use named arguments and return the bare variant struct, *not* the wrapped tagged union:

```tess
c := Shape.Circle(radius = 2.0)   // returns Shape__Circle (bare struct)
s := Option.Some(value = 42)      // returns Option__Some (bare struct)
```

**3. Make functions (wrapping bare variants):**

Per-variant `make_` functions wrap a bare variant struct into the tagged union:

```tess
c := Shape.Circle(radius = 2.0)   // bare Circle struct
shape := make_Shape_Circle(c)     // wrapped Shape

// Especially useful for existing types as variants:
pt := Point(x = 1.0, y = 2.0)
shape := make_Shape_Point(pt)
```

No constructor function is generated for existing type variants — use the type's own constructor and the make function instead.

### Pattern Matching (Case Expression)

```tess
area := case shape: Shape {
    c: Circle    { c.radius * c.radius * 3.14159 }
    s: Square    { s.length * s.length }
    r: Rectangle { r.length * r.height }
}
```

### Mutable Pattern Matching

Using `.&` suffix creates mutable bindings (pointers to variants):

```tess
case shape.&: Shape {
    c: Circle {
        c->radius = 5  // c is Ptr(Circle)
        void
    }
    else { }
}
```


---

## 3. Parser: Desugaring Process

**Location:** `src/tess/src/parser.c`, function `toplevel_tagged_union()`

A tagged union definition is desugared into **6 categories** of AST nodes:

### 3.1 Desugaring Example

For `Shape : | Circle { radius: Float } | Square { length: Float }`:

```
1. __Shape__Tag_          : { Circle, Square }                       // Tag enum
2. Shape__Circle          : { radius: Float }                        // Variant struct (scoped)
3. Shape__Square          : { length: Float }                        // Variant struct (scoped)
4. __Shape__Union_        : { | Circle: Shape__Circle | Square: Shape__Square }  // Internal union
5. Shape                  : { tag: __Shape__Tag_, u: __Shape__Union_ }           // Wrapper struct
6. Circle(radius) -> Shape       // Unscoped constructor function
7. Square(length) -> Shape       // Unscoped constructor function
8. make_Shape_Circle(v) -> Shape // Make function
9. make_Shape_Square(v) -> Shape // Make function
```

### 3.2 Detailed Breakdown

#### Tag Enum
```c
str tag_name = str_cat_3(self->ast_arena, S("__"), tu_name_str, S("__Tag_"));
// Creates: __Shape__Tag_ : { Circle, Square }
ast_node *tag_enum = create_enum_utd(self, tag_name, tag_idents);
```

#### Variant Structs

Variant structs are **scoped under the tagged union type** using double-underscore separation. They are accessed via dot syntax through the `nested_type_parents` mechanism (e.g., `Shape.Circle` resolves to `Shape__Circle`).

Existing type variants (specified with module-qualified syntax) are skipped — they already have a struct definition.

```c
// For each non-existing variant:
str var_name_str = str_cat_3(self->ast_arena, tu_name_str, S("__"), v->name->symbol.name);
// Creates: Shape__Circle : { radius: Float }
```

**Important:** Each variant only includes type parameters that its fields actually reference. This is determined by `collect_used_type_params()`.

#### Internal Union
```c
str union_name_str = str_cat_3(self->ast_arena, S("__"), tu_name_str, S("__Union_"));
// Creates: __Shape__Union_ : { | Circle: Shape__Circle | Square: Shape__Square }
```

For existing type variants, the union field references the module-mangled original type name (e.g., `Foo__Special`) instead of a scoped variant name.

#### Wrapper Struct
```c
// Creates: Shape : { tag: __Shape__Tag_, u: __Shape__Union_ }
```

#### Constructor Functions (unscoped)

Generated by `create_variant_constructor()`. Constructor functions are **unscoped** — they use the bare variant name (e.g., `Circle`, not `Shape__Circle`) as the function name. They take positional arguments matching the variant's fields and return the wrapped tagged union type.

Skipped for existing type variants (the type already has its own constructor).

The constructor body:
1. Constructs the scoped variant struct: `Shape__Circle(radius = radius)`
2. Wraps in union: `__Shape__Union_(Circle = innerCall)`
3. Creates tag value: `__Shape__Tag_.Circle`
4. Constructs wrapper: `Shape(tag = tagValue, u = unionCall)`

#### Make Functions

Generated by `create_variant_make_function()`. One per variant (including existing type variants). Takes a single argument of the variant struct type and returns the tagged union wrapper.

```
make_Shape_Circle(v: Shape__Circle) -> Shape { ... }
make_Shape_Special(v: Foo__Special) -> Shape { ... }  // existing type
```

### 3.3 Existing Type Variant Detection

During variant parsing, the parser checks for dot syntax to distinguish existing types from new variants:

```c
// Parse variant name
if (a_try(self, a_identifier)) return ERROR_STOP;
ast_node *var_name = self->result;

// Check for dot: Module.Type means existing type reference
if (0 == a_try(self, a_dot)) {
    existing_module = var_name->symbol.name;
    if (a_try(self, a_identifier)) return ERROR_STOP;
    var_name    = self->result;
    is_existing = 1;
}
```

For existing types, the mangled name is computed using `mangle_str_for_module()`, with a special case for the `main` module (whose types are unmangled).

### 3.4 Key Helper Functions

#### `collect_used_type_params()`
Scans field annotations to determine which type parameters are actually used:
```c
static u8 collect_used_type_params(parser *self, u8 n_type_args,
                                   ast_node **type_args,
                                   ast_node_array fields,
                                   ast_node ***out_used_type_args)
```

#### `annotation_uses_type_param()`
Recursively checks if an annotation references a type parameter:
```c
static int annotation_uses_type_param(ast_node *node, str param_name)
```

---

## 4. Type System Representation

**Important Clarification:** Tagged unions have **no direct representation** in the type system. They are purely a syntactic feature that desugars into ordinary types:
- The tag enum (`__Shape__Tag_`) is a regular enum type
- The variant structs (`Shape__Circle`, `Shape__Square`) are regular struct types
- The internal union (`__Shape__Union_`) is a C-style union type (declared with `is_union = 1`)
- The wrapper struct (`Shape`) is a regular struct type

After parsing, the type system sees only these desugared types - it has no concept of "tagged union" as a distinct type.

### 4.1 C-Style Unions vs. Tagged Unions

The type system has a concept of **C-style unions** (types with `is_union = 1` in the AST), which are used internally for the `__Shape__Union_` component of tagged unions. These are untagged unions that map directly to C's `union` keyword.

This is **completely different** from the `Union` type constructor in `type.c`, which is a partially-implemented feature for representing union types like `Union(Ptr, Void)` in the type system. The `Union` type constructor is not used by tagged unions.

### 4.2 Type Constructor Definition

When a user type definition is parsed (struct, enum, or C-style union), it creates a `tl_type_constructor_def` containing:
- `name` - canonical type name
- `generic_name` - unspecialized name (for aliases)
- `field_names` - array of field names

For tagged unions, this happens separately for each desugared component (tag enum, variant structs, internal union, wrapper struct).

---

## 5. Type Inference and Specialization

**Location:** `src/tess/src/infer.c`

### 5.1 C-Style Union Detection

`is_union_struct()` detects C-style unions (the internal `__Shape__Union_` type), not tagged unions:
```c
int is_union_struct(tl_infer *self, str name) {
    ast_node *utd = toplevel_get(self, name);
    if (utd && ast_node_is_union_def(utd)) return 1;  // checks is_union == 1
    return 0;
}
```

This is used during specialization to handle the internal union type differently from regular structs.

### 5.2 Generic Specialization for C-Style Unions

`specialize_user_type()` handles C-style union specialization specially:

```c
} else if (is_union_struct(self, name)) {
    // For C-style unions, get args from the node's inferred type, not AST arguments.
    // Union constructions pass only one variant value, but specialization
    // needs all variant types from the inferred type.
    if (node->type && tl_monotype_is_inst(node->type->type)) {
        tl_monotype *mono = node->type->type;
        tl_monotype_substitute(self->arena, mono, self->subs, null);
        arr_sized = mono->cons_inst->args;
    } else {
        return 0; // Type not ready yet
    }
}
```

**Key Insight:** When constructing a C-style union (e.g., `__Shape__Union_(Some = value)`), the AST only contains one argument (the active variant's value), but the type system needs ALL variant types for specialization. The solution is to extract arguments from the node's inferred type instead of the AST.

### 5.3 The Arity Mismatch Problem (Historical Bug)

For a generic tagged union like `Either(a, b)`:
- Variant `Left` only uses type param `a` (arity 1)
- Variant `Right` only uses type param `b` (arity 1)
- Union `__Either__Union_(a, b)` has arity 2

When the parser originally passed ALL parent type params to each variant, it created a mismatch:
- `Left(a, b)` created with 2 params, but only `a` used in fields
- Type system filtered to `Left(t)` with 1 param
- Union field `Left: Left(a, b)` expected 2 params - **CRASH**

**Solution:** `collect_used_type_params()` determines which params each variant uses, ensuring arities match.

### 5.4 Constructor Body Specialization Cascade

When a tagged union constructor is specialized, the specialization must cascade through all nested type constructions in the function body. This is handled by `post_specialize()` in `infer.c`.

**Example:** For `Option(a) : | Some { value: a } | None`, calling `Some(42)`:

1. **Initial specialization:** `Some` becomes `Some_0(value: Int) -> Option_0(Int)`

2. **Body analysis:** The constructor body contains nested calls:
   ```tess
   Shape(
       tag = __Option__Tag_.Some,
       u = __Option__Union_(Some = Option__Some(value = value))
   )
   ```

3. **Cascading specialization:** `post_specialize()` traverses the body and specializes each nested call:
   - `Option__Some(value = value)` → `Option__Some_0(value: Int)`
   - `__Option__Union_(Some = ...)` → `__Option__Union__0(Some: Option__Some_0)`
   - `Option(tag = ..., u = ...)` → `Option_0(tag: __Option__Tag_, u: __Option__Union__0)`

**Implementation in `post_specialize()`:**

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

The recursive call to `specialize_applications_cb` in step 3 is what triggers the cascade—each nested call is visited and specialized, which may trigger further `post_specialize()` calls for those functions.

### 5.5 Internal Union (`__TUnion_`) Specialization

The internal C-style union requires special handling during specialization, different from regular structs:

**The Problem:** When constructing `__Option__Union_(Some = value)`, the AST contains only one argument (the active variant), but the type needs information about ALL variants for proper specialization.

**The Solution:** `specialize_user_type()` detects C-style unions via `is_union_struct()` and extracts arguments from the **inferred type** rather than the AST:

```c
if (is_union_struct(self, name)) {
    // Get all variant types from the inferred type, not AST arguments
    if (node->type && tl_monotype_is_inst(node->type->type)) {
        tl_monotype *mono = node->type->type;
        tl_monotype_substitute(self->arena, mono, self->subs, null);
        arr_sized = mono->cons_inst->args;  // Contains ALL variant types
    }
}
```

This ensures that `__Option__Union_(a)` is properly specialized to `__Option__Union__0` with both `Option__Some_0` and `Option__None` variant types, even though only one variant was provided at the construction site.

---

## 6. Case Expression Type Checking

**Location:** `src/tess/src/infer.c`, function `infer_case()`

### 6.1 Tagged Union Case Handling

```c
if (node->case_.is_union) {
    // Get wrapper type and extract union type
    tl_monotype *wrapper_type = expr_type->type;
    i32 u_index = tl_monotype_type_constructor_field_index(wrapper_type, S("u"));
    tl_monotype *union_type = wrapper_type->cons_inst->args.v[u_index];
    str_sized valid_variants = union_type->cons_inst->def->field_names;
    // ...
}
```

### 6.2 Variant Coverage Tracking

The compiler tracks which variants are covered:
- Allocates array to track covered variants
- For each case condition, looks up variant in union's field names
- Marks variant as covered
- Reports error for unknown variant names

### 6.3 Type Binding for Conditions

For each matched variant:
```c
tl_monotype *variant_type = union_type->cons_inst->args.v[variant_found];

tl_polytype *variant_poly = null;
if (node->case_.is_union == AST_TAGGED_UNION_MUTABLE) {
    // Mutable case (.&): binding is pointer to variant
    variant_poly = tl_polytype_absorb_mono(
        self->arena,
        tl_type_registry_ptr(self->registry, variant_type));
} else {
    // Immutable case: binding is variant value
    variant_poly = tl_polytype_absorb_mono(self->arena, variant_type);
}
```

### 6.4 Exhaustiveness Checking

If no else arm exists, all variants must be covered:
```c
if (!has_else_arm) {
    forall(j, valid_variants) {
        if (!variant_covered[j]) {
            array_push(self->errors, ((tl_infer_error){
                .tag = tl_err_tagged_union_missing_case,
                .node = node}));
            return 1;
        }
    }
}
```

---

## 7. C Code Generation

**Location:** `src/tess/src/transpile.c`, function `generate_tagged_union_case()`

### 7.1 Generated C Structure

For `Shape : | Circle { radius: Float } | Square { length: Float }` in module `Foo`:

```c
typedef enum {
    Foo____Shape__Tag___Circle,
    Foo____Shape__Tag___Square
} Foo____Shape__Tag_;

typedef struct { float radius; } Foo__Shape__Circle;
typedef struct { float length; } Foo__Shape__Square;

typedef union {
    Foo__Shape__Circle Circle;
    Foo__Shape__Square Square;
} Foo____Shape__Union_;

typedef struct {
    Foo____Shape__Tag_ tag;
    Foo____Shape__Union_ u;
} Foo__Shape;
```

### 7.2 Case Expression Code Generation

```c
// Input:
case shape: Shape {
    c: Circle { c.radius * c.radius }
    s: Square { s.length * s.length }
}

// Output:
float res;
if (shape.tag == Foo____Shape__Tag___Circle) {
    Foo__Shape__Circle c = shape.u.Circle;
    res = c.radius * c.radius;
    goto end;
}
else if (shape.tag == Foo____Shape__Tag___Square) {
    Foo__Shape__Square s = shape.u.Square;
    res = s.length * s.length;
    goto end;
}
end:
```

### 7.3 Mutable Case Generation

```c
// Input:
case shape.&: Shape {
    c: Circle { c->radius = 5.0 }
}

// Output:
if (shape.tag == Foo____Shape__Tag___Circle) {
    Foo__Shape__Circle *c = &shape.u.Circle;
    c->radius = 5.0;
}
```

---

## 8. Key Implementation Details

### 8.1 AST Node Flags

The `is_union` field in `ast_case` has three possible values:
- `0` - Not a tagged union case (regular case or struct pattern)
- `1` (`AST_TAGGED_UNION_IMMUTABLE`) - Immutable tagged union case
- `2` (`AST_TAGGED_UNION_MUTABLE`) - Mutable tagged union case (with `.&`)

### 8.2 Naming Conventions

| Component | Pattern | Example |
|-----------|---------|---------|
| Tag enum | `__` + name + `__Tag_` | `__Shape__Tag_` |
| Variant struct (scoped) | name + `__` + variant | `Shape__Circle` |
| Internal union | `__` + name + `__Union_` | `__Shape__Union_` |
| Wrapper struct | name | `Shape` |
| Constructor function | variant (unscoped) | `Circle` |
| Make function | `make_` + name + `_` + variant | `make_Shape_Circle` |
| User access (variant struct) | name + `.` + variant | `Shape.Circle` |

### 8.3 Scoping via `nested_type_parents`

The `nested_type_parents` hash set in the parser enables dot-syntax access to variant structs. When the tagged union `Shape` is parsed, `"Shape"` is inserted into `nested_type_parents`. Later, when the parser encounters `Shape.Circle`, it recognizes `Shape` as a nested type parent and rewrites the access to `Shape__Circle`.

### 8.4 Error Types

From `src/tess/include/error.h`:
- `tl_err_tagged_union_case_syntax_error` - Invalid case condition syntax
- `tl_err_tagged_union_unknown_variant` - Variant name doesn't exist
- `tl_err_tagged_union_missing_case` - Not all variants covered (no else)
- `tl_err_expected_tagged_union` - Expected tagged union type

---

## 9. Common Issues and Solutions

### 9.1 Arity Mismatch in Generic Tagged Unions

**Problem:** For `Either(a, b) : | Left { v: a } | Right { v: b }`:
- Parser creates `Left(a, b)` but type system sees only 1 used param
- Union references `Left(a, b)` expecting 2 params - mismatch

**Solution:** `collect_used_type_params()` determines which params each variant uses:
```c
// Variant struct only gets params it uses
u8 var_n_type_args = collect_used_type_params(self, n_type_args, type_args,
                                               v->fields, &var_type_args);

// Union field annotation matches variant's actual arity
u8 n_used_type_args = collect_used_type_params(self, n_type_args, type_args,
                                                v->fields, &used_type_args);
```

### 9.2 Union Specialization from AST Arguments

**Problem:** Union construction AST has only 1 argument (active variant), but type needs all variants.

**Solution:** Extract arguments from inferred type instead of AST:
```c
if (is_union_struct(self, name)) {
    if (node->type && tl_monotype_is_inst(node->type->type)) {
        tl_monotype *mono = node->type->type;
        tl_monotype_substitute(self->arena, mono, self->subs, null);
        arr_sized = mono->cons_inst->args;  // All variant types from inferred type
    }
}
```

### 9.3 Function Signature Syntax

**Problem:** Using `:` for return types in function signatures fails.

**Correct Syntax:** Use `->` for return types:
```tess
// Wrong:
unwrap(opt: Option(Int)): Int { ... }

// Correct:
unwrap(opt: Option(Int)) -> Int { ... }
```

### 9.4 Existing Type Variants

**Constraint:** Existing type variants must use module-qualified syntax (`| Module.Type`). A bare name always creates a new variant. For types in the `main` module, use `| main.Type`.

**Limitation:** Generic existing types as variants are not yet supported. The `collect_used_type_params()` mechanism relies on scanning variant fields, which are empty for existing types.

---

## Appendix: Test Files

Working test files for reference:
- `src/tess/tl/test_tagged_union.tl` - Basic non-generic tagged union
- `src/tess/tl/test_tagged_union_unscoped.tl` - Unscoped positional constructors
- `src/tess/tl/test_tagged_union_scoped_variant.tl` - Scoped variant struct access and make functions
- `src/tess/tl/test_tagged_union_make.tl` - Per-variant make functions
- `src/tess/tl/test_tagged_union_existing_type.tl` - Existing type as variant (cross-module)
- `src/tess/tl/test_tagged_union_existing_type_main.tl` - Existing type as variant (main module)
- `src/tess/tl/test_tagged_union_generic_basic.tl` - Basic generic Option
- `src/tess/tl/test_tagged_union_generic_case.tl` - Case expression with inference
- `src/tess/tl/test_tagged_union_generic_func.tl` - Generic unwrap function
- `src/tess/tl/test_tagged_union_generic_multi.tl` - Multi-param Either type
- `src/tess/tl/test_tagged_union_generic_nested.tl` - Nested Option(Option(Int))
- `src/tess/tl/test_tagged_union_generic_param.tl` - Concrete type in signature
- `src/tess/tl/test_tagged_union_generic_return.tl` - Generic return type
- `src/tess/tl/test_tagged_union_function_pointer.tl` - Function pointers with tagged unions
- `src/tess/tl/test_tagged_union_generic_function_pointer.tl` - Generic function pointers
- `src/tess/tl/test_tagged_union_mutable_case.tl` - Mutable case expressions
- `src/tess/tl/test_tagged_union_option.tl` - Option type usage
- `src/tess/tl/test_fail_tagged_union_duplicate_variant.tl` - Duplicate variant name detection
- `src/tess/tl/test_fail_tagged_union_missing_case.tl` - Missing case arm detection
- `src/tess/tl/test_fail_tagged_union_unknown_variant.tl` - Unknown variant detection
