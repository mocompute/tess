# Refactoring Plan — Incremental Technical Debt Reduction

## Motivation

Over recent development, the compiler has grown significantly in capability: integer type
cleanup (7 phases), explicit type arguments, package.tl DSL, tagged union improvements,
and more. This rapid feature work has left pockets of special-casing, code duplication,
and monolithic files that make the codebase harder to navigate and maintain.

The test suite (293 integration tests, 72 rejection tests, 17 C unit test files) provides
a strong safety net. This plan takes an incremental approach: one refactoring session at a
time, with full test runs between each.

---

## Current State

### File Sizes (top 6 compiler source files)

| File | Lines | Role |
|------|-------|------|
| infer.c | 6564 | Type inference, specialization, AST traversal |
| parser.c | 4330 | Parsing, AST construction |
| type.c | 3514 | Type system, unification, integer sub-chains |
| transpile.c | 3513 | C code generation |
| tess_exe.c | 2345 | CLI driver |
| ast.c | 1740 | AST node utilities |

### Markers of Accumulated Debt

- **35 FIXMEs** across compiler sources (14 in infer.c, 5 in parser.c and transpile.c each)
- **8 TODOs** with specific improvement suggestions
- **3 HACKs** in tokenizer and sexp_parser
- **2 forward declarations** of `specialize_type_constructor` in infer.c (lines 794 and
  2743) because the file is too large for natural top-down ordering
- **3 self-documented duplication sites** in parser.c (lines 2242, 2708, 3382)

---

## Cross-Cutting Special Cases

A deeper audit of the codebase reveals that much of the complexity comes not from
individual files being too large, but from **cross-cutting concerns** — the same logical
concept manifesting as special-case branches in multiple files. These are the real sources
of fragility: changing one aspect of a feature requires finding and updating every file
where that feature has a special case.

### 1. The `main()` Function (4 files, 5+ special cases)

The `main` function gets special treatment in nearly every compiler phase:

| Location | Special Case |
|----------|-------------|
| parser.c:2159 | Never arity-mangled (`mangle_name_for_arity` skips it) |
| infer.c:3387 | Forced to return CInt regardless of annotation |
| infer.c:5387 | Excluded from generic type analysis |
| infer.c:5511 | Excluded from generic toplevel removal |
| transpile.c:2732 | Always generated even if it has a generic type |

**Observation:** These are five independent `str_eq(name, S("main"))` checks, each
encoding a different aspect of "main is special." A `is_main_function` predicate exists
nowhere — each site reinvents the check.

### 2. C Interop Prefix Conventions (4 files, 10+ special cases)

Three naming prefixes (`c_`, `c_struct_`, `_tl_`) trigger special behavior:

| Prefix | Where Checked | Effect |
|--------|--------------|--------|
| `c_` | infer.c:6489, 4620, 4778 | Skipped during alpha-renaming |
| `c_` | transpile.c:564, 1016 | Emitted literally (prefix stripped), no type info used |
| `c_` | parser.c:906 | Allowed through `__` identifier restriction |
| `c_struct_` | transpile.c:2856 | Rendered with `struct` keyword prefix in C |
| `_tl_` | infer.c:4471 | Skipped during specialization (intrinsics) |
| `_tl_` | transpile.c:3070-3174 | Dispatched to intrinsic handlers (sizeof, alignof, fatal) |

**Observation:** The `is_c_symbol()`, `is_c_struct_symbol()`, and `is_intrinsic()` helpers
exist but are defined locally in infer.c. transpile.c has its own copies of the same
string prefix checks. There's no single place that documents what these prefixes mean.

### 3. Tagged Union Mechanics (3 files, 8+ special cases)

Tagged unions thread special-case logic through parsing, inference, and codegen:

| Location | Special Case |
|----------|-------------|
| parser.c:1654-1671 | Case expression parsing diverges for union types vs predicates |
| parser.c:1689-1691 | Mutable (`&`) vs value union access sets different flags |
| infer.c:1744-1780 | `is_union` flag triggers completely different inference path |
| infer.c:1780 | `AST_TAGGED_UNION_MUTABLE` wraps condition type in Ptr |
| infer.c:1998 | Union structs skip argument count validation |
| infer.c:3851 | `is_union_struct()` helper exists only for this check |
| transpile.c:1533-1651 | Separate codegen path with hardcoded "tag" and "u" field names |
| transpile.c:1686 | `is_union` flag diverts to special case handler |

