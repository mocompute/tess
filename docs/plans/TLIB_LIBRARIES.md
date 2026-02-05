# Tess Library Packing: Design Plan

## Summary

Add a `tess pack` command that bundles Tess source files into a `.tlib` **package** for distribution as a reusable library. A package contains one or more **modules** (declared with `#module`) plus metadata. Consumers include packages via a manifest file (with paths and version verification) or via `-l` flags (without version verification). The compiler performs whole-program compilation as usual. Access control has two levels: the manifest's `modules` list controls which modules consumers can import, and the `[[export]]` attribute controls which symbols within those modules are accessible.

This is distinct from C-compatible shared libraries (`tess lib` producing `.so`/`.dll`), which remain unchanged. A future `[[c_export]]` attribute may be introduced for C-compatible symbol export.

---

## Terminology

- **Module**: A namespace declared with `#module Name` in a `.tl` source file. Typically one file defines one module. Module members are accessed as `Module.function()`.
- **Package**: A `.tlib` archive containing one or more modules plus metadata. Packages bundle modules for distribution but have no name visible to source code--only the modules inside are visible.

**Notes:**
- Every `.tl` file MUST have a `#module` directive before any definitions. The parser will error if it encounters a definition before seeing `#module`.
- Module names are discovered at load time by parsing `#module` declarations (because `#module` directives may be conditional inside `#ifdef`/`#endif`). The manifest's `modules` field separately declares which discovered modules are *public*--this is for access control, not discovery.

**Import semantics:**

`#import "file.tl"` is **not** like C's `#include` (text replacement). Instead:
1. All imports are collected from all source files
2. Each imported file is parsed as a separate compilation unit in depth-first order
3. All files are then compiled together as a single translation unit

This means an imported file's symbols are available to all other files in the translation unit via `ModuleName.symbol` syntax. Each file declares its own module namespace.

---

## Design Decisions

### Source-Only Archives

A `.tlib` is a compressed archive of `.tl` source files plus metadata. There is no pre-compilation or IR. The consumer extracts the source and compiles everything together via whole-program compilation.

**Rationale:**
- The compiler is designed for whole-program compilation; this preserves that model
- Generic functions specialize correctly because the consumer sees the full source
- No partial linking or symbol resolution complexity

### Single-File Distribution

Source is compressed and stored inside the `.tlib` file itself rather than as sidecar files. This makes distribution ergonomic (one file to ship).

### Single Global Module Namespace

All modules--whether from local source files or included packages--share a single global namespace. If two packages define a module with the same name, or if local code defines a module that conflicts with a package, the compiler emits an error early in compilation after parsing `#module` declarations.

This matches how Tess currently works: there is one global namespace for modules, and `Module.function()` syntax accesses members.

### Import Syntax

Three forms of `#import` serve different purposes:

| Syntax | Purpose | Resolution |
|--------|---------|------------|
| `#import "file.tl"` | Local file import | Relative to importing file, then `-I` paths |
| `#import <file.tl>` | Standard library import | Standard library paths (`-S` paths) |
| `#import ModuleName` | Package module import | Modules from included packages (via manifest or `-l`) |

The unquoted form (`#import ModuleName`) is **only** for modules provided by packages. Local project modules must use the quoted form with a file path.

### Package Dependencies

When `tess pack` encounters an import that resolves to a module from another package (not a local file), it records that package as a dependency in the metadata. The source from external packages is **not** bundled--only local source files are included.

**Dependency format:** `PackageName=Version` (e.g., `Utils=1.2.0`)

Version is always required. Multiple dependencies are comma-separated:
`Utils=1.2.0,Logger=0.5.3`. Whitespace before and after commas is ok.

When compiling, if a package declares dependencies, the compiler verifies:
1. Each required package is provided (via manifest or `-l`)
2. The provided package's version matches exactly (strict equality)

**Future: Hash verification**

A future extension will add optional hash verification for reproducible builds:

```
Utils=1.2.0#a1b2c3d4e5f6...
```

