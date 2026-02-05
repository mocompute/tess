# Tess Library Packing: Design Plan

## Summary

Add a `tess pack` command that bundles Tess source files into a `.tlib` **package** for distribution as a reusable library. A package contains one or more **modules** (declared with `#module`) plus metadata. Consumers include packages via a manifest file (with paths and version verification) or via `-l` flags (without version verification). The compiler performs whole-program compilation as usual. The `[[export]]` attribute controls which symbols are part of the library's public API.

This is distinct from C-compatible shared libraries (`tess lib` producing `.so`/`.dll`), which remain unchanged. A future `[[c_export]]` attribute may be introduced for C-compatible symbol export.

---

## Terminology

- **Module**: A namespace declared with `#module Name` in a `.tl` source file. Typically one file defines one module. Module members are accessed as `Module.function()`.
- **Package**: A `.tlib` archive containing one or more modules plus metadata. Packages bundle modules for distribution but have no name visible to source code--only the modules inside are visible.

**Notes:**
- Not every `.tl` file needs a `#module` directive. Files without one are internal implementation files that become part of the translation unit.
- Module names are discovered at load time by parsing `#module` declarations. They cannot be cached in package metadata because `#module` directives may be conditional (inside `#ifdef`/`#endif`).

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

## `.tlib` Archive Format

A custom binary format, chosen over tar for simplicity, security, and zero external dependencies (aside from libdeflate for compression).

### Layout

```
[4 bytes]  Magic: "TLIB"
[4 bytes]  Format version: 1 (little-endian uint32)
[4 bytes]  Name length (little-endian uint32)
[N bytes]  Package name (UTF-8 string)
[4 bytes]  Author length (little-endian uint32)
[N bytes]  Author (UTF-8 string, may be empty)
[4 bytes]  Version length (little-endian uint32)
[N bytes]  Version (UTF-8 string)
[4 bytes]  Requires length (little-endian uint32)
[N bytes]  Requires (UTF-8 string, comma-separated dependencies, may be empty)
[4 bytes]  Requires-optional length (little-endian uint32)
[N bytes]  Requires-optional (UTF-8 string, comma-separated dependencies, may be empty)
[4 bytes]  Uncompressed payload size (little-endian uint32)
[4 bytes]  Compressed payload size (little-endian uint32)
[N bytes]  Deflate-compressed payload
```

### Metadata Fields

| Field | Required | Description |
|-------|----------|-------------|
| Name | Yes | Package name (e.g., "MyLibrary") |
| Author | No | Author name or email |
| Version | Yes | Version string, free-form (compared as literal string) |
| Requires | No | Comma-separated list of required versioned dependencies |
| Requires-optional | No | Comma-separated list of optional versioned dependencies |

**Dependency format:** `PackageName=Version[#Hash]`

Examples:
- `Utils=1.2.0` -- requires Utils version 1.2.0
- `Utils=1.2.0,Logger=0.5.3` -- multiple dependencies
- `Utils=1.2.0#a1b2c3d4...` -- with hash verification (future)

### Payload (before compression)

```
[4 bytes] File count (little-endian uint32)
For each file:
    [4 bytes] Filename length (little-endian uint32)
    [N bytes] Filename (UTF-8, relative path)
    [4 bytes] Content length (little-endian uint32)
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

### Phase 1: Vendor libdeflate

- Vendor libdeflate source into `vendor/libdeflate/`
- Integrate into both Makefile and CMakeLists.txt
- Only the compressor and decompressor are needed

### Phase 2: `.tlib` Archive Format + Reader/Writer

Implement a small module (likely in `src/tess/`):

```c
typedef struct {
    str name;
    str author;
    str version;
    str requires;           // comma-separated required dependencies
    str requires_optional;  // comma-separated optional dependencies
} tlib_metadata;

typedef struct {
    str filename;
    str content;
} tlib_file;

typedef struct {
    tlib_metadata metadata;
    tlib_file *files;
    int file_count;
} tlib_archive;
```

- `tlib_write(allocator, metadata, files[], count, output_path) -> int`
  - Writes magic and format version
  - Writes each metadata field as length-prefixed string
  - Serializes file entries into the payload format
  - Deflate-compresses the payload
  - Writes compressed payload to output file

- `tlib_read(allocator, input_path) -> tlib_archive`
  - Reads and validates magic and format version
  - Reads length-prefixed metadata strings
  - Decompresses the payload
  - Parses file entries with bounds checking
  - Returns metadata + array of (filename, content) pairs

Unit tests for write/read roundtrip.

### Phase 3: `tess pack` Command

Add a new subcommand to `tess_exe.c`:

1. Parse the manifest file (TOML format)
2. Load dependency packages from paths specified in manifest
3. Verify each package's version matches manifest declaration
4. Parse each dependency package to discover module names
5. Build module → package mapping
6. Parse the input `.tl` file(s)
7. Resolve the full import graph, using module → package mapping for unquoted imports
8. Verify manifest dependencies match what the code actually imports (see verification below)
9. Exclude standard library files
10. Collect all local `.tl` source files (not from packages)
11. Write them into a `.tlib` with metadata via the Phase 2 writer

No compilation or type checking occurs. This is purely source collection and archiving.

**Manifest file:**

The manifest is a TOML file specifying package metadata:

```toml
# MyLibrary package manifest

