# Tess Library Packing: Design Plan

## Summary

Add a `tess pack` command that bundles Tess source files into a `.tlib` **package** for distribution as a reusable library. A package contains one or more **modules** (declared with `#module`) plus metadata. Every package (including consumer applications) has a `package.tl` file at its root that declares metadata, exported modules, and dependencies using a function-call DSL that is valid TL syntax. The compiler auto-discovers `package.tl` in the current working directory. The compiler performs whole-program compilation as usual. Access control is at the module level: the `export()` declarations in `package.tl` control which modules consumers can import, and all symbols within those modules are accessible.

This is distinct from C-compatible shared libraries (`tess lib` producing `.so`/`.dll`), which remain unchanged.

---

## Terminology

- **Module**: A namespace declared with `#module Name` in a `.tl` source file. Typically one file defines one module. Module members are accessed as `Module.function()`.
- **Package**: A `.tlib` archive containing one or more modules plus metadata. Packages bundle modules for distribution but have no name visible to source code--only the modules inside are visible.

**Notes:**
- Every `.tl` file MUST have a `#module` directive before any definitions. The parser will error if it encounters a definition before seeing `#module`.
- Module names are discovered at load time by parsing `#module` declarations (because `#module` directives may be conditional inside `#ifdef`/`#endif`). The `export()` declarations in `package.tl` separately declare which discovered modules are *public*--this is for access control, not discovery.

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

Three forms of `#import` serve different purposes:

| Syntax | Purpose | Resolution |
|--------|---------|------------|
| `#import "file.tl"` | Local file import | Relative to importing file, then `-I` paths |
| `#import <file.tl>` | Standard library import | Standard library paths (`-S` paths) |
| `#import ModuleName` | Package module import | Modules from included packages (via `package.tl`) |

The unquoted form (`#import ModuleName`) is **only** for modules provided by packages. Local project modules must use the quoted form with a file path.

### Package Dependencies

When `tess pack` encounters an import that resolves to a module from another package (not a local file), it records that package as a dependency in the `.tlib` archive metadata. The source from external packages is **not** bundled--only local source files are included.

Consumers only need to declare their **direct** dependencies in `package.tl` via `depend()`. When the compiler loads a package, it reads the archive's dependency metadata and automatically resolves transitive dependencies from `depend_path()` directories, verifying version equality. See Phases 8 and 9 for the full resolution algorithm.

The binary encoding of dependencies in the `.tlib` archive is described in the Archive Format section.

### Module-Level Access Control

Access control is purely at the module level. The `export()` declarations in `package.tl` declare which modules are part of the package's public API. All symbols within an exported module are accessible to consumers. Modules not exported are internal and cannot be imported by consumer code.

Library authors control their API surface by organizing code into public and internal modules. For example, a package might expose `MathUtils` while keeping `MathUtils.Internal` unlisted. This is simpler than symbol-level visibility and encourages clean module boundaries.

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

The compiler auto-discovers `package.tl` in the current working directory. Package names and module names are independent (they may differ or be the same). Package names appear in `package.tl` and dependency metadata. Module names appear in source code (`#import Logger`, `Logger.error(...)`).

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

#import Logger    // package dependency (unquoted)

validate_range(lo, hi) {
  if lo > hi { fatal("invalid range") }
}

fatal(msg) {
  Logger.error(msg)    // use the Logger package
  c_exit(1)
  void
}
```

Note: Every file must have a `#module` directive. `MathUtils.Internal` is not listed in `export()`, so consumers cannot import it or access its symbols. All symbols in the exported `MathUtils` module are accessible to consumers.

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

Note: `MathUtils.Internal` is not listed in `export()`, so consumers cannot import it. `package.tl` references the **package name** ("LoggingLib"), while the source code uses the **module name** (`#import Logger`, `Logger.error(...)`).

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

#import MathUtils    // unquoted = package module
// #import MathUtils.Internal          // ERROR: not in package's export()

