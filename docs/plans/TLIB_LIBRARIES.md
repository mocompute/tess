# Tess Library Packing: Design Plan

## Summary

Add a `tess pack` command that bundles Tess source files into a `.tlib` **package** for distribution as a reusable library. A package contains one or more **modules** (declared with `#module`) plus metadata. Every package (including consumer applications) has a `package.tl` file at its root that declares metadata, exported modules, and dependencies using a function-call DSL that is valid TL syntax. The compiler auto-discovers `package.tl` in the current working directory. The compiler performs whole-program compilation as usual. The `export()` declarations in `package.tl` document which modules are part of the package's public API, though access control is not enforced--all package modules enter the global namespace. Package modules become available to consumer code automatically when declared via `depend()` in `package.tl`--no explicit import is needed. Tree shaking removes unused code.

This is distinct from C-compatible libraries (`tess lib` producing `.so`/`.dll`, or `tess lib --static` producing `.a`/`.lib`), which remain unchanged.

---

## Terminology

- **Module**: A namespace declared with `#module Name` in a `.tl` source file. Typically one file defines one module. Module members are accessed as `Module.function()`.
- **Package**: A `.tlib` archive containing one or more modules plus metadata. Packages bundle modules for distribution but have no name visible to source code--only the modules inside are visible.

**Notes:**
- Every `.tl` file MUST have a `#module` directive before any definitions. The parser will error if it encounters a definition before seeing `#module`.
- Module names are discovered at load time by parsing `#module` declarations (because `#module` directives may be conditional inside `#ifdef`/`#endif`). The `export()` declarations in `package.tl` separately declare which discovered modules are *public*--this is for documentation and tooling, not discovery or access control.

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

**Consequence: strict version equality.** Because all modules share a single global namespace, two versions of the same package cannot coexist--they would define the same module names, causing a conflict. This means dependency version requirements must use strict equality (`Logger=2.0.0`), not version ranges. This is not a temporary simplification; it is a structural requirement of the flat namespace model. If package A requires `Logger=1.0` and package B requires `Logger=2.0`, the compiler must emit an error--there is no way to satisfy both.

### Import Syntax

Two forms of `#import` serve different purposes:

| Syntax | Purpose | Resolution |
|--------|---------|------------|
| `#import "file.tl"` | Local file import | Relative to importing file, then `-I` paths |
| `#import <file.tl>` | Standard library import | Standard library paths (`-S` paths) |

Package modules do not use `#import`. When a package is declared as a dependency via `depend()` in `package.tl`, all its modules are loaded automatically and enter the global namespace. Consumer code accesses them directly via qualified syntax (`Module.function()`).

### Package Dependencies

Dependencies declared in `package.tl` via `depend()` are recorded in the `.tlib` archive metadata. The source from external packages is **not** bundled--only local source files are included.

Consumers only need to declare their **direct** dependencies in `package.tl` via `depend()`. When the compiler loads a package, it reads the archive's dependency metadata and automatically resolves transitive dependencies from `depend_path()` directories, verifying version equality. See Phase 8 for the full resolution algorithm.

The binary encoding of dependencies in the `.tlib` archive is described in the Archive Format section.

### Module-Level Access Control

The `export()` declarations in `package.tl` declare which modules are part of the package's public API. However, access control is **not enforced** by the compiler. When a consumer loads a package, all modules (exported and internal) enter the global namespace because exported modules may depend on internal ones. A consumer *could* call into `MathUtils.Internal` — it is the producer's responsibility to name internal modules clearly (e.g., using an `.Internal` suffix) and to document the public API via `export()`.

The `export()` declaration serves three purposes:
1. **Documentation** — clearly communicates which modules are the public API
2. **Tooling** — future tools (linters, documentation generators) can use it
3. **Pack validation** — `tess pack` verifies that exported modules exist in the source

Library authors control their API surface by organizing code into public and internal modules. For example, a package might expose `MathUtils` while keeping `MathUtils.Internal` as an internal detail. Using clear naming conventions makes the boundary obvious to consumers.

### Standard Library Exclusion

Standard library files are excluded from `.tlib` archives. The consumer's compiler has its own standard library. Only user-authored source files are bundled.

### Whole-Program Compilation with Tree Shaking

When packages are included (via `package.tl`), all modules from all packages are loaded into the global namespace. The compiler performs whole-program compilation, and tree shaking at the end of the pipeline discards code not referenced by the program.

### Diamond Dependencies