[package]
name = "MyLibrary"
version = "1.0.0"
author = "Jane Doe"

[dependencies]
Utils = { version = "1.2.0", path = "libs/Utils.tlib" }
Logger = { version = "0.5.3", path = "libs/Logger.tlib" }

# Platform-specific dependencies (not required to be used)
[dependencies.optional]
WinAPI = { version = "0.3.0", path = "libs/WinAPI.tlib" }
Posix = { version = "1.1.0", path = "libs/Posix.tlib" }
```

The manifest follows the [TOML specification](https://toml.io/). Paths are relative to the manifest file's location.

**Sections:**

| Section | Required | Description |
|---------|----------|-------------|
| `[package]` | Yes | Package metadata |
| `[dependencies]` | No | Required packages (must all be used) |
| `[dependencies.optional]` | No | Conditional packages (may be unused) |

**Package fields:**

| Field | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Package name |
| `version` | Yes | Version string (free-form) |
| `author` | No | Author name or email |

**Dependency fields:**

| Field | Required | Description |
|-------|----------|-------------|
| `version` | Yes | Exact version required |
| `path` | Yes | Path to `.tlib` file (relative to manifest) |

**Dependency verification during pack:**

The manifest declares dependencies with paths. The packer verifies consistency:

1. Load each package from its manifest path, verify version matches
2. Discover which modules each package contains
3. Build module → package mapping
4. For each unquoted `#import ModuleName` in the code:
   - Find which package provides `ModuleName`
   - Verify that package is declared in manifest (`[dependencies]` or `[dependencies.optional]`)
5. Verify every required dependency (`[dependencies]`) is actually used
6. Optional dependencies (`[dependencies.optional]`) may be unused without error

**CLI:**
```bash
tess pack manifest.toml foo.tl -o Foo.tlib
tess pack manifest.toml foo.tl bar.tl -o Foo.tlib
```

The manifest file is required. Dependency package paths are specified in the manifest (no `-l` flags).

### Phase 4: Package Consumption

Extend the compiler CLI and import resolution in `tess_exe.c`:

**CLI:**
```bash
tess exe main.tl -l Foo.tlib -l Bar.tlib -o main    # without manifest
tess exe manifest.toml main.tl -o main              # with manifest (no -l flags)
```

The manifest is optional for `tess exe`:
- Without manifest: use `-l` flags to provide packages, no version verification
- With manifest: package paths come from manifest, versions are verified, `-l` flags not allowed

**Compilation process:**

1. Load packages (from manifest paths, or from `-l` flags if no manifest)
2. Verify package versions match manifest declarations (if manifest provided)
3. Parse each package to discover module names (from `#module` declarations)
4. Build module → package mapping
5. Check for module name conflicts (across packages and local source)
6. Verify each package's own declared dependencies are satisfied
7. Extract source files from packages into arena-allocated buffers
8. Feed all source (local + package) into the existing compilation pipeline
9. The rest of the compiler (parsing, inference, transpilation) works unchanged
10. Tree shaking removes unreferenced code

**Consumer source:**
```tl
#import Util      // imports module Util from a package
#import "local.tl" // imports local file
```

### Phase 5: `[[export]]` Enforcement

When compiling a program that includes packages:

1. After parsing package source, identify all symbols with `[[export]]` attributes
2. During type inference, track which package-defined symbols are referenced by consumer code (local source files)
3. Error if consumer code references a non-`[[export]]` symbol from a package module
4. Package-internal symbols remain usable within the package's own source files

### Phase 6: Tests

- Unit test: `.tlib` write/read roundtrip (correct format, corruption detection)
- Unit test: metadata field roundtrip (name, author, version, requires)
- Unit test: manifest file parsing (valid TOML, comments, missing fields, malformed)
- Unit test: filename validation rejects `..` escapes and absolute paths
- Integration test: `tess pack` a multi-file library, then `tess exe` a consumer that imports it
- Integration test: module name conflict detection across packages
- Integration test: pack dependency verification (missing dep in manifest, unused required dep, unused optional ok, version mismatch)
- Integration test: compile dependency verification (missing `-l` package, version mismatch with manifest, transitive dep missing)
- Integration test: `[[export]]` enforcement--error when accessing non-exported symbol
- Integration test: generics in a package specialize correctly in the consumer
- Add all tests to both Makefile and CMakeLists.txt

---

## Open Questions

- **TOML parsing**: Vendor a TOML library, or implement a minimal subset parser? Features needed: `[section]`, `key = "value"`, inline tables `{ key = "value" }`, and comments.
- **`[[export]]` on types**: Should `[[export]]` work on struct/enum definitions, or only on functions and values?
- **Circular dependencies**: What if package A requires B and B requires A? Probably an error, but should be specified.
- **Hash algorithm**: For future hash verification, which algorithm? SHA-256 is standard but produces long hashes. Consider truncation or alternative (e.g., BLAKE3).