The hash covers a canonical representation of the package content (platform-independent):
- Package name and version
- Sorted list of (filename, content) pairs
- Uses a standard hash algorithm (e.g., SHA-256, truncated for readability)

This ensures byte-for-byte reproducibility regardless of when or where the package was created.

### `[[export]]` as API Boundary

- `[[export]]` marks a symbol as part of the library's public API
- `[[export]]` is ignored by `tess pack` (it just bundles everything)
- `[[export]]` is ignored by `tess exe` on standalone files (no packages)
- `[[export]]` is enforced when `tess exe` compiles a program that includes packages: consumer code may only reference `[[export]]`-annotated symbols from the package's modules
- A future `[[c_export(name)]]` variant may allow custom symbol naming for C-compatible libraries

### Standard Library Exclusion

Standard library files are excluded from `.tlib` archives. The consumer's compiler has its own standard library. Only user-authored source files are bundled.

### Whole-Program Compilation with Tree Shaking

When packages are included (via manifest or `-l`), all modules from all packages are loaded into the global namespace. The compiler performs whole-program compilation, and tree shaking at the end of the pipeline discards code not referenced by the program.

---

## End-to-End Example

This example walks through a library producer publishing a `MathUtils` package (which depends on a `LoggingLib` package) and a consumer using it.

### Prerequisite: The LoggingLib Package

First, assume a logging package exists. In this example, the **package name** ("LoggingLib") differs from the **module name** ("Logger") to illustrate that they are independent--they may also be the same:

**LoggingLib's manifest.toml:**
```toml
[package]
name = "LoggingLib"
version = "2.0.0"
author = "Bob"
modules = ["Logger"]
```

**LoggingLib's module (logger.tl):**
```tl
#module Logger    // module name used in code

#import <stdio.tl>

[[export]] warn(msg) {
  c_fprintf(c_stderr, "[WARN] %s\n", msg)
  void
}

[[export]] error(msg) {
  c_fprintf(c_stderr, "[ERROR] %s\n", msg)
  void
}
```

Built with:
```bash
tess pack -m manifest.toml logger.tl -o LoggingLib.tlib
```

Package names and module names are independent (they may differ or be the same). Package names appear in manifests and dependency metadata. Module names appear in source code (`#import Logger`, `Logger.error(...)`).

### Producer: Creating a Package with Dependencies

**Directory structure:**
```
mathutils/
  manifest.toml
  libs/
    LoggingLib.tlib
  src/
    math.tl
    internal.tl
```

**src/math.tl** -- the public module:
```tl
#module MathUtils

#import "internal.tl"    // adds internal.tl to compilation unit

// Public API (exported)
[[export]] clamp(x, lo, hi) {
  _validate_range(lo, hi)    // same-module call
  if x < lo { lo }
  else if x > hi { hi }
  else { x }
}

[[export]] lerp(a, b, t) {
  a + (b - a) * t
}

// Internal helper (not exported)
_validate_range(lo, hi) {
  if lo > hi { MathUtils.Internal.fatal("invalid range") }
}
```

**src/internal.tl** -- internal module (not exported):
```tl
#module MathUtils.Internal

#import Logger    // package dependency (unquoted)

// Internal function (not exported)
fatal(msg) {
  Logger.error(msg)    // use the Logger package
  c_exit(1)
  void
}
```

Note: Every file must have a `#module` directive. Internal implementation uses a submodule (`MathUtils.Internal`) whose symbols are not exported, so consumers cannot access them.

**manifest.toml:**
```toml
[package]
name = "MathUtils"
version = "1.0.0"
author = "Alice"
modules = ["MathUtils"]    # only public module; MathUtils.Internal is internal

[dependencies]
LoggingLib = { version = "2.0.0", path = "libs/LoggingLib.tlib" }
```

Note: `MathUtils.Internal` is not listed in `modules`, so consumers cannot import it. The manifest references the **package name** ("LoggingLib"), while the source code uses the **module name** (`#import Logger`, `Logger.error(...)`).

**Build the package:**
```bash
tess pack -m manifest.toml src/math.tl -o MathUtils.tlib
```

