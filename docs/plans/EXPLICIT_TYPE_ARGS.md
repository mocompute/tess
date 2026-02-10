# Explicit Type Arguments — Migration Plan

## Overview

Migrate Tess from implicit, parenthesized type arguments to explicit, square-bracket
type arguments (Go-style syntax). This is both a **syntax change** (`()` → `[]` for
type arguments) and a **semantic change** (required type parameter declarations on
generic functions).

### Before / After

```
BEFORE                                      AFTER
──────                                      ─────
Point(a) : { x: a, y: a }                  Point[a] : { x: a, y: a }
Option(T) : | Some { v: T } | None         Option[T] : | Some { v: T } | None
Pt = Point(Int)                             Pt = Point[Int]

x: Ptr(Int)                                 x: Ptr[Int]
s: Ptr(Const(CChar))                        s: Ptr[Const[CChar]]
buf: CArray(CChar, 256)                     buf: CArray[CChar, 256]
arr: Array(Int)                             arr: Array[Int]

empty(T: Type) -> Array(T) { ... }          empty[T]() -> Array[T] { ... }
with_capacity(T: Type, n: Int)              with_capacity[T](n: Int)
arr := empty(Int)                           arr := empty[Int]()
map(f, arr) { ... }                         map[a, b](f: (a)->b, arr: Array[a]) -> Array[b]

sizeof(T)                                   sizeof(T)          // unchanged
alignof(T)                                  alignof(T)         // unchanged
sizeof(Ptr(Void))                           sizeof(Ptr[Void])  // inner types change
Constructor(x = 1, y = 2)                   Constructor(x = 1, y = 2)  // unchanged
Variant(42)                                 Variant(42)        // unchanged (value args)
```

### Disambiguation Rules (indexing vs type arguments)

Square brackets `[]` are used for both pointer indexing and type arguments.
Context-based disambiguation:

| Context | Syntax | Interpretation |
|---------|--------|---------------|
| After identifier, before `(` | `name[T](args)` | Type args on function call |
| After known type name | `Ptr[T]` | Type constructor |
| In type position (after `:`, `->`) | `name[T]` | Type constructor |
| In type definition (before `:`) | `Name[a] :` | Type parameter declaration |
| In expression position, no `(` following | `arr[i]` | Pointer indexing |

**Key heuristic:** In expression position, `name[args]` is type arguments if followed
by `(`, otherwise indexing. In type position (annotations, definitions, return types),
`name[args]` is always type arguments.

### Scope

| Category | Files | Occurrences |
|----------|-------|-------------|
| `Ptr(` in type annotations | 61 | ~389 |
| `Array(` in type annotations | 12 | ~142 |
| `Const(` in type annotations | 13 | ~62 |
| `CArray(` in type annotations | 8 | ~19 |
| `Map(` in type annotations | 5 | ~14 |
| Generic struct definitions | ~40 locations | |
| Tagged union with type params | 14 files | |
| Type aliases | 5 files | |
| `T: Type` pattern | 7 files | ~30 occurrences |
| **Total .tl files needing changes** | **~80** | |

---

## Current State (WIP branch: `wip-explicit-type-args`)

Already implemented:
- **AST**: `ast_let` has `type_parameters[]`, `ast_named_function_application` has `type_arguments[]`
- **Parser**: `a_funcall()` and `toplevel_defun()` parse `[T, U]` before `(args)`
- **Inference**: `load_type_arguments()` pre-populates type arg context for `ast_let`;
  `assign_type_arguments()` matches call-site type args to definition params
- **Type predicates**: `T :: Int` works when `T` is an explicit type argument
- **Test**: `test.tl` at repo root exercises basic function def + call + type predicate

Not yet implemented:
- `[]` in type annotations (`Ptr[T]`, `Array[Int]`, etc.)
- `[]` in type/union definitions (`Point[a] : { ... }`)
- `[]` in type aliases
- Context-based disambiguation
- Migration of existing .tl files
- Required type parameter declarations on all generic functions
- Removal of `T: Type` pattern

---

## Phase 1: Parser — Square Brackets in Type Positions

**Goal:** The parser produces AST nodes with type arguments in the `type_arguments`
field when `[]` syntax is used, while keeping `()` syntax working so existing tests
don't break.

### 1a. Type annotations in `a_funcall()` / `a_type_constructor()`