**Observation:** The "tag" and "u" field names are magic strings in transpile.c that must
match whatever the type system generates. The mutable vs value distinction threads through
all three phases as separate flags.

### 4. Cast Annotations (2 files, 5+ special cases)

The `is_cast` / `is_cast_annotation` mechanism creates forking paths through inference:

| Location | Special Case |
|----------|-------------|
| infer.c:1861 | Cast flag detected, suppresses unification errors |
| infer.c:1878 | Sub-flag: `is_integer_cast` for integer-specific handling |
| infer.c:1909 | Weak int + cast annotation: constrain to annotation type |
| infer.c:2022 | Cast annotation in struct field: permissive constraint |
| infer.c:3924 | Cast annotation in specialize_user_type: use annotation type |

**Observation:** Cast annotations create a parallel inference path. The `is_cast` flag
alters constraint generation, error suppression, and specialization — a single syntactic
feature with tendrils in five places within one file.

### 5. Const Type Wrapper (3 files, 5+ special cases)

`Const(T)` is transparent in some contexts and meaningful in others:

| Location | Special Case |
|----------|-------------|
| type.c:2825 | Stripped before unification |
| infer.c:2980 | Stripped before struct field access |
| infer.c:1511 | Checked on pointer dereference for const-assignment error |
| transpile.c:2827-2832 | Only meaningful inside Ptr (`Ptr(Const(T))` → `const T*`) |
| transpile.c:2352 | Skipped when looking for Arrow inside pointers |

**Observation:** Const is "usually transparent, except when it's not." Each consumer
decides independently whether to strip it.

### 6. Weak Integer Literals (3 files, 10+ special cases)

Weak integers create alternative paths through inference, unification, and codegen:

| Location | Special Case |
|----------|-------------|
| type.c:3112-3128 | Family compatibility check (signed/unsigned) |
| type.c:3125-3128 | Standalone types (CSize, CChar) rejected |
| type.c:3130-3140 | Compile-time bounds checking on literal values |
| type.c:3145-3150 | Resolved weak int: recursive unification |
| type.c:3185-3192 | Two weak ints: same-family merge vs cross-family error |
| type.c:3238-3242 | TV → weak int chain following |
| type.c:3248-3250 | Two narrow integers force EXACT mode |
| type.c:3363-3372 | Defaulting sets both var root and TV root |
| infer.c:1909 | Weak int + annotation: special constraint |
| infer.c:4065 | Post-specialize: second round of defaulting |
| transpile.c:1138-1200 | Narrowing detection and bounds-check emission |

**Observation:** This is the most pervasive special-case system. Weak ints affect
unification (6 branches in type.c), inference (2 in infer.c), and codegen (1 in
transpile.c). The double-defaulting requirement (before and after specialization) is
particularly subtle.

### 7. Pointer Types (3 files, 8+ special cases)

Pointers get special treatment in many contexts beyond just type checking:

| Location | Special Case |
|----------|-------------|
| type.c:2983-2988 | Occurs check allows Ptr self-reference |
| type.c:2354 | Hash cycle detection: special Ptr target handling |
| infer.c:2941 | Arrow (`->`) operator requires Ptr unwrap |
| infer.c:3035 | CArray field type decays to pointer |
| transpile.c:1273 | Pointer assignment: emit explicit cast |
| transpile.c:1886 | CArray field: generate as lvalue to prevent decay |
| transpile.c:1961 | Pointer comparison: cast both sides to `void*` |
| transpile.c:2820-2907 | Ptr-to-Arrow rendering: function pointer C syntax |

**Observation:** Pointer handling is spread across all three backend files. The CArray
decay rules alone require special cases in both infer.c and transpile.c.

### 8. Module Name Mangling (2 files, 4+ special cases)

| Location | Special Case |
|----------|-------------|
| parser.c:287 | `builtin` module: names NOT mangled |
| parser.c:2197 | Empty/main module: names NOT mangled |
| parser.c:1291 | Arity mangling MUST happen before module mangling |
| transpile.c:2712-2720 | `tl_` and `std_` prefixed names: mangling skipped |

### 9. Type-to-C Name Mapping (transpile.c, 27+ entries)

`type_to_c()` contains an exhaustive `str_eq` chain mapping Tess type names to C type
strings. This is the single largest special-case block in the codebase:

```
Int → long long, UInt → unsigned long long, CInt → int,
CChar → char, Bool → /*bool*/int, Float → double,
CArray(T,N) → T* (decay), Ptr(Const(T)) → const T*, ...
```