When packages A and B both depend on package C, the consumer provides C once. Because C's source is compiled exactly once into a single compilation unit, type identity is naturally preserved--`C.SomeType` as seen by A is the same type as seen by B. This is a direct benefit of the source-only, whole-program compilation model. Pre-compiled or IR-based approaches would need explicit type deduplication or canonical type representations; source-level compilation avoids this entirely.

---

## End-to-End Example

This example walks through a library producer publishing a `MathUtils` package (which depends on a `LoggingLib` package) and a consumer using it.

### Prerequisite: The LoggingLib Package

First, assume a logging package exists. In this example, the **package name** ("LoggingLib") differs from the **module name** ("Logger") to illustrate that they are independent--they may also be the same:

**LoggingLib's `package.tl`:**
```tl
format(1)
package("LoggingLib")
version("2.0.0")
author("Bob")
export("Logger")
```

**LoggingLib's module (logger.tl):**
```tl
#module Logger    // module name used in code

#import <stdio.tl>

warn(msg) {
  c_fprintf(c_stderr, "[WARN] %s\n", msg)
  void
}

error(msg) {
  c_fprintf(c_stderr, "[ERROR] %s\n", msg)
  void
}
```

Built with:
```bash
tess pack logger.tl -o LoggingLib.tlib
```

The compiler auto-discovers `package.tl` in the current working directory. Package names and module names are independent (they may differ or be the same). Package names appear in `package.tl` and dependency metadata. Module names appear in source code (`Logger.error(...)`).

### Producer: Creating a Package with Dependencies

**Directory structure:**
```
mathutils/
  package.tl
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

clamp(x, lo, hi) {
  MathUtils.Internal.validate_range(lo, hi)
  if x < lo { lo }
  else if x > hi { hi }
  else { x }
}

lerp(a, b, t) {
  a + (b - a) * t
}
```

**src/internal.tl** -- internal module (not listed in `export()`):
```tl
#module MathUtils.Internal

validate_range(lo, hi) {
  if lo > hi { fatal("invalid range") }
}

fatal(msg) {
  Logger.error(msg)    // use the Logger package
  c_exit(1)
  void
}
```

Note: Every file must have a `#module` directive. `MathUtils.Internal` is not listed in `export()`, so it is not part of the public API. It is still loaded into consumer compilations (because `MathUtils` depends on it), but consumers should not rely on it. Tree shaking will remove it from the final binary if unused by consumer code.

**package.tl:**
```tl
format(1)
package("MathUtils")
version("1.0.0")
author("Alice")
export("MathUtils")    // only public module; MathUtils.Internal is internal

depend("LoggingLib", "2.0.0")
depend_path("./libs")
```

Note: `MathUtils.Internal` is not listed in `export()`, signaling it is not part of the public API. `package.tl` references the **package name** ("LoggingLib"), while the source code uses the **module name** (`Logger.error(...)`).

**Build the package:**
```bash
tess pack src/math.tl -o MathUtils.tlib
```