main() {
  x := MathUtils.clamp(150, 0, 100)
  c_printf("clamped: %d\n", x)    // prints "clamped: 100"

  y := MathUtils.lerp(0.0, 10.0, 0.5)
  c_printf("lerp: %f\n", y)       // prints "lerp: 5.0"

  // MathUtils.Internal.fatal("x")        // ERROR: MathUtils.Internal not in export()

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

A `package.tl` is required for any build that uses packages. There are no `-l` or `-L` CLI flags — all dependency information comes from `package.tl`.

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

#### Phase 5: Package Manifest (`package.tl`) — needs rewrite

**Previous implementation:** INI-like manifest parser in `manifest.c`/`manifest.h` with 14 tests in `test_manifest.c`. This is being replaced (see Design Change Log).

**New design:** `package.tl` is a file at the package root using a function-call DSL that is valid TL syntax. The existing TL parser parses it into AST nodes, and a DSL interpreter walks the AST to extract metadata.

**Format:**

```tl
format(1)
package("MathUtils")
version("1.0.0")
author("Alice")
export("MathUtils", "OtherModule")

depend("LoggingLib", "2.0.0")
depend("OtherLib", "1.2.3", "./vendor/OtherLib.tlib")
depend_optional("WinAPI", "1.0.0")
depend_path("./libs")
depend_path("./vendor")
```

**DSL functions:**

| Function | Arguments | Required | Description |
|----------|-----------|----------|-------------|
| `format(n)` | 1 integer | Yes | DSL format version (currently 1). Checked first; error if unsupported. |
| `package(name)` | 1 string | Yes | Package name |
| `version(ver)` | 1 string | Yes | Version string |
| `author(name)` | 1 string | No | Author name or email |
| `export(mod, ...)` | 1+ strings | For `tess pack` | Exported module names (public API) |
| `depend(name, ver)` | 2 strings | No | Required dependency (name + version) |
| `depend(name, ver, path)` | 3 strings | No | Required dependency with explicit path override |
| `depend_optional(name, ver)` | 2 strings | No | Optional dependency |
| `depend_optional(name, ver, path)` | 3 strings | No | Optional dependency with explicit path override |
| `depend_path(dir)` | 1 string | No | Add a dependency search path (accumulates) |

**Parsing approach:**
1. Parse `package.tl` with the existing TL parser to produce an AST
2. Walk the top-level AST nodes, expecting only function call expressions
3. Check `format()` first — error if missing or unsupported version
4. Interpret each remaining call by name: extract string literal arguments, populate metadata struct
5. Error on unknown function names, wrong argument counts, or non-string arguments (except `format()` which takes an integer)

**Auto-discovery:** The compiler looks for `package.tl` in the current working directory when running `tess pack` or `tess exe`. If found, metadata and dependencies are loaded from it. `package.tl` is required for any build that uses packages — there are no `-l`/`-L` CLI flags. If `package.tl` is not found and the source code uses `#import ModuleName` (unquoted package imports), the compiler emits an error.

**Data structures** (replacing the old `tl_manifest` types):

```c
typedef struct {
    u32  format;          // from format(), currently 1
    str  name;            // from package()
    str  version;         // from version()
    str  author;          // from author(), may be empty
    str *exports;         // from export() calls
    u32  export_count;
    str *depend_paths;    // from depend_path() calls
    u32  depend_path_count;
} tl_package_info;

typedef struct {
    str name;             // dependency package name
    str version;          // required version
    str path;             // optional explicit path override (may be empty)
} tl_package_dep;

typedef struct {
    tl_package_info  info;
    tl_package_dep  *deps;
    u32              dep_count;
    tl_package_dep  *optional_deps;
    u32              optional_dep_count;
} tl_package;
```

**Required fields:**
- `format()` is always required and must be the first call
- `package()` and `version()` are always required
- `export()` is required for `tess pack`, not required for `tess exe`
- All other fields are optional

**Unit tests** (replacing the 14 tests in `test_manifest.c`):
- Basic package with all fields
- Minimal package (required fields only)
- Multiple `depend()` and `depend_optional()` calls
- Multiple `depend_path()` calls accumulate
- `depend()` with 3-argument path override
- Missing required fields (format, package, version) produce errors
- `format()` must be first call; error if not
- Unsupported format version produces error
- Unknown function names produce errors
- Wrong argument counts produce errors
- Non-string arguments produce errors
- `export()` with multiple arguments

#### Phase 5b: Package.tl Integration with Pack Command — needs rewrite

**Previous implementation:** `-m manifest.toml` flag in `tess_exe.c`. Being replaced by auto-discovery (see Design Change Log).

**New design:** `tess pack` auto-discovers `package.tl` in the current working directory:

```bash
tess pack src/math.tl -o MathUtils.tlib
```

When `package.tl` is found, metadata is read from it. `depend()` declarations are parsed but not validated (dependencies aren't loaded yet). The `-m` flag is removed.

- Remove support for `-m`, `--name`, `--author`, `--version`, and `--modules` flags
- Integration test: `test_pack_with_package_tl` in `src/tess/src/test_tlib.c`

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

---

### Remaining Phases

The remaining work is organized into phases that build incrementally. Each phase has clear validation criteria and can be tested before proceeding.

#### Phase 7: Basic Package Consumption

**Goal:** Compile code that uses a package (without inter-package dependencies).

**Implementation:**

The compiler auto-discovers `package.tl` in the current working directory and loads dependencies from it:

```bash
tess exe main.tl -o main
```

The compilation process:
1. Auto-discover and parse `package.tl`
2. Load each `depend()` package via `tl_tlib_read()`, resolving from explicit paths or `depend_path()` directories
3. Extract source files into arena-allocated buffers
3. All modules from all packages enter the single global namespace
4. Build two sets from package metadata and source scanning:
   - **All package modules**: every module discovered from package source via `#module` directives
   - **Exported modules**: modules declared in the package's `export()` (the public API)
5. Detect module name conflicts (duplicate `#module` across packages or between packages and local code)
6. Feed all source (local + package) into the existing compilation pipeline
7. Tree shaking removes unreferenced code

All package source is loaded upfront and unconditionally — there is no lazy loading triggered by imports. `#import ModuleName` (unquoted) is a declaration of intent to use a package module, resolved against the exported modules set, not a trigger to load package files.

**No file→package provenance tracking.** The two module sets (all package modules vs exported modules) enable module-level access control (Phase 10). No per-file "is package source" flag is needed since there is no symbol-level access control.

**Unquoted import resolution:**

Modify the tokenizer/parser to recognize `#import ModuleName` (no quotes, no angle brackets) as a package module import. The import resolver checks that `ModuleName` exists in the exported modules set. If the module exists in all package modules but is not exported, it is an internal module and the import is rejected. If not found at all, an error is emitted.

**Validation:**
- Integration test: Create a simple package, compile a consumer that imports it
- Verify the end-to-end example from this document works (without LoggingLib dependency)

#### Phase 8: Inter-Package Dependencies

**Goal:** Package A can declare and use Package B as a dependency.

**Implementation:**

**During pack:**
1. Load dependency packages from `package.tl`'s `depend()` declarations (resolved via `depend_path()` or explicit path)
2. Verify each package's version matches `depend()` declaration
3. Scan dependencies to discover their modules
4. Build module → package mapping for dependencies
5. When packing source uses `#import ModuleName`:
   - If `ModuleName` is from a dependency, record that dependency
6. Write used dependencies to archive's `depends` field
7. Error if a required dependency is declared but never used
8. Optional dependencies may be unused

**During compile (transitive dependency resolution):**
1. When loading a package, check its `depends` field
2. For each required dependency, search `depend_path()` directories for `<PackageName>.tlib`
3. Read metadata and verify version matches exactly
4. Recurse: load the transitive dependency's own `depends` the same way
5. Detect cycles during resolution (A→B→A) and emit an error listing the cycle path
6. Error on missing dependencies with helpful message: `"package 'MathUtils' depends 'LoggingLib=2.0.0', not found in library search paths"`

**Validation:**
- Integration test: MathUtils→LoggingLib example from this document
- Test version mismatch detection
- Test missing transitive dependency detection (helpful error message)
- Test transitive dependency auto-resolution from `depend_path()`
- Test circular dependency detection

#### Phase 9: Package.tl-Based Compilation

**Goal:** `tess exe` auto-discovers `package.tl` and uses it for version-verified compilation.

**Implementation:**

```bash
tess exe src/main.tl -o main
```

The compiler auto-discovers `package.tl` in the current working directory:
1. Parse `package.tl` for `depend()` declarations (direct dependencies only)
2. Load packages from explicit paths or resolve from `depend_path()` directories
3. Verify versions match `depend()` declarations
4. Auto-resolve transitive dependencies from `depend_path()` directories

Consumers only list direct dependencies via `depend()`. Transitive dependencies are resolved automatically from `depend_path()`. This enables reproducible builds with pinned versions for direct dependencies while keeping `package.tl` concise.

**Validation:**
- Integration test: compile with `package.tl`, verify version checking
- Test transitive dependency auto-resolution from `depend_path()`
- Test dependency resolution with explicit path (3-argument `depend()`)
- Test error when `package.tl` is missing but package imports are used

#### Phase 10: Module-Level Access Control

**Goal:** Consumers can only import modules listed in a package's `export()` declarations.

**Implementation:**

Access control uses two sets built during package loading (Phase 7):
- **All package modules**: every module discovered from package source
- **Exported modules**: modules declared in packages' `export()` declarations

**Unquoted import resolution** (for local files):
1. If `ModuleName` is in the exported modules set → allowed
2. If `ModuleName` is in all package modules but not exported → error: "module 'X' is not a public module"
3. If `ModuleName` is not found → error: "module 'X' not found"

**Qualified access** (`Module.symbol`) in local files:
- If `Module` is in all package modules but not in exported modules → error (accessing internal package module)
- If `Module` is in exported modules → allowed (all symbols accessible)
- If `Module` is not in package modules → local module, no restrictions

Package source files are unrestricted — they can access any module freely, including unlisted modules from other packages.

**Known limitation:** Access control is not enforced between packages — only between consumer (local) code and packages.

**Validation:**
- Integration test: attempt to import internal module, verify error
- Test that internal modules work within the package
- Test that local modules remain accessible without restrictions
- Test that qualified access to an unlisted package module from local code is rejected

#### Phase 11: Comprehensive Test Suite

Final validation and edge case coverage:

**Unit tests:**
- `.tlib` write/read roundtrip (correct format, corruption detection)
- Metadata field roundtrip (all fields, empty fields, special characters)
- `package.tl` parsing (valid packages, missing fields, malformed DSL)
- Filename validation (rejects `..` escapes, absolute paths)
- Module discovery (simple, conditional, edge cases)

**Integration tests:**
- End-to-end: pack multi-file library → compile consumer → run
- Module name conflict detection across packages
- Pack dependency verification (missing dep, unused required dep, unused optional ok, version mismatch)
- Compile dependency verification (missing package, version mismatch, transitive dep missing)
- Module access control (import non-public module → error)
- Generics in a package specialize correctly in the consumer
- Circular dependency detection (package A requires B, B requires A → error)

**Build system:**
- All tests added to both Makefile and CMakeLists.txt
- CI verification on multiple platforms

---

### Phase Dependencies

```
Phase 5 (Package.tl Parser) — needs rewrite
    ↓
Phase 5b (Package.tl Integration) — needs rewrite
    ↓
Phase 6 (Module Discovery — scanning) ✓
    ↓
Phase 6b (Module Discovery — pack integration) ✓
    ↓
Phase 7 (Basic Consumption)  ←── First end-to-end validation
    ↓
Phase 8 (Dependencies)
    ↓
Phase 9 (Package.tl Compilation)
    ↓
Phase 10 (Module Access Control)
    ↓
Phase 11 (Test Suite)
```

**Key validation points:**
- After Phase 5: `package.tl` parser works standalone with full test coverage
- After Phase 6/6b: Module discovery, validation, and self-containment checking complete
- After Phase 7: Can pack and consume a simple library (no dependencies)
- After Phase 8: Can handle library chains (A uses B)
- After Phase 10: Full access control model working (module-level)

Each phase can be merged independently, allowing incremental progress and early feedback on the design

---

## Open Questions

### Resolved

- **Package metadata format**: Use a `package.tl` file with a function-call DSL that is valid TL syntax. Parsed by the existing TL parser, interpreted as DSL. Replaces the previous INI-like manifest format. See Design Change Log.

- **Circular dependencies**: Error at pack time. When building the dependency graph, detect cycles and report an error listing the cycle path (e.g., "circular dependency: A → B → C → A").

- **Format version compatibility**: Keep v1 during development. The format is internal/experimental; no backwards compatibility needed.

- **Symbol-level access control (`[[export]]`)**: Removed. Access control is purely at the module level via `export()` in `package.tl`. All symbols in an exported module are accessible. Authors control API surface by organizing code into public vs internal modules (e.g., `MathUtils` vs `MathUtils.Internal`). This is simpler and encourages clean module boundaries. See Design Change Log.

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
- Phase 10 simplified (listed modules → all symbols accessible, no deferred symbol check)
- Phase 11 (Symbol-Level Access Control) eliminated entirely
- Old Phase 12 (Test Suite) renumbered to Phase 11

**Implementation cleanup required:**
- Remove `[[export]]` state machine scanning in `source_scanner.c`
- Remove `export_seen` hashset from scanner state
- Remove export warning logic (public module with no exports, non-public module with exports)
- Remove associated tests in `test_source_scanner.c`
- The `[[export]]` attribute syntax remains valid in the language's general attribute system; it simply has no special meaning for package visibility

### Replaced: INI Manifest with `package.tl` DSL

**Change:** Replaced the INI-like `manifest.toml` with a `package.tl` file at the package root. The file uses a function-call DSL (`package()`, `version()`, `export()`, `depend()`, etc.) that is valid TL syntax, parsed by the existing TL parser and interpreted as a DSL.

**Rationale:** Follows the `go.mod` model — every package (including consumer applications) has a single metadata file at its root. Using TL syntax means no separate parser is needed; the existing TL parser produces AST nodes that are interpreted as DSL calls. Auto-discovery from the current working directory simplifies the CLI (no `-m` flag).

**Phases affected:**
- Phase 5 (Manifest Parser) — full rewrite: INI parser replaced by TL parser + AST interpreter
- Phase 5b (Manifest Integration) — `-m` flag replaced by auto-discovery
- Phase 7 onward — all references to "manifest" updated to `package.tl`

**Implementation cleanup required:**
- Replace `manifest.c` and `manifest.h` with new `package.tl` interpreter
- Replace all 14 tests in `test_manifest.c` with new `package.tl` tests
- Remove `-m` flag handling from `tess_exe.c`
- Update `tl_manifest` / `tl_manifest_package` / `tl_manifest_dep` types to `tl_package` / `tl_package_info` / `tl_package_dep`

### Removed: `-l` and `-L` CLI Flags

**Change:** Removed `-l` (load package) and `-L` (library search path) CLI flags. All dependency information must come from `package.tl`. A `package.tl` is required for any build that uses packages.

**Rationale:** Simplifies the CLI and ensures a single source of truth for dependencies. Having two ways to specify dependencies (CLI flags vs `package.tl`) creates ambiguity about version verification behavior and complicates the compilation pipeline.

**Phases affected:**
- Phase 7 (Basic Consumption) — no longer adds `-l`/`-L` flags; loads from `package.tl` only
- Phase 9 (Package.tl Compilation) — no longer needs to check for `-l`/`-L` and `package.tl` conflicts

**Implementation cleanup required:**
- Remove `-l` and `-L` flag parsing from `tess_exe.c`
- Remove "without manifest" code paths