**Observation:** This is a hand-maintained lookup table embedded in control flow. Adding a
new built-in type requires finding and updating this chain. A data-driven approach (table
of `{name, c_name}` pairs) would be less error-prone.

### 10. Node Position / Context Mode Dispatch (infer.c)

The `node_position` enum (8 values) controls type resolution behavior:

```
npos_toplevel, npos_formal_parameter, npos_function_argument,
npos_value_rhs, npos_assign_lhs, npos_reassign_lhs,
npos_operand, npos_field_name
```

`resolve_node()` at line 3105 dispatches on this enum with qualitatively different logic
per position. Several other functions also branch on node position.

**Observation:** This is arguably the correct design (different positions *do* have
different semantics), but the 8-way dispatch in a single function makes it hard to
understand any one case in isolation.

---

## Refactoring Sessions

Each session is designed to be completable independently, with `make -j test` validating
the result. Sessions are ordered by a combination of value and safety — early sessions
build confidence with lower-risk changes before tackling the larger structural work.

### Session 1: Parser Deduplication

**Target:** parser.c
**Risk:** Low
**Goal:** Address the three self-documented duplication TODOs.

1. **Line 2242 — Merge reassignment cases.** `a_reassignment()` has two near-identical
   branches for `=` vs compound assignment operators (`+=`, etc.). The structure
   (parse lvalue, parse expression, create node) is shared; only the final node
   constructor differs.

2. **Line 3382 — Combine `toplevel_union` with `toplevel_struct`.** Both parse a type
   name (with optional type parameters), a field list in braces, then construct an
   `ast_user_type_definition` node. The only difference is `is_union = 1` and the
   `|`-separated field syntax. A shared helper could handle the common tail.

3. **Line 2708 — Extract shared parameter-parsing logic from `toplevel_defun` and
   `a_type_arrow`.** Both parse type parameter lists in `[]` and value parameter lists
   in `()`. The shared prefix (type params, value params, return type annotation)
   could become a helper that returns a param descriptor.

**Validation:** `make -j test` — all parser changes are exercised by existing integration
tests.

---

### Session 2: Silence the Safe FIXMEs

**Target:** infer.c, transpile.c, type.c, parser.c
**Risk:** Low
**Goal:** Resolve FIXMEs that have clear, safe fixes — primarily missing error messages,
stale comments, and dead code questions.

Candidates (representative, not exhaustive):

| Location | FIXME | Resolution |
|----------|-------|------------|
| infer.c:492 | "assumes alias name is a symbol" | Add nfa handling or assert |
| infer.c:882 | "could not parse type — better error" | Emit proper diagnostic |
| infer.c:1166-1189 | "v2 type arguments may not be needed" | Verify and remove dead code |
| infer.c:4197, 4300 | "ignores error" | Propagate error or emit diagnostic |
| infer.c:6201 | "ignores specialize_arrow error" | Propagate error |
| transpile.c:2714 | "do we still use std_?" | Check and remove if dead |
| transpile.c:1123 | "file/line not accurate" | Fix source location tracking |
| type.c:1934 | "name of this function is misleading" | Rename |

**Validation:** `make -j test` plus `make CONFIG=debug -j test`.

---

### Session 3: Split infer.c — Extract Specialization

**Target:** infer.c → infer.c + specialize.c
**Risk:** Moderate
**Goal:** Extract the specialization pipeline into its own file.

The specialization code is the most naturally separable subsystem in infer.c. It has a
clear entry point (`post_specialize` and related functions) and a relatively well-defined
interface with the rest of inference (it reads from the type environment and instance
cache, and writes specialized toplevels).

**Scope of extraction:**

- `specialize_type_constructor_()` and `specialize_type_constructor()` (lines ~3612-3849)
- `specialize_arrow()` and supporting functions
- `specialize_applications_cb()`, `specialize_case()`, `specialize_let_in()`, etc.
- `post_specialize()` and its helpers
- `type_literal_specialize()`
- Instance cache management functions

**Shared state interface:** The extracted code needs access to `tl_infer` context (type
registry, toplevels hashmap, instance cache, arenas). This is already bundled in the
`tl_infer` struct, so the interface is clean — functions just take `tl_infer *self`.

**Steps:**

1. Identify all specialize-related functions and their dependency graph
2. Identify any static helpers shared between specialization and inference proper
3. Move shared helpers to infer.h or a new internal header (infer_internal.h)
4. Move specialization functions to specialize.c
5. Remove forward declarations that were only needed due to file size
6. Update both Makefile and CMakeLists.txt

