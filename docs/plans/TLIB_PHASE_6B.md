# Phase 6b: Module Discovery Integration with Pack — Implementation Plan

**Parent document:** [TLIB_LIBRARIES.md](TLIB_LIBRARIES.md) (Phase 6b section)

**Status:** Complete (all sub-phases).

---

## Overview

Phase 6b connects the module discovery scanning (completed in Phase 6) with the pack command to provide validation and early feedback. The work is split into four sub-phases, each independently testable.

### Goals

1. Validate that manifest's `modules` list matches actually-discovered modules in source
2. Scan source files for `[[export]]` annotations and warn about mismatches
3. Verify the archive is self-contained (every quoted import resolves within the archive)

### Sub-Phase Dependencies

```
Sub-Phase 1 (Module Validation)    ✅ complete
    |
    v
Sub-Phase 2 ([[export]] Scanning)  ✅ complete
    |
    v
Sub-Phase 3 ([[export]] Warnings)  ✅ complete
    |
    v
Sub-Phase 4 (Self-Containment)     ✅ complete
```

Sub-Phase 4 is independent and could be done in any order, but is placed last because it involves more complex path logic in `tlib.c`.

---

## Design Decisions

These decisions were made during planning to resolve ambiguities in the parent design document.

### Internal modules: verbose-only, not warnings

The parent doc says "Warn (but don't error) if discovered modules aren't in manifest (they're internal)." However, internal modules are a normal and expected pattern (e.g., `MathUtils.Internal` in the end-to-end example). Emitting a warning for every internal module is noisy.

**Decision:** Do not warn about unused modules. Only emit actual warnings for likely mistakes (public module with no exports, non-public module with exports).

### `[[export]]` scanning: text-level, guard against false positives

For Phase 6b, `[[export]]` detection uses the source scanner's state machine, which already tracks string and comment context. The scanner detects `[[export]]` at line start (the `start` state) and ignores occurrences inside strings and comments.

### Module-to-file mapping via re-scan

This is no longer necessary, as the import-scanning infrastructure has been updated so that the `modules_seen` field is now a map with module keys and file path values (str).

### Validation as a standalone command

**Decision:** Validation logic lives in `tl_source_scanner_validate()` in `source_scanner.c`, callable from both a standalone `validate` CLI command and from `pack`. This makes validation independently testable without file I/O (unit tests operate on in-memory scanner state).

### Self-containment check in `tl_tlib_pack()`

**Decision:** The self-containment check happens inside `tl_tlib_pack()` (in `tlib.c`) after building the entries array but before writing. This checks what's actually going into the archive with final relative paths, which is more accurate than checking filesystem paths in `pack_files()`.

---

## Sub-Phase 1: Module Discovery Validation Against Manifest — COMPLETE

**Commit:** `a1f5da4`

### What was implemented

Validation function `tl_source_scanner_validate()` in `source_scanner.c` cross-checks scanner state against manifest module list:

- **Error** if manifest declares a module not found in `modules_seen`
- Reports internal modules (in source but not in manifest) only in verbose mode
- Returns `tl_source_scanner_validate_result` with `error_count` and `warning_count`
- Early returns with no validation when `module_count == 0`

Called from both `validate_files()` and `pack_files()` in `tess_exe.c`.

### Files changed

- `src/tess/include/source_scanner.h` — added `tl_source_scanner_validate_result` type and function declaration
- `src/tess/src/source_scanner.c` — implemented `tl_source_scanner_validate()`
- `src/tess/src/tess_exe.c` — added `validate` command, integrated validation into `pack`
- `src/tess/src/test_source_scanner.c` — 8 unit tests

### Tests

- `test_validate_missing_module` — manifest declares module not in source: error
- `test_validate_ok` — manifest module found with exports: no errors/warnings
- `test_validate_empty_modules` — empty module list: skip validation
- `test_validate_multiple_one_missing` — two manifest modules, one missing: error

---

## Sub-Phase 2: `[[export]]` Scanning — COMPLETE

**Commit:** `ad28567`

### What was implemented

Export detection integrated into the source scanner's state machine in `source_scanner.c`. The scanner detects `[[export]]` at line start (in the `start` state), correctly ignoring occurrences inside strings and comments. When a file contains both a `#module` directive and `[[export]]`, the module name is added to the `export_seen` hashset.

### Files changed

- `src/tess/include/source_scanner.h` — added `export_seen` and `current_file_module` fields
- `src/tess/src/source_scanner.c` — export detection in state machine, populate `export_seen`
- `src/tess/src/test_source_scanner.c` — 6 unit tests

### Tests

- `test_export_basic` — module with `[[export]]` appears in `export_seen`
- `test_export_absent` — module without `[[export]]` does not appear
- `test_export_in_string` — `[[export]]` inside string is ignored
- `test_export_in_comment` — `[[export]]` inside comment is ignored
- `test_export_no_module` — `[[export]]` without `#module` adds nothing
- `test_export_multiple_files` — only files with `[[export]]` appear

---

## Sub-Phase 3: `[[export]]` Warning Integration — COMPLETE

**Commit:** `a1f5da4` (combined with Sub-Phase 1)