Currently, `Ptr(T)` in a type annotation is parsed as an NFA node with `T` in
`arguments`. We need `Ptr[T]` to produce an NFA with `T` in `type_arguments` and
empty `arguments`.

**Files:** `parser.c` — `a_type_constructor()`, `a_funcall()`

- When parsing in type-annotation context and `[` follows the name, parse the
  contents as type arguments
- `Ptr[T]` → NFA with `name="Ptr"`, `type_arguments=["T"]`, `arguments=[]`
- `Ptr[Const[CChar]]` → nested NFAs, inner ones also use `type_arguments`

### 1b. Type definitions

`Point[a] : { x: a, y: a }` — the `[a]` replaces `(a)` in type definition headers.

**Files:** `parser.c` — `a_user_type_def()` or wherever UTD headers are parsed

- After the type name, try `[params]` before `:`
- `ast_user_type_definition` already has `type_arguments` field — just populate it
  from `[]` instead of `()`

### 1c. Tagged union definitions

Same as 1b but for tagged unions: `Option[T] : | Some { v: T } | None`

**Files:** `parser.c` — tagged union parsing

### 1d. Type aliases

`Pt = Point[Int]` — the RHS is a type expression using `[]`.

**Files:** `parser.c` — type alias parsing. The RHS is already parsed as a type
expression, so this should work once 1a is complete.

### 1e. Disambiguation

Implement context-based rules so `arr[i]` (indexing) and `foo[Int](0)` (type args)
are distinguished correctly.

**Files:** `parser.c`

- In expression position: `name[...]` is type args only if followed by `(`
- In type position: `name[...]` is always type args
- The parser may need a flag or context variable to know whether it's in type position

**Verification:** Write small test files exercising both `arr[i]` indexing and
`Ptr[T]` type args in the same file.

---

## Phase 2: Type System Integration

**Goal:** The type parser (`tl_type_registry_parse_type_()`) correctly handles NFA
nodes that carry type arguments in `type_arguments` instead of `arguments`.

### 2a. Type parser reads from `type_arguments`

**Files:** `type.c` — `tl_type_registry_parse_type_()`

Currently this function reads `node->named_application.arguments` to get type
constructor arguments. Add logic:

```
if node has type_arguments (n_type_arguments > 0):
    use type_arguments as the type constructor args
else:
    fall back to arguments (old syntax, during transition)
```

This must work for:
- Built-in unary types: `Ptr[T]`, `Const[T]`
- Built-in special types: `CArray[T, N]`, `Union[a, b]`
- User-defined generic types: `Array[T]`, `Map[K, V]`, `Point[a]`

### 2b. Nested type constructors

`Ptr[Const[CChar]]` produces a nested NFA tree. The inner `Const[CChar]` NFA must
also have its type args in `type_arguments`. The recursive `parse_type_()` call
handles this naturally if 2a is correct.

### 2c. `sizeof` / `alignof` interaction

`sizeof(Ptr[Void])` — the outer `sizeof()` uses regular `()` (it's a built-in
operator, not a type constructor). The inner `Ptr[Void]` uses `[]`. The parser must
parse the inside of `sizeof(...)` as a type expression that supports `[]`.

**Files:** `parser.c` — sizeof/alignof parsing; `type.c` — sizeof type resolution

### 2d. Type variable sugar

Currently `type_variable_sugar()` auto-creates type variables for unknown symbols in
type annotations. With explicit type args, this behavior should eventually be removed
(Phase 5), but during the transition it must continue to work for the old syntax.

**Verification:** `test.tl` and new focused test files covering `Ptr[Int]`,
`Array[Ptr[Int]]`, `CArray[CChar, 256]`, `Point[Int]` in various positions.

---

## Phase 3: Migration Script + Execution

**Goal:** Convert all .tl files from old `()` syntax to new `[]` syntax for type
arguments. Verify all tests pass.

### 3a. Write migration script

A Python script that handles the following transformations:

**Type definitions** (highest confidence — mechanical):
```
Point(a) :     →  Point[a] :
Option(T) : |  →  Option[T] : |
```
Pattern: `(\w+)\(([a-z][a-z, ]*)\)\s*:` where params are lowercase letters

**Type annotations** (high confidence — known type constructors):
Target known names: `Ptr`, `Const`, `Array`, `CArray`, `Map`, `Option`, `Result`,
`Union`, `Iter`, plus any user-defined generic types found by scanning definitions.