**Validation:** `make -j test`, `make CONFIG=debug -j test`, `make CONFIG=asan -j test`.

---

### Session 4: Split infer.c — Extract Traversal

**Target:** infer.c → infer.c + infer_traverse.c
**Risk:** Moderate
**Goal:** Extract the AST traversal/visitor callbacks into their own file.

After Session 3 removes specialization, the remaining infer.c should be around 4000-4500
lines. The traversal callbacks (`infer_traverse_cb`, `infer_named_function_application`,
`infer_let_in`, `infer_case`, etc.) form another natural module.

**Steps:** Same pattern as Session 3 — identify functions, shared helpers, move, update
build files.

**Post-state:** infer.c should be under 2500 lines, containing core constraint generation
and unification orchestration.

**Validation:** Full test suite in all three configurations.

---

### Session 5: Extract Integer Type Subsystem

**Target:** type.c → type.c + type_integers.c
**Risk:** Moderate
**Goal:** Pull the integer sub-chain logic, weak int handling, narrowness checks, and
integer family comparison into a dedicated file.

The integer type system is a well-defined subsystem with clear boundaries:
- Sub-chain definitions and width ordering
- `is_narrow_integer` classification
- Weak int creation, defaulting, and substitution
- Cross-family unification error detection
- Integer type comparison and widening checks

This extraction will make the integer type logic easier to find and reason about,
especially as directional unification work continues.

**Validation:** Full test suite. The integer tests (`test_integers.tl`, `test_weak_int.tl`,
etc.) specifically exercise this subsystem.

---

### Session 6: Parser Structural Cleanup

**Target:** parser.c
**Risk:** Low-Moderate
**Goal:** Address remaining parser FIXMEs and improve structural organization.

Candidates:

- **Line 918:** Arity-qualified names accepted in places they shouldn't be — add
  validation or restrict parsing context.
- **Line 1164:** Ellipsis accepted via recursion where it shouldn't be — add guard.
- **Line 1448:** Tokens not yet supported by tokenizer — either implement or emit clear
  error.
- **Line 1629:** Feature not yet implemented — implement or emit clear error.
- **Line 3054:** Funcalls as alias names — decide whether to support and either implement
  properly or reject.

**Validation:** `make -j test` — rejection tests (`test_fail_*.tl`) specifically cover
parser error cases.

---

### Session 7: Transpile Cleanup

**Target:** transpile.c
**Risk:** Low
**Goal:** Address transpile.c FIXMEs and reduce string-building fragility.

- Remove dead code (`std_` prefix check at line 2714 if confirmed unused)
- Fix source location tracking (line 1123)
- Replace `FIXME_generate_expr` placeholder (lines 2324-2325) with proper error
- Add missing error reporting (line 3166)
- Consider extracting type-to-C conversion helpers into a focused helper section

**Validation:** Full test suite. Generated C code correctness is validated by the
integration tests actually compiling and running the output.

---

### Session 8: Centralize C Interop Prefix Handling

**Target:** infer.c, transpile.c, parser.c
**Risk:** Low
**Goal:** Consolidate the scattered `c_`, `c_struct_`, `_tl_` prefix checks.

Currently `is_c_symbol()`, `is_c_struct_symbol()`, and `is_intrinsic()` are defined as
static helpers in infer.c, while transpile.c has its own inline `str_cmp_nc` checks for
the same prefixes. parser.c has yet another check for `c__` in identifier validation.

1. Move prefix predicates to a shared header (e.g., `names.h` or `conventions.h`)
2. Replace all inline `str_cmp_nc(name, "c_", 2)` checks with the named predicates
3. Add a comment block documenting what each prefix means and where it's used

**Validation:** `make -j test` — purely mechanical, no behavior change.

---

### Session 9: Data-Driven Type-to-C Mapping ✓ DONE

**Target:** transpile.c, type.c, type.h
**Commit:** acc7defa (wip-refactor)

Extended `tl_type_constructor_def` with `c_type_name`, `c_min_macro`, `c_max_macro`,
`integer_min_value`, `integer_max_value`, and `has_integer_range` fields. Populated them
from the `builtin_nullary[]` table. Replaced ~104 `str_eq` calls across 5 functions
(`integer_value_fits`, `integer_c_min`, `integer_c_max`, `type_to_c`, `is_c_exportable_type`)
with field accesses. Net -102 lines. All 310 tests pass (release, debug, ASAN).