### What was implemented

Warning checks in `tl_source_scanner_validate()`:

- **Warning** if a public module (in manifest) has no `[[export]]` symbols
- **Warning** if a non-public module has `[[export]]` symbols

Warnings do not block pack — only `error_count > 0` causes failure. The `validate` command reports warning count in its summary message.

### Tests

- `test_validate_warn_public_no_export` — public module, no exports: 1 warning
- `test_validate_warn_export_not_public` — non-public module with exports: 1 warning
- `test_validate_internal_module_ok` — internal module without exports: no warnings
- `test_validate_both_warnings` — both warning types: 2 warnings

---

## Sub-Phase 4: Self-Containment Check — COMPLETE

**Commit:** `9827279`

### What was implemented

Self-containment check in `tl_tlib_pack()` (in `tlib.c`) verifies every quoted `#import "file.tl"` resolves to another file in the archive. Runs after building the entries array but before writing.

**Static function `check_self_containment()`** in `tlib.c`:
- Builds a hashset of entry names (relative paths in the archive)
- For each entry, uses `tl_source_scanner_collect_imports()` to extract all `#import` directives with correct string/comment handling
- Skips angle-bracket imports (stdlib, not in archive)
- For quoted imports: computes directory from entry name, joins with import path, normalizes, checks hashset
- Validates closing quote delimiter (guards against malformed imports)
- Returns 0 if self-contained, 1 if an import escapes the archive

**Shared scanner infrastructure:** The state machine in `source_scanner.c` was refactored into a callback-based `scan_directives()` core function. Both the full scanner (`tl_source_scanner_scan()`) and import-only extraction (`tl_source_scanner_collect_imports()`) share this state machine, ensuring correct string/comment handling in both paths.

**Design decisions:**
- Conservative scanning: collects ALL imports regardless of `#ifdef` conditionals (archive integrity check, not compilation behavior)
- Uses `tl_source_scanner_collect_imports()` (no `tl_source_scanner` struct needed) for clean separation

### Files changed

- `src/tess/src/source_scanner.c` — extracted `scan_directives()` callback-based core, refactored `tl_source_scanner_scan()` to use it, added `tl_source_scanner_collect_imports()`
- `src/tess/include/source_scanner.h` — added `tl_source_scanner_collect_imports()` declaration
- `src/tess/src/tlib.c` — added `check_self_containment()`, called from `tl_tlib_pack()`
- `src/tess/src/test_source_scanner.c` — 4 new tests for `tl_source_scanner_collect_imports()`
- `src/tess/src/test_tlib.c` — 5 new self-containment tests

### Tests

Source scanner collect_imports tests (`test_source_scanner.c`):
- `test_collect_basic` — extracts both quoted and angle-bracket imports
- `test_collect_ignores_string` — `#import` inside string literal is ignored
- `test_collect_ignores_comment` — `#import` inside `//` comment is ignored
- `test_collect_ignores_conditionals` — imports inside `#ifdef` blocks are still collected (no conditional filtering)

Self-containment tests (`test_tlib.c`):
- `test_pack_self_contained` — `a.tl` imports `b.tl`, both packed: succeeds
- `test_pack_not_self_contained` — `a.tl` imports `missing.tl`: fails with error
- `test_pack_stdlib_import_ok` — `#import <stdio.tl>`: succeeds (stdlib ignored)
- `test_pack_malformed_import_ok` — `#import "unterminated` (no closing quote): gracefully skipped
- `test_pack_subdir_import` — `lib/a.tl` imports `../util.tl`: succeeds (relative path resolution)

---

## Build System

No new source files were created for Sub-Phases 1-3. All validation logic is in existing `source_scanner.c`, tests in existing `test_source_scanner.c`, CLI in existing `tess_exe.c`.

Sub-Phase 4 adds code to existing `tlib.c` and tests to existing `test_tlib.c`. No build system changes expected.

---

## Summary

| Sub-Phase | Location | Type | Blocks pack? | Status |
|-----------|----------|------|-------------|--------|
| 1: Module Validation | `source_scanner.c` | Error on mismatch | Yes (error) | Complete |
| 2: Export Scanning | `source_scanner.c` | Detection only | No | Complete |
| 3: Export Warnings | `source_scanner.c` | Warning on mismatch | No (warning) | Complete |
| 4: Self-Containment | `tlib.c` | Error on escape | Yes (error) | Complete |

### Key infrastructure built

- `source_scanner.c/.h` — extracted from `tess_exe.c` as a testable library
- Callback-based `scan_directives()` core with 8-state state machine (strings, comments, `[[export]]`)
- `tl_source_scanner_scan()` — full scanner with conditional compilation, module tracking, exports
- `tl_source_scanner_collect_imports()` — lightweight import-only extraction (no scanner struct needed)
- `modules_seen` (str->str map) and `export_seen` (str hashset) track scan results
- `tl_source_scanner_validate()` — reusable validation function with structured result
- `validate` CLI command — standalone validation without packing
- `check_self_containment()` in `tlib.c` — archive integrity check using shared scanner
- 38 unit tests in `test_source_scanner.c`, 5 self-containment tests in `test_tlib.c`