```
Ptr(T)           →  Ptr[T]
Ptr(Const(CChar)) →  Ptr[Const[CChar]]
CArray(CChar, 15) →  CArray[CChar, 15]
Array(T)         →  Array[T]
```

Must handle balanced parentheses for nesting. Must only convert in type positions
(after `:`, after `->`, in type definition fields, in `sizeof`/`alignof` args).

**Do NOT convert:**
- Struct constructors: `Point(x = 1, y = 2)` — has `name =` inside
- Tagged union value constructors: `Some(42)`, `Circle(2.0)` — positional value args
- Regular function calls: `add(1, 2)`
- `sizeof(T)` / `alignof(T)` outer parens (just the inner type expressions)

**Type aliases:**
```
CString = Ptr(CChar)  →  CString = Ptr[CChar]
```

### 3b. Manual conversion: `T: Type` pattern

The `T: Type` pattern (30 occurrences in 7 files) requires manual conversion because
it changes function signatures:

```tl
// Before
empty(T: Type) -> Array(T) { Array(v: Ptr(T) = null, ...) }
arr := empty(Int)

// After
empty[T]() -> Array[T] { Array(v: Ptr[T] = null, ...) }
arr := empty[Int]()
```

This changes arity, which affects function pointer references (`empty/1` → `empty/0`)
and all call sites. Handle these 7 files manually:

1. `src/tl/std/Array.tl` (11 occurrences — highest priority)
2. `src/tl/std/Array-tutorial.tl` (8 occurrences)
3. `src/tess/tl/test_type_arguments_annotations.tl`
4. `src/tess/tl/test_type_argument_field_annotation.tl`
5. `src/tess/tl/test_type_predicate_type_arg.tl`
6. `src/tess/tl/test_lambda_immediate_type_argument.tl`
7. `src/tess/tl/syntax_exploration.tl`

### 3c. Run migration

1. Run script on all `src/tl/std/*.tl` (stdlib)
2. Run script on `src/tess/embed/prelude.tl`
3. Run script on all `src/tess/tl/test_*.tl` (tests)
4. Run script on `src/tl/examples/*.tl`
5. Manual conversion of `T: Type` files
6. `make -j test` — verify all tests pass

### 3d. Review and fix edge cases

Manually review the diff for:
- Struct constructors incorrectly converted
- Tagged union constructors incorrectly converted
- `sizeof`/`alignof` with complex type expressions
- Nested generics (`Ptr[Ptr[Int]]`)

---

## Phase 4: Remove Old Syntax

**Goal:** The parser no longer accepts `()` for type arguments. Parentheses are only
for value arguments, struct constructors, and tagged union constructors.

### 4a. Parser cleanup

**Files:** `parser.c`

- `a_type_constructor()`: Remove `()` path for type constructor arguments
- Type definition parsing: Only accept `[params]`, not `(params)`
- Type annotation parsing: Only accept `Name[args]` for parameterized types

### 4b. Type parser cleanup

**Files:** `type.c`

- `tl_type_registry_parse_type_()`: Remove fallback to `arguments` for type
  constructor args; only use `type_arguments`
- Remove any dual-syntax handling

### 4c. Error messages

Add clear error messages when old syntax is detected:
```
error: use square brackets for type arguments: Ptr[T] instead of Ptr(T)
```

**Verification:** Ensure no test uses old syntax. `make -j test` passes.

---

## Phase 5: Required Type Parameter Declarations

**Goal:** All generic functions must explicitly declare their type parameters with
`[T, U, ...]`. Implicit type variable creation is removed.

### 5a. Complete `load_type_arguments` / `assign_type_arguments`

**Files:** `infer.c`

- `load_type_arguments`: Already works for basic cases. Ensure it handles:
  - Multiple type parameters: `foo[T, U](x: T, y: U)`
  - Type parameters used in complex annotations: `foo[T](x: Ptr[T])`
  - Type parameters in return types: `foo[T](x: T) -> Array[T]`

- `assign_type_arguments`: Already works for basic cases. Ensure it handles:
  - Partial type argument application (if supported)
  - Type argument count mismatch errors
  - Complex type expressions as arguments: `foo[Ptr[Int]](x)`

### 5b. Remove `process_annotation` type argument merging

**Files:** `infer.c`

The commented-out `map_merge(&ctx->type_arguments, result.type_arguments)` in
`process_annotation()` (line ~748) can be removed. With v2, type args are pre-loaded
by `load_type_arguments` before annotations are processed.