---

### Session 10: CArray Helpers and Ptr Deduplication ✓ DONE

**Target:** type.h, type.c, infer.c, transpile.c
**Branch:** wip-refactor

Added `tl_monotype_is_carray`, `tl_monotype_carray_element`, `tl_monotype_carray_count`
helpers (matching the existing Ptr/Const pattern). Replaced 5 inline
`tl_monotype_is_inst_of(x, S("CArray"))` + raw `cons_inst->args.v[0]` accesses with
the new helpers (3 in infer.c, 2 in transpile.c, plus 1 `is_carray`-only check).
Extracted `render_ptr_to_c()` to deduplicate two identical 15-line Ptr rendering blocks
in `type_to_c()`. Removed dead `tl_monotype_is_string` declaration from type.h. Added
doc comment for the Ptr/Const/CArray helper group. All 310 tests pass (release, debug,
ASAN).

---

### Session 11: Consolidate `main()` Special Cases

**Target:** parser.c, infer.c, transpile.c
**Risk:** Low
**Goal:** Make the "main is special" logic discoverable and consistent.

1. Add `is_main_function()` predicate alongside the C interop predicates (Session 8)
2. Replace all `str_eq(name, S("main"))` checks with the predicate
3. Add a comment at the predicate documenting all the ways main is special:
   - Never arity-mangled
   - Forced CInt return type
   - Excluded from generic analysis and removal
   - Always generated in codegen

**Validation:** `make -j test` — no behavior change.

---

### Session 12: Reduce Tagged Union Special-Casing ✓ DONE

**Target:** ast.h, parser.c, infer.c, transpile.c
**Branch:** wip-refactor

Added `AST_TAGGED_UNION_TAG_FIELD`/`AST_TAGGED_UNION_UNION_FIELD` constants to ast.h,
replacing 11 magic string sites across parser.c (4), infer.c (4), and transpile.c (3).
Extracted `tagged_union_variant_poly()` in infer.c to deduplicate Ptr-wrapping for
mutable union bindings (2 sites). Extracted `tagged_union_wrapper_fields()` in
transpile.c to deduplicate field-lookup loops (2 sites). Fixed `is_union_struct()` static
inconsistency (made definition static, removed redundant forward declaration). Reviewed
`is_union` struct promotion and mutable/value uniformity — concluded current design is
already well-factored after these changes. All 310 tests pass (release, debug, ASAN).

---

### Future Sessions (not yet scoped)

These are larger architectural changes that may be worth considering after the above
cleanup is complete:

- **Internal header for infer subsystem.** After Sessions 3-4, the split files will need
  shared declarations. An `infer_internal.h` (not installed, not part of public API)
  would be cleaner than putting everything in `infer.h`.

- **Intermediate representation before codegen.** Currently the compiler goes AST →
  C strings. An IR layer would reduce string-building complexity in transpile.c and
  enable future optimizations. This is a large architectural change and should be its
  own design document.

- **Consolidate hot-path context reuse pattern.** The `hot_parse_ctx` reentrancy guards
  in infer.c are a maintenance hazard. After splitting the file, evaluate whether the
  optimization is still necessary or whether a cleaner approach is feasible.

- **Const transparency rules.** Const is stripped in some contexts (unification, struct
  access) but meaningful in others (Ptr wrapping, reassignment checks). A clearer model
  — perhaps Const stripping as an explicit phase or a `strip_const_if_transparent()`
  helper — would reduce the five independent "should I strip Const here?" decisions.

- **Cast annotation refactoring.** The `is_cast` flag creates five forking paths through
  inference in infer.c. Consider whether cast annotations could be handled as a
  transformation pass (rewriting the AST before inference) rather than as a flag that
  modifies inference behavior inline.

---

## Principles

1. **One session, one concern.** Each session addresses a single category of debt.
   Don't mix file splitting with bug fixes.

2. **Test between every session.** Run `make -j test` at minimum. For structural changes
   (Sessions 3-5), also run debug and ASAN configurations.

3. **Keep both build systems in sync.** Any new .c file must be added to both Makefile
   and CMakeLists.txt.

4. **No behavior changes.** These are pure refactorings. If a FIXME resolution would
   change observable behavior (e.g., emitting an error where none was emitted before),
   that's fine — but note it explicitly and verify with tests.

5. **Commit after each session.** Each session should produce a clean, reviewable commit
   (or small series of commits).