The resulting `MathUtils.tlib` contains `math.tl` and `internal.tl` (but NOT LoggingLib's source--that stays in LoggingLib.tlib). The metadata records:
- name="MathUtils", version="1.0.0"
- depends=["LoggingLib=2.0.0"]

### Consumer: Using the Package

**Directory structure:**
```
myapp/
  package.tl
  libs/
    MathUtils.tlib
    LoggingLib.tlib    # transitive dependency
  src/
    main.tl
```

**src/main.tl:**
```tl
#module main

// No #import needed for package modules -- they are loaded automatically
// from depend() declarations in package.tl

main() {
  x := MathUtils.clamp(150, 0, 100)
  c_printf("clamped: %d\n", x)    // prints "clamped: 100"

  y := MathUtils.lerp(0.0, 10.0, 0.5)
  c_printf("lerp: %f\n", y)       // prints "lerp: 5.0"

  0
}
```

**package.tl:**
```tl
format(1)
package("MyApp")
version("0.1.0")

depend("MathUtils", "1.0.0")
depend_path("./libs")
// LoggingLib is NOT listed -- resolved automatically from depend_path
```

```bash
tess exe src/main.tl -o myapp
```

The compiler auto-discovers `package.tl` in the current working directory, loads declared dependencies, and
verifies versions match. Consumers only declare **direct** dependencies. The compiler reads MathUtils's
`depends` field (`"LoggingLib", "2.0.0"`), searches `depend_path()` directories for `LoggingLib.tlib`,
verifies the version matches, and loads it automatically. If a transitive dependency cannot be found, the
compiler emits an error naming the missing package and which package requires it.

A `package.tl` is required for any build that uses packages. Builds that do not use packages (e.g., local-only projects, individual test files) work without `package.tl` — the compiler simply skips dependency loading when no `package.tl` is found. There are no `-l` or `-L` CLI flags — all dependency information comes from `package.tl`.

---

## `.tlib` Archive Format

A custom binary format, chosen over tar for simplicity, security, and zero external dependencies (aside from libdeflate for compression).

### Layout

```
[4 bytes]  Magic: "TLIB"
[4 bytes]  Format version: 1 (big-endian uint32)
[2 bytes]  Name length (big-endian uint16)
[N bytes]  Package name (UTF-8 string)
[2 bytes]  Author length (big-endian uint16)
[N bytes]  Author (UTF-8 string, may be empty)
[2 bytes]  Version length (big-endian uint16)
[N bytes]  Version (UTF-8 string)
[2 bytes]  Modules count (big-endian uint16)
For each module:
    [2 bytes]  Module name length (big-endian uint16)
    [N bytes]  Module name (UTF-8 string)
[2 bytes]  depends count (big-endian uint16)
For each required dependency:
    [2 bytes]  Dependency string length (big-endian uint16)
    [N bytes]  Dependency string (UTF-8, format: "PackageName=Version")
[2 bytes]  depends-optional count (big-endian uint16)
For each optional dependency:
    [2 bytes]  Dependency string length (big-endian uint16)
    [N bytes]  Dependency string (UTF-8, format: "PackageName=Version")
[4 bytes]  Uncompressed payload size (big-endian uint32)
[4 bytes]  Compressed payload size (big-endian uint32)
[N bytes]  Deflate-compressed payload
[4 bytes]  CRC32 checksum of all preceding bytes (big-endian uint32)
```

### Integrity Check

A CRC32 checksum is stored at the end of the archive, covering all bytes from the magic through the compressed payload. On read, the reader computes CRC32 over everything before the last 4 bytes and compares. A mismatch produces a corruption error. CRC32 is available via vendored libdeflate (`libdeflate_crc32`). This is for corruption detection, not a security feature--the future SHA-256 hash in dependency records serves that purpose.

### Metadata Fields

| Field | Size | Required | Description |
|-------|------|----------|-------------|
| Name | u16-prefixed string | Yes | Package name (e.g., "MyLibrary") |
| Author | u16-prefixed string | No | Author name or email |
| Version | u16-prefixed string | Yes | Version string, free-form (compared as literal string) |
| Modules | u16 count + u16-prefixed strings | Yes | Array of public module names |
| depends | u16 count + u16-prefixed strings | No | Array of required versioned dependencies |
| depends-optional | u16 count + u16-prefixed strings | No | Array of optional versioned dependencies |

All metadata fields use u16 (big-endian) for lengths and counts, giving a maximum of 65535 bytes per string and 65535 entries per array -- more than sufficient for metadata. Only the payload sizes (uncompressed and compressed) use u32, since source archives can be large.

**Dependency string format:** `PackageName=Version[#Hash]`

Version is always required. Each dependency is stored as a u16 length-prefixed UTF-8 string.

Examples:
- `Utils=1.2.0` -- requires Utils version 1.2.0
- `Utils=1.2.0#a1b2c3d4...` -- with hash verification (future)

**Future: Hash verification.** A future extension will add optional hash verification for reproducible builds. The hash would cover a canonical representation of the package content (platform-independent): package name and version, sorted list of (filename, content) pairs, using a standard hash algorithm (e.g., SHA-256, truncated for readability). This ensures byte-for-byte reproducibility regardless of when or where the package was created.

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

Files are stored with their directory structure relative to a common root (the nearest common ancestor of all packed files). All paths are normalized at pack time--`..` components are resolved before storing. This preserves import resolution semantics:

- `liba/core.tl` and `libb/core.tl` can coexist
- `#import "../util/util.tl"` from `liba/foo.tl` resolves correctly to `util/util.tl` (the `..` is in the source code, resolved at compile time against the archive's directory structure)
- The compiler's existing relative import resolution works unchanged

Stored path validation rules:
- No `..` components in stored filenames (resolved at pack time)
- No absolute paths
- After packing, every quoted `#import` in every archived file must resolve to another file in the archive (self-containment check for local imports)

### Size Limits

Metadata fields use `u16` (big-endian), giving a maximum of 65535 bytes per string and 65535 entries per array. Payload sizes use `u32` (big-endian), giving a maximum of ~4GB. These are conscious design choices--source archives should never approach these limits. Specific limits:
- Individual metadata string (name, version, etc.): 65535 bytes
- Metadata array entries (modules, dependencies): 65535 entries
- Individual file content in payload: ~4GB
- Total uncompressed payload: ~4GB
- Total compressed payload: ~4GB

### Safety

- All lengths are bounds-checked against remaining buffer size on read
- Maximum individual file size and total archive size limits prevent memory exhaustion
- Filenames are validated: no absolute paths, no `..` components

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
- `tl_tlib_valid_filename()`: Validates filenames (rejects absolute paths and `..` components)
- Unit tests in `src/tess/src/test_tlib.c`

**Note:** This was a simplified format without metadata fields. Phase 4 added the full format with name/version/modules/depends.

#### Phase 3: Pack/Unpack CLI Commands ✓

Implemented in `src/tess/src/tess_exe.c`:

```bash
tess pack <file1.tl> [file2.tl ...] -o output.tlib [-v]
tess unpack archive.tlib [-o output_dir] [--list] [-v]
```

High-level operations in `src/tess/src/tlib.c`:
- `tl_tlib_pack()`: Resolves imports, excludes stdlib, computes relative paths
- `tl_tlib_unpack()`: Extracts files or lists contents

**Limitations:** No `package.tl` support, no metadata in archive.

#### Phase 4: Archive Metadata ✓

Implemented in `src/tess/src/tlib.c` and `src/tess/include/tlib.h`.

```c
typedef struct {
    str name;              // package name (required)
    str author;            // author (may be empty)
    str version;           // version string (required)
    str *modules;          // array of public module names
    u16  module_count;
    str *depends;         // array of required dependencies ("Name=Version")
    u16  depends_count;
    str *depends_optional; // array of optional dependencies ("Name=Version")
    u16  depends_optional_count;
} tl_tlib_metadata;

typedef struct {
    char const *name;
    u32         name_len;
    byte const *data;
    u32         data_len;
} tl_tlib_entry;

typedef struct {
    tl_tlib_metadata metadata;
    tl_tlib_entry   *entries;
    u32              entries_count;
} tl_tlib_archive;
```

**Target binary format:** Metadata is stored uncompressed after the fixed header (magic + version) and before the payload sizes. All metadata uses u16 lengths: scalar fields (name, author, version) are u16 length-prefixed UTF-8 strings, and array fields (modules, depends, depends-optional) use a u16 element count followed by u16 length-prefixed strings. Only payload sizes use u32. This allows metadata inspection without decompressing the archive. A CRC32 checksum at the end covers the entire archive for corruption detection.

**CLI flags for testing** (temporary until `package.tl` support in Phase 5):

```bash
tess pack --name MyLib --pkg-version 1.0.0 --author "Alice" --modules "Foo,Bar" src/*.tl -o MyLib.tlib
```

- `--name` and `--pkg-version` are required (note: `--pkg-version` instead of `--version` to avoid collision with `-V/--version`)
- `--author` and `--modules` are optional (default to empty)
- `tess unpack --list` displays metadata followed by file list

**Unit tests** in `src/tess/src/test_tlib.c`:
- `test_metadata_roundtrip()` - all fields preserved through write/read cycle
- `test_metadata_empty_fields()` - optional fields handle empty strings correctly

#### Phase 5: Package Manifest (`package.tl`) ✓

Replaced the INI-like manifest parser with a `package.tl` DSL interpreter. The file uses a function-call DSL that is valid TL syntax, parsed by the existing TL parser (`parser_parse_all_toplevel_funcalls()`) and interpreted as DSL calls.

Implemented in `src/tess/src/manifest.c` with header `src/tess/include/manifest.h`:

- `tl_package_parse_file()`: Reads file, parses with TL parser, walks AST to extract metadata
- `extract_string()` / `extract_int()`: Helper functions to extract typed values from AST nodes
- String literals are unwrapped from the parser's `nfa("Str__from_literal__1", [c_string(value)])` representation

**DSL functions:**

| Function | Arguments | Required | Description |
|----------|-----------|----------|-------------|
| `format(n)` | 1 integer | Yes | DSL format version (currently 1). Must be first declaration. |
| `package(name)` | 1 string | Yes | Package name (duplicate rejected) |
| `version(ver)` | 1 string | Yes | Version string (duplicate rejected) |
| `author(name)` | 1 string | No | Author name or email |
| `export(mod, ...)` | 1+ strings | For `tess pack` | Exported module names (public API) |
| `depend(name, ver [, path])` | 2-3 strings | No | Required dependency |
| `depend_optional(name, ver [, path])` | 2-3 strings | No | Optional dependency |
| `depend_path(dir)` | 1 string | No | Dependency search path (accumulates) |

**Data structures** (in `manifest.h`): `tl_package_info`, `tl_package_dep`, `tl_package`

**Unit tests** in `src/tess/src/test_manifest.c` (16 tests):
- Basic package, minimal package, multiple depends, depend with path, multiple depend_paths, export with multiple args
- Error cases: missing format/package/version, format not first, unsupported format, unknown function, wrong arg count, non-string arg, duplicate package, duplicate version

#### Phase 5b: Package.tl Integration with Pack Command ✓

`tess pack` and `tess validate` auto-discover `package.tl` in the current working directory via `tl_package_parse_file()`. The `-m` flag and `--name`/`--author`/`--version`/`--modules` CLI flags were removed.

- `pack_files()` and `validate_files()` in `tess_exe.c` read `package.tl` from CWD
- Integration test: `test_pack_with_manifest` in `src/tess/src/test_tlib.c`

#### Phase 6: Module Discovery (partial) ✓

**Goal:** Discover module names from `#module` directives in source files.

**Completed: `#module` directive scanning.**

The existing directive scanner in `tess_exe.c` (`process_hash_directive()`) was extended to discover `#module` directives alongside `#import` directives. A `modules_seen` hashmap in the `state` struct tracks discovered modules. Module discovery respects conditional compilation (`#ifdef`/`#ifndef`/`#endif`) the same way imports do.

```c
// In process_hash_directive():
else if (!is_stdlib_file && str_eq(words.v[0], S("module"))) {
    str_hset_insert(&self->modules_seen, words.v[1]);
}
```

Discovered modules are printed in verbose mode during `pack_files()`.

**Not yet completed:**

The remaining parts of Phase 6 are listed under "Remaining Phases" below as Phase 6b.

---

#### Phase 6b: Module Discovery Integration with Pack ✓

**Detailed implementation plan:** [TLIB_PHASE_6B.md](TLIB_PHASE_6B.md)

**Goal:** Use discovered modules to validate against `package.tl` and enforce self-containment.

Implemented in two sub-phases (export scanning was removed — see Design Change Log):

1. **Module Validation** — `tl_source_scanner_validate()` cross-checks `export()` modules against discovered `#module` directives. Errors on missing modules, verbose-only listing of internal modules.
2. **Self-Containment** — `check_self_containment()` in `tlib.c` verifies every quoted `#import` resolves to another file in the archive. Uses shared `tl_source_scanner_collect_imports()` for correct string/comment handling.

Key infrastructure: callback-based `scan_directives()` core in `source_scanner.c` shared by the full scanner and the lightweight import collector. `validate` CLI command for standalone validation.

#### Phase 7: Basic Package Consumption ✓

**Goal:** Compile code that uses a package (without inter-package dependencies).

**Implementation:**

The compiler auto-discovers `package.tl` in the current working directory and loads dependencies from it:

```bash
tess exe main.tl -o main
```

The compilation process:
1. Auto-discover and parse `package.tl` (skip dependency loading if not found)
2. Load each `depend()` package via `tl_tlib_read()`, resolving from explicit paths (3-argument `depend()`) or `depend_path()` directories
3. Verify each loaded package's metadata version matches the `depend()` declaration exactly
4. Extract source files to a temp directory via `tl_tlib_extract()`
5. All modules from all packages (exported and internal) enter the single global namespace
6. Detect module name conflicts (duplicate `#module` across packages or between packages and local code)
7. Feed all source (local + package) into the existing compilation pipeline
8. Tree shaking removes unreferenced code

All package source is loaded upfront and unconditionally based on `depend()` declarations in `package.tl`. Package modules are accessed via existing qualified syntax (`Module.function()`), the same as local modules.

**Parser fix for cross-file nested modules:** Import resolution orders files depth-first (imported files before importers), which means a child module file (e.g., `internal.tl` with `#module MathUtils.Internal`) is parsed before its parent (`math.tl` with `#module MathUtils`). The parser's nested module validation (`#module Parent.Child` checks that `Parent` exists) originally only checked modules parsed so far, causing a `nested_module_parent_not_found` error. Fixed by passing the source scanner's pre-scanned module map (`known_modules`) to the parser, so it can validate against all modules discovered during directive scanning. This enables the `MathUtils.Internal` pattern shown in the end-to-end example.

Key functions:
- `tl_tlib_extract()` in `tlib.c`: Extracts archive entries to a directory, returns file paths
- `resolve_tlib_path()` in `tess_exe.c`: Searches `depend_path()` dirs or explicit path for `.tlib` files
- `load_package_deps()` in `tess_exe.c`: Parses `package.tl`, resolves/reads/version-checks/extracts dependencies
- `files_in_order()` modified to accept package files, scan them for directives, and include them in the compilation file list with deduplication

E2E tests in `test_tlib.c`: `test_e2e_basic_package`, `test_e2e_version_mismatch`, `test_e2e_dep_not_found`, `test_e2e_multi_file_library` (uses cross-file nested modules).

---

#### Phase 8: Inter-Package Dependencies ✓

**Goal:** Package A can declare and use Package B as a dependency.

**Implementation:**

**During pack (`pack_files()` in `tess_exe.c`):**
1. Validate declared dependencies exist and versions match before writing the archive
2. Write all `depend()` declarations to the archive's `depends` field
3. Optional dependencies (`depend_optional()`) are written to the `depends-optional` field

Dependencies are declared in `package.tl`, not auto-detected from source. If a producer declares a `depend()` they don't actually use, it is still recorded in the archive — consumers will need to provide it. This is accepted for simplicity; a future lint pass could warn about unused declarations.

**During compile (transitive dependency resolution in `load_package_deps()`):**
1. When loading a package, check its `depends` field (parsed via `parse_dep_string()`)
2. For each required dependency, search the consumer's `depend_path()` directories for `<PackageName>.tlib`
3. Read metadata and verify version matches exactly
4. Recurse: `resolve_dep_recursive()` loads transitive dependencies the same way
5. Detect cycles during resolution (A→B→A) via resolution stack, emit error listing the cycle path
6. Detect version conflicts in diamond dependencies (A needs C=1.0, B needs C=2.0) via loaded map
7. Deduplicate: diamond dependencies where both require the same version are loaded once

Key functions added to `tess_exe.c`:
- `parse_dep_string()`: Splits "Name=Version" archive metadata strings into components
- `resolve_dep_recursive()`: Recursive resolver with cycle detection (stack) and dedup/conflict detection (loaded hashmap)
- `dep_resolve_ctx`: Tracking struct with `loaded` map (str→str) and `stack` array

E2E tests in `test_tlib.c`:
- `test_e2e_transitive_deps`: A→B→C chain (LogLib→MathLib→App, exit code 42)
- `test_e2e_diamond_deps`: LibA and LibB both depend on BaseLib (loaded once, exit code 42)
- `test_e2e_circular_deps`: A→B→A cycle detection (compile error)
- `test_e2e_version_conflict`: Diamond with version mismatch (compile error)
- `test_e2e_missing_transitive_dep`: Transitive dep not available (compile error with helpful message)

---

#### Phase 9: Module-Level Access Control (Documentation Only) ✓

**Goal:** Verify that `export()` serves its documentation and validation purposes correctly.

**Implementation:**

Access control is **not enforced at compile time**. All modules from a package (exported and internal) enter the global namespace because exported modules may depend on internal ones. The `export()` declaration is advisory — it documents the public API and is validated during `tess pack` (ensuring exported modules exist), but the compiler does not prevent consumer code from accessing internal modules.

It is the producer's responsibility to name internal modules clearly (e.g., `MathUtils.Internal`) and to document the public API via `export()`. This is analogous to Python's `_private` naming convention — not enforced by the language, but a clear signal.

**Validation:**
- `test_e2e_internal_module_accessible`: Package with exported (MathPub) and internal (MathInt) modules; consumer accesses both; verifies internal modules enter the global namespace (exit code 42)
- `tess pack` validates that `export()` modules exist in source (covered by `test_source_scanner.c` validation tests)
- Tree shaking of unused internal modules: not testable in current framework (removed code leaves no observable artifact)

#### Phase 10: Comprehensive Test Suite ✓

Final validation and edge case coverage. Audit confirmed comprehensive coverage across 75+ tests in 3 test files.

**Unit tests (all covered):**
- `.tlib` write/read roundtrip: 10 tests (roundtrip, empty, validation, byte order, large payload, metadata, unicode, corruption, CRC32)
- `package.tl` parsing: 17 tests in `test_manifest.c` (valid packages, missing fields, malformed DSL, all error cases)
- Filename validation: `test_filename_validation` (rejects `..` escapes, absolute paths, backslashes)
- Module discovery: 29 tests in `test_source_scanner.c` (simple, conditional, edge cases, string/comment handling)

**Integration tests (all covered):**
- End-to-end: `test_e2e_basic_package`, `test_e2e_multi_file_library` (pack → consume → run)
- Module name conflict detection: `test_e2e_module_conflict` (two packages defining "Utils" → compile error)
- Pack dependency verification: `test_e2e_version_mismatch`, `test_e2e_dep_not_found`
- Compile dependency verification: `test_e2e_missing_transitive_dep`, `test_e2e_version_conflict`
- Internal module accessible: `test_e2e_internal_module_accessible` (not enforced, both modules usable)
- Generics across packages: `test_e2e_generic_package` (polymorphic identity/add specialize correctly in consumer)
- Circular dependency detection: `test_e2e_circular_deps` (A→B→A → error)
- Transitive dependencies: `test_e2e_transitive_deps` (A→B→C chain)
- Diamond dependencies: `test_e2e_diamond_deps` (A,B both depend on C, loaded once)

**Build system:**
- All tests in `test_tlib.c` — no new test files needed, both Makefile and CMakeLists.txt already build `test_tlib`

---

### Phase Dependencies

```
Phase 5 (Package.tl Parser) ✓
    ↓
Phase 5b (Package.tl Integration) ✓
    ↓
Phase 6 (Module Discovery — scanning) ✓
    ↓
Phase 6b (Module Discovery — pack integration) ✓
    ↓
Phase 7 (Basic Consumption) ✓  ←── First end-to-end validation
    ↓
Phase 8 (Inter-Package Dependencies) ✓
    ↓
Phase 9 (Module Access Control — documentation only) ✓
    ↓
Phase 10 (Test Suite) ✓
```

**Key validation points:**
- After Phase 5: `package.tl` parser works standalone with full test coverage
- After Phase 6/6b: Module discovery, validation, and self-containment checking complete
- After Phase 7: Can pack and consume a simple library (with version verification)
- After Phase 8: Can handle library chains with transitive resolution (A uses B uses C) ✓
- After Phase 9: Export declarations advisory, internal modules accessible, pack validates exports ✓
- After Phase 10: Comprehensive test suite covering all edge cases (75+ tests) ✓

Each phase can be merged independently, allowing incremental progress and early feedback on the design

---

## Open Questions

### Resolved

- **Package metadata format**: Use a `package.tl` file with a function-call DSL that is valid TL syntax. Parsed by the existing TL parser, interpreted as DSL. Replaces the previous INI-like manifest format. See Design Change Log.

- **Circular dependencies**: Error at pack time. When building the dependency graph, detect cycles and report an error listing the cycle path (e.g., "circular dependency: A → B → C → A").

- **Format version compatibility**: Keep v1 during development. The format is internal/experimental; no backwards compatibility needed.

- **Symbol-level access control (`[[export]]`)**: Removed. Access control is purely at the module level via `export()` in `package.tl`. All symbols in an exported module are accessible. Authors control API surface by organizing code into public vs internal modules (e.g., `MathUtils` vs `MathUtils.Internal`). This is simpler and encourages clean module boundaries. See Design Change Log.

- **Unquoted `#import ModuleName`**: Removed. Package modules are loaded automatically from `depend()` declarations in `package.tl` and accessed via qualified syntax (`Module.function()`). No explicit import needed. See Design Change Log.

- **Transitive dependency version conflicts**: Error on conflict. The single global module namespace means two versions of the same package cannot coexist--strict equality is a structural requirement, not a simplification. See "Single Global Module Namespace" section.

### Open

- **Hash algorithm**: For future hash verification, which algorithm? SHA-256 is standard but produces long hashes. Consider:
  - SHA-256 truncated to 16 bytes (32 hex chars)
  - BLAKE3 (faster, modern)

  This is not blocking—hash verification is a future extension.

- **Module naming conflicts**: What error message when two packages define the same module? Should we include package paths in the error? Example: "module 'Utils' defined in both 'libs/A.tlib' and 'libs/B.tlib'"

- **Optional dependencies for consumers**: How are `depend_optional()` declarations used during compilation? Options:
  1. Consumer must explicitly enable them (e.g., `-D USE_WINAPI`)
  2. Auto-detect based on platform
  3. They're "optional" only for the producer — consumer provides what the package actually needs via their own `depend()`

  Recommendation: Option 3 for simplicity. Optional means "producer doesn't always use this dep"; consumer provides what the package actually needs.

---

## Design Change Log

### Removed: `[[export]]` Symbol-Level Access Control

**Change:** Removed `[[export]]` as a package visibility mechanism. Access control is now purely at the module level via `export()` in `package.tl`. All symbols within an exported module are accessible to consumers.

**Rationale:** Simpler model — authors control their API surface by organizing code into public modules (declared in `export()`) and internal modules (not declared, e.g., `MathUtils.Internal`). No parser/AST changes needed for export attributes, no type inference changes to check export status on qualified access.

**Phases affected:**
- Phase 6b simplified (removed sub-phases 2 and 3)
- Phase 7 simplified (no "is package source" flag needed)
- Phase 9 (was Phase 10) simplified (listed modules → all symbols accessible, no deferred symbol check)
- Phase 11 (Symbol-Level Access Control) eliminated entirely
- Test Suite renumbered to Phase 10

**Implementation cleanup (done):**
- Removed `[[export]]` state machine scanning and `out_has_export` parameter from `scan_directives()` in `source_scanner.c`
- Removed `export_seen` hashset from scanner state
- Removed export warning logic and associated tests in `test_source_scanner.c`
- The `[[export]]` attribute syntax remains valid in the language's general attribute system; it simply has no special meaning for package visibility

### Replaced: INI Manifest with `package.tl` DSL

**Change:** Replaced the INI-like `manifest.toml` with a `package.tl` file at the package root. The file uses a function-call DSL (`package()`, `version()`, `export()`, `depend()`, etc.) that is valid TL syntax, parsed by the existing TL parser and interpreted as a DSL.

**Rationale:** Follows the `go.mod` model — every package (including consumer applications) has a single metadata file at its root. Using TL syntax means no separate parser is needed; the existing TL parser produces AST nodes that are interpreted as DSL calls. Auto-discovery from the current working directory simplifies the CLI (no `-m` flag).

**Phases affected:**
- Phase 5 (Manifest Parser) — full rewrite: INI parser replaced by TL parser + AST interpreter
- Phase 5b (Manifest Integration) — `-m` flag replaced by auto-discovery
- Phase 7 onward — all references to "manifest" updated to `package.tl`

**Implementation cleanup (done):**
- Replaced `manifest.c` and `manifest.h` with `package.tl` DSL interpreter
- Replaced tests in `test_manifest.c` with 16 new `package.tl` tests
- Removed `-m` flag handling from `tess_exe.c`
- Updated types to `tl_package` / `tl_package_info` / `tl_package_dep`

### Removed: `#import ModuleName` (Unquoted Package Import)

**Change:** Removed the unquoted `#import ModuleName` syntax for package module imports. Package modules are now available automatically when declared via `depend()` in `package.tl`. Consumer code accesses them via existing qualified syntax (`Module.function()`).

**Rationale:** `package.tl` already declares all dependencies via `depend()`. The compiler loads all package source upfront based on these declarations — there is no lazy loading triggered by imports. The unquoted `#import` was redundant: it didn't trigger loading (that's `package.tl`'s job), and it didn't add information the compiler didn't already have. Removing it means no tokenizer/parser changes are needed for package support, and the import system stays simple with two file-based forms (`"file.tl"` and `<file.tl>`).

**Phases affected:**
- Phase 7 simplified (no unquoted import resolution, no parser changes)
- Phase 8 simplified (dependencies come from `package.tl` declarations, not detected from imports)
- Phase 9 (was Phase 10) simplified (access control is not enforced — all modules are loaded, `export()` is advisory/documentary, internal modules are removed by tree shaking when unused)
- Phase 9 (was Phase 9, Package.tl-Based Compilation) eliminated — content merged into Phases 7 and 8

### Removed: `-l` and `-L` CLI Flags

**Change:** Removed `-l` (load package) and `-L` (library search path) CLI flags. All dependency information must come from `package.tl`. A `package.tl` is required for any build that uses packages.

**Rationale:** Simplifies the CLI and ensures a single source of truth for dependencies. Having two ways to specify dependencies (CLI flags vs `package.tl`) creates ambiguity about version verification behavior and complicates the compilation pipeline.

**Phases affected:**
- Phase 7 (Basic Consumption) — no longer adds `-l`/`-L` flags; loads from `package.tl` only
- Old Phase 9 (Package.tl Compilation) eliminated — merged into Phases 7 and 8

**Implementation cleanup (done):**
- `-l` and `-L` flags were never added; `package.tl` is the sole mechanism from the start