Also remove the `add_to_lexicals` block that follows — type args should already be
in scope.

### 5c. Remove `type_variable_sugar` auto-creation

**Files:** `type.c`

Currently, when an unknown symbol appears in a type annotation (e.g., `x: T` where
`T` isn't a known type), `type_variable_sugar()` creates a fresh type variable. This
is the implicit generics mechanism.

With required declarations, unknown symbols in annotations should produce an error
unless they match a declared type parameter in the current `type_arguments` context.

**Transition:** Keep `type_variable_sugar` working for types that ARE in
`ctx->type_arguments` (i.e., declared type params). Error for truly unknown types.

### 5d. Remove `T: Type` support

**Files:** `infer.c`, `type.c`

- The pattern of passing types as `Type`-typed value arguments is no longer needed
- `Type` as a type literal may be kept for backward compatibility or removed entirely
- `_tl_sizeof_` and `_tl_alignof_` intrinsics may need updates since they currently
  take `Type`-typed value arguments

### 5e. Update monomorphization

**Files:** `infer.c` (monomorphization / specialization code)

Explicit type arguments should feed directly into monomorphization. When
`foo[Int](0)` is called, the specializer knows `T=Int` from the explicit type arg,
not just from unification.

Verify that:
- Monomorphized function names include type arguments
- Multiple instantiations (`foo[Int]`, `foo[Float]`) produce distinct specializations
- Type arguments propagate through nested generic calls

### 5f. Migrate remaining generic functions

After removing implicit type variable creation, any generic function that doesn't
have `[T]` declarations will fail to compile. Grep for functions with lowercase
single-letter type annotations that lack `[]` declarations and add them.

**Verification:** Full test suite. Write new tests specifically for:
- Explicit type args with complex types
- Type arg count mismatch errors
- Missing type parameter declaration errors
- Type predicates with explicit type args (already tested)

---

## Phase 6: Cleanup and Documentation

### 6a. Remove FIXMEs

Address all v2-related FIXMEs:
- `infer.c:748` — `process_annotation` merge (Phase 5b)
- `infer.c:751` — `add_to_lexicals` block
- `type.c:621` — `type_variable_sugar` behavior
- `parser.c:3042` — `annotation_uses_type_param` type_arguments check

### 6b. Update documentation

- Update `docs/LANGUAGE_REFERENCE.md` with new syntax throughout
- Update any examples in `docs/` directory
- Update `docs/plans/` — mark this plan as complete

### 6c. Update `syntax_exploration.tl`

This file discusses the `T: Type` pattern. Update to reflect the new design.

### 6d. Delete scratch files

Remove `test.tl` from repo root and any other temporary files created during
development.

---

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Migration script misses edge cases | High | Manual review of full diff; run tests after each batch |
| Disambiguation bugs (indexing vs type args) | Medium | Dedicated test file with both patterns; parser context flag |
| `sizeof(Ptr[Void])` parsing interaction | Medium | Test sizeof/alignof with complex type expressions early |
| Breaking monomorphization | Medium | Existing monomorphization tests catch regressions |
| `CArray[T, N]` mixed type/value args | Low | CArray is already special-cased; extend special case for `[]` |
| Implicit-to-explicit type param migration | High | Many functions need `[T]` added; grep + batch fix |

## Execution Order

Phases 1 and 2 can be interleaved (parser change + type system change for each
position). Phase 3 (migration) must happen after Phases 1-2 are complete for all
positions. Phase 4 (remove old syntax) immediately follows Phase 3. Phase 5
(required declarations) is the final semantic change. Phase 6 is cleanup.

Estimated touch points: `parser.c`, `type.c`, `infer.c`, `ast.h`, `ast.c`,
`transpile.c`, plus ~80 .tl files for migration.

---

## Appendix A: Key Code Locations

All paths relative to repo root. Line numbers as of commit `a9fec778`.

### AST — `src/tess/include/ast.h`

| Line | Symbol | Notes |
|------|--------|-------|
| 77 | `struct ast_let` | Function definition node |
| 80-81 | `.type_parameters` / `.n_type_parameters` | NEW: `[T, U]` on function defs |
| 94 | `struct ast_named_application` | Function call / type constructor node |
| 97-98 | `.type_arguments` / `.n_type_arguments` | NEW: `[Int]` on function calls |
| 158 | `struct ast_user_type_def` | Struct / union / tagged union / enum definition |
| 160-165 | `.type_arguments` / `.n_type_arguments` | Pre-existing: `(a)` on type defs |
| 249 | `ast_node_create_let(alloc, name, params, type_params, body)` | Constructor; `type_params` is `ast_node_sized` |
| 251 | `ast_node_create_nfa(alloc, name, args, type_args)` | Constructor; `type_args` is `ast_node_sized` |
| 252 | `ast_node_create_nfa_tc(alloc, name, args, type_args)` | Type constructor variant of NFA |

### Parser — `src/tess/src/parser.c`

| Line | Function | Role |
|------|----------|------|
| 629 | `a_open_square()` | NEW: tokenize `[` |
| 643 | `a_close_square()` | NEW: tokenize `]` |
| 1138 | `a_type_identifier()` | Parses type names; delegates to `a_funcall` for `Name(args)`. **Key change point**: currently `Name(a)` produces NFA with args in `arguments`; needs to handle `Name[a]` putting args in `type_arguments` |
| 1176 | `a_type_annotation()` | Parses `: Type` annotations; calls `a_type_identifier` |
| 1217 | `a_funcall()` | Parses function calls; **already handles** `name[T, U](args)` putting type args in `type_arguments` |
| 1270 | `a_type_constructor()` | Parses type constructors in annotations; calls `a_funcall`. Needs `[]` support |
| 2411 | `toplevel_defun()` | Parses function definitions; **already handles** `name[T, U](params) { body }` |
| 2668 | `toplevel_type_alias()` | Parses `Foo = Bar(Int)`. RHS uses `a_type_identifier` |
| 2879 | `toplevel_struct()` | Parses struct defs. Extracts type args from `type_ident` NFA (lines 2898-2901). **Key change point**: currently reads from `named_application.arguments` |
| 2975 | `toplevel_union()` | Parses union defs. Same pattern as `toplevel_struct` (lines 3005-3008) |
| 3031 | `annotation_uses_type_param()` | Checks if annotation references a type param. **FIXME at 3042**: needs to also check `type_arguments` |
| 3098 | `create_struct_utd()` | Helper: creates UTD node from parsed fields + type args |
| 3486 | `toplevel_tagged_union()` | Parses tagged union defs. Extracts type args from `type_ident` NFA (lines 3505-3508). **Key change point**: same pattern as `toplevel_struct` |

**Parser flow for type definitions:** `a_type_identifier()` → tries `a_funcall()` first → if it matches `Name(args)`, returns NFA with type args in `arguments`. Then `toplevel_struct`/`toplevel_tagged_union` extract from `named_application.arguments`. With new syntax, `a_type_identifier` needs to produce NFA with args in `type_arguments` when `[]` is used.

### Type System — `src/tess/src/type.c`

| Line | Function | Role |
|------|----------|------|
| 74 | `make_unary_tc(self, S("Ptr"))` | Registers `Ptr` as unary type constructor |
| 77 | `make_unary_tc(self, S("Const"))` | Registers `Const` as unary type constructor |
| 201 | `make_unary_tc()` | Creates a unary type constructor in the registry |
| 321 | `tl_type_registry_instantiate_carray()` | Special CArray instantiation (type + int count) |
| 350 | `tl_type_registry_is_unary_type()` | Checks if name is a unary type constructor |
| 502 | `tl_type_registry_add_type_argument()` | NEW: adds named type arg to context map |
| 508 | `tl_type_registry_add_fresh_type_argument()` | NEW: creates fresh literal + adds to map |
| 515 | `add_type_argument()` | NEW: local helper wrapping the above |
| 522 | `is_type_argument()` | NEW: checks if name is a declared type arg |
| 526 | `get_type_argument()` | NEW: retrieves type for a declared type arg |
| 530 | `parse_type_specials()` | Handles special type names (e.g., `any`); uses `add_type_argument` |
| 550 | `type_variable_sugar()` | Auto-creates type variables for unknown symbols. **FIXME at 621**: should not auto-create with v2 type args |
| 577 | `tl_type_registry_parse_type_()` | **Main type parser**. Receives NFA nodes, dispatches based on name |
| 645 | (in `parse_type_`) | Unary type check: `tl_type_registry_is_unary_type` branch — handles `Ptr(T)`, `Const(T)` |
| 733 | (in `parse_type_`) | Union special case |
| 739-741 | (in `parse_type_`) | CArray special case: `tl_type_registry_instantiate_carray` |
| 875 | `tl_type_registry_parse_type_ctx_init()` | Initializes parse context (type_arguments map, etc.) |

**Type parser flow for `Ptr(T)`:** `parse_type_()` receives NFA node → checks `is_unary_type("Ptr")` → reads single arg from `node->named_application.arguments[0]` → recursively parses arg → instantiates. **With new syntax:** must read from `type_arguments` instead.

### Inference — `src/tess/src/infer.c`

| Line | Function | Role |
|------|----------|------|
| 577 | `traverse_ctx_load_type_arguments()` | NEW: at start of `ast_let`, creates fresh type literals for each declared `[T, U]` type parameter |
| 588 | `traverse_ctx_assign_type_arguments()` | NEW: at `ast_nfa`, matches call-site `[Int]` to definition's `[T]` by position |
| 738 | `process_annotation()` | Processes type annotations. **FIXME at 748**: `map_merge` of type args commented out (v2 pre-loads them). **FIXME at 751**: `add_to_lexicals` block may be removable |
| 1557 | (in traversal) | Where `load_type_arguments` is called for `ast_let` |
| 1607 | (in traversal) | Where `assign_type_arguments` is called for `ast_nfa` |
| 2268 | `check_type_predicate()` | Handles `T :: Int` and `x :: T` — already updated for type args |
| 3236 | `rename_variables()` | Variable renaming pass; line 3436 recurses into type predicate RHS for type arg references |

### Transpiler — `src/tess/src/transpile.c`

Minimal changes needed. One call site updated to pass `(ast_node_sized){0}` for the new `type_args` parameter on `ast_node_create_nfa`. The transpiler works with resolved types, so syntax changes are mostly transparent.

### FIXME Locations (v2 type args)

| File | Line | Description |
|------|------|-------------|
| `parser.c` | 3042 | `annotation_uses_type_param`: should also check `node->named_application.type_arguments` |
| `infer.c` | 748 | `process_annotation`: `map_merge` of type args commented out |
| `infer.c` | 751 | `process_annotation`: `add_to_lexicals` block possibly removable |
| `type.c` | 621 | `type_variable_sugar`: should not auto-create with v2 type args |

---

## Appendix B: Files Requiring `T: Type` Manual Conversion

These files use the `T: Type` value-parameter pattern that must be manually converted
to `[T]` type parameter syntax. This changes function arity and all call sites.

| File | Occurrences | Functions |
|------|-------------|-----------|
| `src/tl/std/Array.tl` | 11 | `empty`, `with_capacity` (2 overloads), `_type_width_aligned`, `_ensure_capacity`, `push`, `pop`, `last`, `iter_init`, `iter_value`, `iter_ptr` |
| `src/tl/std/Array-tutorial.tl` | 8 | Same functions (tutorial copy) |
| `src/tess/tl/test_type_arguments_annotations.tl` | 1 | `array_new` |
| `src/tess/tl/test_type_argument_field_annotation.tl` | 1 | `empty` |
| `src/tess/tl/test_type_predicate_type_arg.tl` | 3 | `is_int_type`, `matches_param`, `combined` |
| `src/tess/tl/test_lambda_immediate_type_argument.tl` | 1 | Lambda: `(x: Type) { sizeof(x) } (Int)` |
| `src/tess/tl/syntax_exploration.tl` | 1 | `array_new` |

---

## Appendix C: Design Decisions (for context recovery)

These decisions were made during planning and should be preserved:

1. **Go-style square brackets** for all type arguments — no exceptions
2. **No backward compatibility** — old `()` syntax will be removed after migration
3. **Context-based disambiguation** — parser uses position context, not lookahead heuristics
4. **Script-assisted migration** — Python script for bulk conversion, manual for `T: Type`
5. **`sizeof(T)` / `alignof(T)` keep parentheses** — they are built-in operators, not type constructors; but inner type expressions use `[]`: `sizeof(Ptr[Void])`
6. **Struct constructors unchanged** — `Point(x = 1, y = 2)` keeps `()` (value args)
7. **Tagged union value constructors unchanged** — `Some(42)` keeps `()` (value args)
8. **Type definitions use `[]`** — `Point[a] : { ... }`, `Option[T] : | ...`
9. **All built-in type constructors use `[]`** — `Ptr[T]`, `CArray[T, N]`, `Const[T]`, `Array[T]`
