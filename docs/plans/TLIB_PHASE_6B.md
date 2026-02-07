# Phase 6b: Module Discovery Integration with Pack — Implementation Plan

**Parent document:** [TLIB_LIBRARIES.md](TLIB_LIBRARIES.md) (Phase 6b section)

**Status:** Planning

---

## Overview

Phase 6b connects the module discovery scanning (completed in Phase 6) with the pack command to provide validation and early feedback. The work is split into four sub-phases, each independently testable.

### Goals

1. Validate that manifest's `modules` list matches actually-discovered modules in source
2. Scan source files for `[[export]]` annotations and warn about mismatches
3. Verify the archive is self-contained (every quoted import resolves within the archive)

### Sub-Phase Dependencies

```
Sub-Phase 1 (Module Validation)
    |
    v
Sub-Phase 2 ([[export]] Scanning)
    |
    v
Sub-Phase 3 ([[export]] Warnings)   <-- depends on both 1 and 2
    |
    v
Sub-Phase 4 (Self-Containment)      <-- independent of 1-3, ordered last
```

Sub-Phase 4 is independent and could be done in any order, but is placed last because it involves more complex path logic in `tlib.c`.

---

## Design Decisions

These decisions were made during planning to resolve ambiguities in the parent design document.

### Internal modules: verbose-only, not warnings

The parent doc says "Warn (but don't error) if discovered modules aren't in manifest (they're internal)." However, internal modules are a normal and expected pattern (e.g., `MathUtils.Internal` in the end-to-end example). Emitting a warning for every internal module is noisy.

**Decision:** Do not warn about unused modules. Only emit actual warnings for likely mistakes (public module with no exports, non-public module with exports).

### `[[export]]` scanning: text-level: guard against false positives

For Phase 6b, `[[export]]` detection uses simple text search (`[[export]]` literal in file content). This may produce false positives from comments (`// [[export]]`) or strings (`"[[export]]"`).

**Decision:** Guard against false positives. ``[[export]]`` annotations always appear before a symbol. Using
a line parser, you can detect strings and comments and ignore them.

### Module-to-file mapping via re-scan

This is no longer necessary, as the import-scanning infrastructure has been updated so that the modules_seen
field is now a map with module keys and file path values (str).

### Self-containment check in `tl_tlib_pack()`

**Decision:** The self-containment check happens inside `tl_tlib_pack()` (in `tlib.c`) after building the entries array but before writing. This checks what's actually going into the archive with final relative paths, which is more accurate than checking filesystem paths in `pack_files()`.

---

## Sub-Phase 1: Module Discovery Validation Against Manifest

### Task

After scanning discovers modules and the manifest is parsed, cross-check the two lists. Error if the manifest declares a module that doesn't exist in source. Report internal modules only in verbose mode.

### Implementation

**Location:** `pack_files()` in `src/tess/src/tess_exe.c`, between manifest parsing (line ~1030) and calling `tl_tlib_pack()` (line ~1080).

**Logic:**

1. For each module listed in `manifest.package.modules`:
   - Check if it exists in `self->modules_seen`
   - If not found: **error** — `"error: manifest declares module '%s' but no #module directive found in source"`
   - Return 1 (pack fails)
2. For each module in `self->modules_seen`:
   - If not in `manifest.package.modules`: verbose-only info — `"  (internal) %s"` (only with `-v`)
3. If `manifest.package.module_count == 0`: no validation needed (package with no public API)

### Tests

New tests in `src/tess/src/test_tlib.c`:

- **`test_pack_module_not_in_source`** — Create source with `#module Foo`, manifest with `modules = [Bar]`. Pack must fail with error.
- **`test_pack_module_validation_ok`** — Create source with `#module Foo` and a second file with `#module Internal`, manifest with `modules = [Foo]`. Pack must succeed.
- **`test_pack_empty_modules_list`** — Create source with `#module Foo`, manifest with `modules = []`. Pack must succeed (no validation).