The resulting `MathUtils.tlib` contains `math.tl` and `internal.tl` (but NOT LoggingLib's source--that stays in LoggingLib.tlib). The metadata records:
- name="MathUtils", version="1.0.0"
- requires="LoggingLib=2.0.0"

### Consumer: Using the Package

**Directory structure:**
```
myapp/
  manifest.toml
  libs/
    MathUtils.tlib
    LoggingLib.tlib    # transitive dependency
  src/
    main.tl
```

**src/main.tl:**
```tl
#module main

#import MathUtils    // unquoted = package module
// #import MathUtils.Internal          // ERROR: not in package's modules list

main() {
  x := MathUtils.clamp(150, 0, 100)
  c_printf("clamped: %d\n", x)    // prints "clamped: 100"

  y := MathUtils.lerp(0.0, 10.0, 0.5)
  c_printf("lerp: %f\n", y)       // prints "lerp: 5.0"

  // MathUtils._validate_range(1, 2)      // ERROR: not [[export]]ed

  0
}
```

**Option A: With manifest (version verification):**

**manifest.toml:**
```toml
[package]
name = "MyApp"
version = "0.1.0"

[dependencies]
MathUtils = { version = "1.0.0", path = "libs/MathUtils.tlib" }
LoggingLib = { version = "2.0.0", path = "libs/LoggingLib.tlib" }    # transitive dep
```

```bash
tess exe -m manifest.toml src/main.tl -o myapp
```

**Option B: Without manifest (quick and simple):**

```bash
tess exe src/main.tl -l libs/MathUtils.tlib -l libs/LoggingLib.tlib -o myapp
```

Note: The consumer must provide LoggingLib.tlib even though main.tl doesn't directly import it--MathUtils depends on it. The compiler verifies that MathUtils's declared dependency (LoggingLib=2.0.0) is satisfied by the provided LoggingLib.tlib.

Both options produce the same executable. Option A verifies all package versions match; Option B uses whatever versions are in the `.tlib` files.

---

## `.tlib` Archive Format

A custom binary format, chosen over tar for simplicity, security, and zero external dependencies (aside from libdeflate for compression).

### Layout

```
[4 bytes]  Magic: "TLIB"
[4 bytes]  Format version: 1 (big-endian uint32)
[4 bytes]  Name length (big-endian uint32)
[N bytes]  Package name (UTF-8 string)
[4 bytes]  Author length (big-endian uint32)
[N bytes]  Author (UTF-8 string, may be empty)
[4 bytes]  Version length (big-endian uint32)
[N bytes]  Version (UTF-8 string)
[4 bytes]  Modules length (big-endian uint32)
[N bytes]  Modules (UTF-8 string, comma-separated public module names)
[4 bytes]  Requires length (big-endian uint32)
[N bytes]  Requires (UTF-8 string, comma-separated dependencies, may be empty)
[4 bytes]  Requires-optional length (big-endian uint32)
[N bytes]  Requires-optional (UTF-8 string, comma-separated dependencies, may be empty)
[4 bytes]  Uncompressed payload size (big-endian uint32)
[4 bytes]  Compressed payload size (big-endian uint32)
[N bytes]  Deflate-compressed payload
```

### Metadata Fields

| Field | Required | Description |
|-------|----------|-------------|
| Name | Yes | Package name (e.g., "MyLibrary") |
| Author | No | Author name or email |
| Version | Yes | Version string, free-form (compared as literal string) |
| Modules | Yes | Comma-separated list of public module names |
| Requires | No | Comma-separated list of required versioned dependencies |
| Requires-optional | No | Comma-separated list of optional versioned dependencies |

**Dependency format:** `PackageName=Version[#Hash]`

Examples:
- `Utils=1.2.0` -- requires Utils version 1.2.0
- `Utils=1.2.0,Logger=0.5.3` -- multiple dependencies
- `Utils=1.2.0#a1b2c3d4...` -- with hash verification (future)

### Payload (before compression)

```
[4 bytes] File count (big-endian uint32)
For each file:
    [4 bytes] Filename length (big-endian uint32)
    [N bytes] Filename (UTF-8, relative path)
    [4 bytes] Content length (big-endian uint32)
    [N bytes] Content (raw source bytes)
```

### Path Handling

Files are stored with their directory structure relative to a common root (the nearest common ancestor of all packed files). This preserves import resolution semantics:

- `liba/core.tl` and `libb/core.tl` can coexist
- `#import "../util/util.tl"` from `liba/foo.tl` resolves correctly to `util/util.tl`
- The compiler's existing relative import resolution works unchanged

`..` components are allowed in stored paths. The validation rule is:
- No path may escape the archive root (e.g., `../../../etc/passwd` is rejected)
- After packing, every quoted `#import` in every archived file must resolve to another file in the archive (self-containment check for local imports)

### Safety

- All lengths are bounds-checked against remaining buffer size on read
- Maximum individual file size and total archive size limits prevent memory exhaustion
- Filenames are validated: no absolute paths, no escape from archive root

### Compression

Deflate via vendored libdeflate. Only the payload is compressed; the header and metadata are uncompressed so the reader can inspect metadata without decompressing.

---

## Implementation Phases

This section describes an incremental implementation plan. Each phase is designed to be testable independently, allowing validation of the design direction before investing in later phases. The system is usable at each stage, even with limitations.

### Completed Phases

#### Phase 1: Vendor libdeflate ✓

- Vendored libdeflate-1.25 source into `vendor/libdeflate-1.25/`
- Integrated into both Makefile and CMakeLists.txt
- Compressor and decompressor available via `#include "libdeflate.h"`

#### Phase 2: Basic Archive Format ✓

Implemented in `src/tess/src/tlib.c` with header `src/tess/include/tlib.h`:

```c
typedef struct {
    char const *name;
    u32         name_len;
    byte const *data;
    u32         data_len;
} tl_tlib_entry;

typedef struct {
    tl_tlib_entry *entries;
    u32            count;
} tl_tlib_archive;
```

- `tl_tlib_write()`: Writes magic, version, compressed payload
- `tl_tlib_read()`: Reads and validates archive, decompresses payload
- `tl_tlib_valid_filename()`: Validates filenames (rejects absolute paths, ".." escapes)
- Unit tests in `src/tess/src/test_tlib.c`

**Note:** This is a simplified format without metadata fields. The full format with name/version/modules/requires will be added in Phase 4.

#### Phase 3: Pack/Unpack CLI Commands ✓

Implemented in `src/tess/src/tess_exe.c`:

```bash
tess pack <file1.tl> [file2.tl ...] -o output.tlib [-v]
tess unpack archive.tlib [-o output_dir] [--list] [-v]
```

High-level operations in `src/tess/src/tlib.c`:
- `tl_tlib_pack()`: Resolves imports, excludes stdlib, computes relative paths
- `tl_tlib_unpack()`: Extracts files or lists contents

**Limitations:** No manifest support, no metadata in archive.

---

### Remaining Phases

The remaining work is organized into phases that build incrementally. Each phase has clear validation criteria and can be tested before proceeding.

#### Phase 4: Archive Metadata

**Goal:** Archives contain package information (name, version, modules, dependencies).

**Changes:**

Extend the archive format to include metadata fields before the payload:

```c
typedef struct {
    str name;              // package name
    str author;            // author (may be empty)
    str version;           // version string
    str modules;           // comma-separated public module names
    str requires;          // comma-separated required dependencies
    str requires_optional; // comma-separated optional dependencies
} tl_tlib_metadata;

typedef struct {
    tl_tlib_metadata metadata;
    tl_tlib_entry   *files;
    u32              file_count;
} tl_tlib_archive;
```

Update `tl_tlib_write()` and `tl_tlib_read()` to handle metadata fields per the binary format specification in this document.

**Temporary CLI for testing:** Add command-line flags to `tess pack` for specifying metadata without a manifest:

```bash
tess pack --name MyLib --version 1.0.0 --modules "Foo,Bar" foo.tl -o MyLib.tlib
```

This allows testing the format before manifest parsing is implemented. The `--name` and `--version` flags are required; `--modules` defaults to empty.

**Validation:**
- Error if `--name` or `--version` is missing
- Unit tests for metadata roundtrip
- `tess unpack --list` shows metadata fields
- Keep the format version at v1 during development. There is no need
  for backwards compatibility.

#### Phase 5: Minimal TOML Manifest Parser

**Goal:** Parse TOML manifest files for package metadata.

**Scope:** Implement a minimal TOML subset parser supporting:
- `[section]` headers
- `[section.subsection]` dotted headers (for `[dependencies.PkgName]`)
- `key = "string value"`
- `key = ["array", "of", "strings"]`
- `# comments`
- Inline tables `{ key = "value", ... }` (for dependencies)

Both dependency styles are supported:

```toml
# Inline table style
[dependencies]
Utils = { version = "1.2.0", path = "libs/Utils.tlib" }

# Regular table style
[dependencies.Logger]
version = "0.5.3"
path = "libs/Logger.tlib"
```

**Implementation:**
- New file `src/tess/src/manifest.c` with header `src/tess/include/manifest.h`
- Hand-written parser (~300-400 lines), not a full TOML library
- Returns structured data for `[package]` and `[dependencies]` sections

```c
typedef struct {
    str name;
    str version;
    str author;
    str_sized modules;  // array of module names
} tl_manifest_package;

typedef struct {
    str name;
    str version;
    str path;
} tl_manifest_dep;

typedef struct {
    tl_manifest_package package;
    tl_manifest_dep    *deps;
    u32                 dep_count;
    tl_manifest_dep    *optional_deps;
    u32                 optional_dep_count;
} tl_manifest;
```

**Integrate with pack command:**

```bash
tess pack -m manifest.toml foo.tl -o Foo.tlib
```

When `-m` is provided, read metadata from manifest instead of command-line flags. At this phase, `[dependencies]` is parsed but not validated (dependencies aren't loaded yet).

**CLI conflict handling:** Error if `-m` is used together with `--name`, `--version`, or `--modules` flags.

**Validation:**
- Unit tests for manifest parsing (valid TOML, comments, missing fields, malformed)
- Integration test: pack with manifest, verify archive contains correct metadata
- Test that `-m` with metadata CLI flags produces an error

#### Phase 6: Module Discovery

**Goal:** Discover module names from `#module` directives in source files.

**Implementation:**

The existing code in `tess_exe.c` already handles conditional compilation for import resolution:
- `process_import_hash()` (lines 215-249) tracks `#ifdef`/`#ifndef`/`#endif` nesting
- `read_import_lines()` (lines 251-288) scans source for hash directives
- `import_skip_depth` and `import_defines` handle conditional state

Extend this infrastructure to also discover `#module` directives:

```c
// In process_import_hash(), add handling for #module:
if (0 == self->import_skip_depth && words.size >= 2 && str_eq(words.v[0], S("module"))) {
    // words.v[1] is the module name (e.g., "MathUtils" or "MathUtils.Internal")
    array_push(*modules_output, words.v[1]);
}
```

Options for integration:
1. **Extend existing functions**: Add a `str_array *modules` output parameter to `read_import_lines()` and `process_import_hash()` to collect module names alongside imports
2. **Factor out scanning**: Extract the line-scanning and conditional-compilation logic into a reusable scanner that can collect both imports and modules in one pass

Recommendation: Option 1 is simpler and avoids duplication. The scanner already does one pass over the source; adding module collection is minimal overhead

**Integrate with pack:**

After collecting source files, scan each for `#module` declarations:
1. Discover all modules in source files (both public and internal)
2. If manifest specifies `modules = [...]`:
   - Verify every module listed in manifest is discovered in source
   - Warn (but don't error) if discovered modules aren't in manifest (they're internal)
3. Store the manifest's `modules` list in archive metadata (not all discovered modules)

**Self-containment check:**

Verify every quoted `#import "file.tl"` in the archived files resolves to another file in the archive. Error if an import would escape the archive (references a file not being packed).

**Validation:**
- Unit tests for module discovery (simple, conditional, nested)
- Integration test: pack multi-module library, verify modules metadata matches
- Test self-containment check catches missing imports

#### Phase 7: Basic Package Consumption

**Goal:** Compile code that uses a package (without inter-package dependencies).

**Implementation:**

Add `-l` flag to `tess exe`:

```bash
tess exe main.tl -l Foo.tlib -o main
```

The compilation process:
1. Load each `-l` package via `tl_tlib_read()`
2. Extract source files into arena-allocated buffers
3. Track file→package provenance (which files came from which package)
4. Scan extracted source for `#module` declarations
5. Build module → package mapping
6. Extend import resolver to handle unquoted imports:
   - `#import ModuleName` → look up in module→package map → add package files to compilation
7. Feed all source (local + package) into existing compilation pipeline
8. Tree shaking removes unreferenced code

**File provenance tracking:** Each source file is tagged with its origin (local, or which package). This is needed later for access control (Phase 10-11) to determine if code is "inside" or "outside" a package.

**Unquoted import resolution:**

Modify the tokenizer/parser to recognize `#import ModuleName` (no quotes, no angle brackets) as a package module import. The import resolver then:
1. Looks up `ModuleName` in the module→package mapping
2. If found, adds the package's source files to compilation (if not already added)
3. If not found, emits an error

**Validation:**
- Integration test: Create a simple package, compile a consumer that imports it
- Verify the end-to-end example from this document works (without LoggingLib dependency)

#### Phase 8: Inter-Package Dependencies

**Goal:** Package A can declare and use Package B as a dependency.

**Implementation:**

**During pack:**
1. Load dependency packages from manifest's `[dependencies]` paths
2. Verify each package's version matches manifest declaration
3. Scan dependencies to discover their modules
4. Build module → package mapping for dependencies
5. When packing source uses `#import ModuleName`:
   - If `ModuleName` is from a dependency, record that dependency
6. Write used dependencies to archive's `requires` field
7. Error if a required dependency is declared but never used
8. Optional dependencies may be unused

**During compile:**
1. When loading a package, check its `requires` field
2. Verify all required packages are provided (via `-l` or manifest)
3. Verify versions match
4. Load dependencies recursively (handle transitive deps)
5. Error on missing dependencies with helpful message

**Validation:**
- Integration test: MathUtils→LoggingLib example from this document
- Test version mismatch detection
- Test missing dependency detection
- Test transitive dependency handling

#### Phase 9: Manifest-Based Compilation

**Goal:** `tess exe` can use a manifest for version-verified compilation.

**Implementation:**

```bash
tess exe -m manifest.toml main.tl -o main
```

When `-m` is provided:
1. Parse manifest for `[dependencies]`
2. Load packages from manifest paths (not `-l` flags)
3. Verify versions match manifest declarations
4. `-l` flags are disallowed when `-m` is used

This enables reproducible builds with pinned versions.

**Validation:**
- Integration test: compile with manifest, verify version checking
- Test that `-l` and `-m` together produce an error

#### Phase 10: Module-Level Access Control

**Goal:** Consumers can only import modules listed in a package's `modules` metadata.

**Implementation:**

When resolving `#import ModuleName` from a package:
1. Read the package's `modules` metadata
2. Verify `ModuleName` is in that list
3. If not, emit error: "module 'X' is not exported by package 'Y'"

Internal modules (not listed in `modules`) remain usable within the package itself. The file provenance tracking from Phase 7 determines what's "inside" vs "outside" the package.

**Validation:**
- Integration test: attempt to import internal module, verify error
- Test that internal modules work within the package

#### Phase 11: Symbol-Level Access Control (`[[export]]`)

**Goal:** Consumers can only access `[[export]]`-marked symbols from package modules.

**Implementation:**

1. Extend the parser to recognize `[[export]]` attribute on function definitions
2. During parsing, mark exported symbols in the AST
3. Use file provenance tracking (from Phase 7) to determine code origin
4. During type inference, when consumer code references a symbol from a package:
   - Verify the symbol has `[[export]]` attribute
   - If not, emit error: "symbol 'X' is not exported from module 'Y'"
5. Package-internal code (same provenance) can access non-exported symbols freely

**Future extension:** `[[c_export(name)]]` for C-compatible symbol naming.

**Validation:**
- Integration test: access exported symbol (works)
- Integration test: access non-exported symbol (error)
- Test that package-internal code can access non-exported symbols

#### Phase 12: Comprehensive Test Suite

Final validation and edge case coverage:

**Unit tests:**
- `.tlib` write/read roundtrip (correct format, corruption detection)
- Metadata field roundtrip (all fields, empty fields, special characters)
- Manifest parsing (valid TOML, comments, missing fields, malformed)
- Filename validation (rejects `..` escapes, absolute paths)
- Module discovery (simple, conditional, edge cases)

**Integration tests:**
- End-to-end: pack multi-file library → compile consumer → run
- Module name conflict detection across packages
- Pack dependency verification (missing dep, unused required dep, unused optional ok, version mismatch)
- Compile dependency verification (missing package, version mismatch, transitive dep missing)
- Module access control (import non-public module → error)
- Symbol access control (access non-exported symbol → error)
- Generics in a package specialize correctly in the consumer
- Circular dependency detection (package A requires B, B requires A → error)

**Build system:**
- All tests added to both Makefile and CMakeLists.txt
- CI verification on multiple platforms

---

### Phase Dependencies

```
Phase 4 (Metadata)
    ↓
Phase 5 (Manifest Parser)
    ↓
Phase 6 (Module Discovery)
    ↓
Phase 7 (Basic Consumption)  ←── First end-to-end validation
    ↓
Phase 8 (Dependencies)
    ↓
Phase 9 (Manifest Compilation)
    ↓
Phase 10 (Module Access Control)
    ↓
Phase 11 (Symbol Access Control)
    ↓
Phase 12 (Test Suite)
```

**Key validation points:**
- After Phase 7: Can pack and consume a simple library (no dependencies)
- After Phase 8: Can handle library chains (A uses B)
- After Phase 11: Full access control model working

Each phase can be merged independently, allowing incremental progress and early feedback on the design

---

## Open Questions

### Resolved

- **TOML parsing**: Implement a minimal subset parser (~300-400 lines). Features: `[section]`, `[section.subsection]` dotted headers, `key = "value"`, `key = [...]`, inline tables `{ key = "value" }`, regular tables, and `# comments`. This avoids vendoring a full TOML library while covering all manifest needs.

- **Circular dependencies**: Error at pack time. When building the dependency graph, detect cycles and report an error listing the cycle path (e.g., "circular dependency: A → B → C → A").

- **Format version compatibility**: Keep v1 during development. The format is internal/experimental; no backwards compatibility needed.

### Open

- **`[[export]]` on types**: Should `[[export]]` work on struct/enum definitions, or only on functions and values? Options:
  1. Functions only (simplest)
  2. Functions and values (globals)
  3. Functions, values, and types (full control)

  Recommendation: Start with functions only (Phase 11), extend later if needed.

- **Hash algorithm**: For future hash verification, which algorithm? SHA-256 is standard but produces long hashes. Consider:
  - SHA-256 truncated to 16 bytes (32 hex chars)
  - BLAKE3 (faster, modern)

  This is not blocking—hash verification is a future extension.

- **Module naming conflicts**: What error message when two packages define the same module? Should we include package paths in the error? Example: "module 'Utils' defined in both 'libs/A.tlib' and 'libs/B.tlib'"

- **Transitive dependency version conflicts**: What if A requires Logger=1.0 and B requires Logger=2.0? Options:
  1. Error (strict, simple)
  2. Allow if versions are compatible (needs semver)

  Recommendation: Error for now. Semver support can be added later.

- **Optional dependencies for consumers**: How are `[dependencies.optional]` used during compilation? Options:
  1. Consumer must explicitly enable them (e.g., `-D USE_WINAPI`)
  2. Auto-detect based on platform
  3. Just provide them via `-l`; they're "optional" only for the producer

  Recommendation: Option 3 for simplicity. Optional means "producer doesn't always use this dep"; consumer provides what the package actually needs.