### Verification

- `make -j test` passes
- Manual test: `tess pack -m manifest.toml src.tl -o out.tlib` with mismatched modules shows error

---

## Sub-Phase 2: `[[export]]` Scanning

### Task

Scan each packed source file for `[[export]]` annotations and build a mapping of which modules contain exports.

### Implementation

**Location:** New function in `src/tess/src/tess_exe.c`.

**New function: `scan_module_exports()`**

```c
// Scan non-stdlib files for #module declarations and [[export]] annotations.
// Populates modules_with_exports hashmap: module_name -> 1 (present = has exports).
// Uses the same conditional compilation logic as scan_directives().
static void scan_module_exports(state *self, str_sized files, hashmap **modules_with_exports);
```

For each non-stdlib file in `files`:
1. Read file content
2. Find the `#module` directive (respecting `#ifdef`/`#ifndef` conditional compilation, same logic as existing scanner)
3. Search file content for `[[export]]` literal (simple `strstr`/byte search)
4. If found, insert module name into `modules_with_exports` hashset
5. If no `#module` found, skip (shouldn't happen for user files, but defensive)

**Called from:** `pack_files()`, after `files_in_order()` returns and before manifest validation.

### Tests

New tests in `src/tess/src/test_tlib.c`:

- **`test_pack_export_detection`** — Source: `#module Foo\n[[export]] foo() { 1 }\n`. Verify module `Foo` is detected as having exports (pack succeeds, use with Sub-Phase 3 warning tests).
- **`test_pack_no_export_detection`** — Source: `#module Foo\nfoo() { 1 }\n`. Verify module `Foo` is detected as NOT having exports.

Note: These tests are most naturally verified via the warning behavior in Sub-Phase 3. The scanning function itself is internal, so we test it indirectly through its observable effects (warnings emitted or not).

### Verification

- `make -j test` passes

---

## Sub-Phase 3: `[[export]]` Warning Integration

### Task

Use the `modules_with_exports` mapping and manifest's `modules` list to emit warnings about likely mistakes.

### Implementation

**Location:** `pack_files()` in `src/tess/src/tess_exe.c`, after Sub-Phase 2 scanning and manifest parsing.

**Logic:**

1. For each module in `manifest.package.modules` (public modules):
   - If module has no `[[export]]` symbols (not in `modules_with_exports`):
   - **warning:** `"warning: module '%s' is listed as public but has no [[export]] symbols"`
2. For each module in `modules_with_exports`:
   - If not in `manifest.package.modules`:
   - **warning:** `"warning: module '%s' has [[export]] symbols but is not listed in manifest modules"`

Warnings go to stderr. Pack still succeeds — these are advisory.

### Tests

New tests in `src/tess/src/test_tlib.c`:

- **`test_pack_warn_public_no_exports`** — Manifest `modules = [Foo]`, source `#module Foo` with no `[[export]]`. Pack succeeds (verify return code 0). Warning is emitted to stderr.
- **`test_pack_warn_exports_not_public`** — Manifest `modules = []`, source `#module Bar` with `[[export]] func() { 1 }`. Pack succeeds. Warning is emitted to stderr.
- **`test_pack_no_warnings`** — Manifest `modules = [Foo]`, source `#module Foo` with `[[export]] func() { 1 }`. Pack succeeds with no warnings.

**Test mechanics for stderr verification:** The pack function returns 0 (success) in all warning cases, so tests verify the return code. For warning text verification, we have two options:
1. Add a structured result to `tl_tlib_pack_opts` (e.g., warning count) that tests can inspect
2. Accept that warning text is verified via manual/integration testing

Option 1 is preferred if feasible without over-engineering. A simple `int *warning_count` field in the opts or a return struct would suffice.

### Verification

- `make -j test` passes
- Manual test confirms warning text on stderr

---

## Sub-Phase 4: Self-Containment Check

### Task

Verify every quoted `#import "file.tl"` in packed files resolves to another file in the archive. Error if an import escapes the archive.

### Implementation

**Location:** `tl_tlib_pack()` in `src/tess/src/tlib.c`, after building the entries array (after current line ~522) but before writing (current line ~554).

**New static function: `check_self_containment()`**

```c
// Verify all quoted imports in archive entries resolve to other entries.
// Returns 0 if self-contained, 1 if an import escapes the archive.
static int check_self_containment(allocator *alloc, tl_tlib_entry const *entries, u32 count);
```

**Algorithm:**

1. Build a hashset of entry names (relative paths in the archive)
2. For each entry:
   a. Scan content for `#import` directives using a minimal scanner (similar to `scan_directives` but only extracting imports, no conditional compilation needed since `files_in_order()` already resolved the actual imports)
   b. For each import found:
      - Determine kind: quoted (`"..."`) or angle-bracket (`<...>`)
      - **Skip** angle-bracket imports (stdlib, not in archive)
      - For quoted imports:
        - Compute the importing file's directory from its entry name
        - Join with the import path
        - Normalize (resolve `.` and `..` components)
        - Check if the result exists in the entry names hashset
   c. If not found: **error** — `"error: self-containment check failed: '%s' (imported from '%s') not found in archive"` — return 1

3. If all imports resolve, return 0

**Path resolution detail:**

For entry `liba/foo.tl` importing `"../util/util.tl"`:
- Directory: `liba/`
- Joined: `liba/../util/util.tl`
- Normalized: `util/util.tl`
- Check: is `util/util.tl` in the hashset?

**Edge case — conditional imports:** The scanner does NOT need to respect `#ifdef`/`#endif` for the self-containment check. The check should be conservative: if ANY quoted import appears in the file (even in a conditionally-excluded branch), it should resolve. This is stricter than necessary but simpler and safer. If this proves too strict in practice, it can be relaxed later.

**Design note:** We could alternatively reuse the conditional-compilation-aware scanning. But the self-containment check is about archive integrity, not compilation behavior. Being strict here is a feature: it ensures the archive works regardless of which conditional branches are active.

### Tests

New tests in `src/tess/src/test_tlib.c`:

- **`test_pack_self_contained`** — Two files: `a.tl` with `#import "b.tl"`, `b.tl` standalone. Pack both. Must succeed.
- **`test_pack_not_self_contained`** — `a.tl` with `#import "missing.tl"`. Pack only `a.tl`. Must fail with self-containment error.
- **`test_pack_stdlib_import_ok`** — `a.tl` with `#import <stdio.tl>`. Pack `a.tl`. Must succeed (stdlib imports ignored).
- **`test_pack_subdir_import`** — `lib/a.tl` with `#import "../util.tl"`, `util.tl` standalone. Pack both with correct relative paths. Must succeed.

**Note:** These tests operate at the `tl_tlib_pack()` level, building file arrays and calling pack directly (similar to existing `test_pack_with_manifest`). They do NOT need the full CLI pipeline.

### Verification

- `make -j test` passes
- Manual test: pack a file with a broken import, verify error message

---

## Build System

All new tests are added to both `Makefile` and `CMakeLists.txt` (per project rules). The test functions are added to the existing `test_tlib.c` file and registered in its `main()`.

No new source files are created — all implementation goes into existing files:
- `src/tess/src/tess_exe.c` (Sub-Phases 1-3)
- `src/tess/src/tlib.c` (Sub-Phase 4)
- `src/tess/src/test_tlib.c` (all new tests)

---

## Summary

| Sub-Phase | Location | Type | Blocks pack? |
|-----------|----------|------|-------------|
| 1: Module Validation | `tess_exe.c` | Error on mismatch | Yes (error) |
| 2: Export Scanning | `tess_exe.c` | Detection only | No |
| 3: Export Warnings | `tess_exe.c` | Warning on mismatch | No (warning) |
| 4: Self-Containment | `tlib.c` | Error on escape | Yes (error) |
