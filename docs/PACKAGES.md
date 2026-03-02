# Packages

Tess supports distributing reusable libraries as `.tlib` **packages**. A package bundles one or more modules as a compressed source archive. Consumers declare dependencies in a `package.tl` file, and the compiler loads, version-checks, and compiles everything together via whole-program compilation.

This is distinct from C-compatible libraries (`tess lib` producing `.so`/`.dll`, or `tess lib --static` producing `.a`/`.lib`), which remain unchanged.

## Concepts

- **Module**: A namespace declared with `#module Name` in a `.tl` source file. Members are accessed as `Module.function()`. See [LANGUAGE_REFERENCE.md](LANGUAGE_REFERENCE.md).
- **Package**: A `.tlib` archive containing one or more modules plus metadata. Packages have a name (declared in `package.tl`) that is independent of the module names inside. Only the module names are visible in source code.

Packages are source-only archives. There is no pre-compilation or IR. The consumer extracts source and compiles everything together, which preserves whole-program compilation semantics and ensures generic functions specialize correctly.

## Quick Start

### Creating a package

```
mylib/
  package.tl
  src/
    math.tl
```

**src/math.tl:**
```tl
#module MathUtils

clamp(x, lo, hi) {
  if x < lo { lo }
  else if x > hi { hi }
  else { x }
}
```

**package.tl:**
```tl
format(1)
package(mylib)
version("1.0.0")
author("Alice")
export(MathUtils)
source("src/")
```

Note that the package name (`mylib`) and the module name (`MathUtils`) are independent. The package name appears in `package.tl` and dependency declarations; the module name appears in source code.

**Build the package:**
```bash
tess pack -o mylib.tlib
```

Note that `package.tl` is found automatically in the current working directory. The `source("src/")` declaration tells the compiler where to find source files, so no file arguments are needed on the command line. You can still list files explicitly (`tess pack src/math.tl -o mylib.tlib`), which overrides `source()`.

### Consuming a package

```
myapp/
  package.tl
  libs/
    mylib.tlib
  src/
    main.tl
```

**src/main.tl:**
```tl
#module main

// No #import needed for package modules. They load automatically from package.tl.

main() {
  x := MathUtils.clamp(150, 0, 100)
  c_printf("clamped: %d\n", x)
  0
}
```

**package.tl:**
```tl
format(1)
package(MyApp)
version("0.1.0")
source("src/")
depend(mylib, "1.0.0")
depend_path("./libs")
```

**Build:**
```bash
tess exe -o myapp
```

The compiler auto-discovers `package.tl` in the current working directory, resolves `source("src/")` to find source files, loads `mylib.tlib` from the `libs/` directory, verifies the version matches, and compiles everything together. Consumer code uses the *module* name (`MathUtils.clamp`), not the package name.

---

## `package.tl` Reference

Every project that produces or consumes packages needs a `package.tl` file at its root. The file uses a function-call DSL that is valid TL syntax. The compiler auto-discovers it in the current working directory.

Builds that do not use packages work without `package.tl` -- the compiler simply skips dependency loading when none is found.

### DSL Functions

| Function | Arguments | Required | Description |
|----------|-----------|----------|-------------|
| `format(n)` | 1 integer | Yes | DSL format version (currently `1`). Must be the first declaration. |
| `package(name)` | 1 identifier | Yes | Package name (must be unique; duplicates rejected) |
| `version(ver)` | 1 string | Yes | Version string (literal string comparison, not semver) |
| `author(name)` | 1 string | No | Author name or email |
| `export(mod, ...)` | 1+ identifiers | For `tess pack` | Modules that are part of the public API |
| `source(path, ...)` | 1+ strings | No | Source files or directories (directories scanned recursively for `*.tl`) |
| `depend(name, ver)` | identifier + string | No | Required dependency with version |
| `depend_optional(name, ver)` | identifier + string | No | Optional dependency with version |
| `depend_path(dir)` | 1 string | No | Directory to search for `.tlib` files (accumulates) |

### Example

```tl
format(1)
package(mylib)
version("1.0.0")
author("Alice")
export(MathUtils)
source("src/")

depend(logging_lib, "2.0.0")
depend_path("./libs")
```

### Notes

- `format(1)` must appear first.
- `package()` and `version()` cannot be declared more than once.
- Multiple `export()`, `source()`, `depend()`, `depend_optional()`, and `depend_path()` calls accumulate.
- `export()` accepts multiple arguments: `export(Mod1, Mod2)`.
- `source()` accepts multiple arguments: `source("src/", "extra/util.tl")`. Directory arguments are scanned recursively for `*.tl` files.
- Version strings are compared as literal strings, not as semantic versions. `"1.0.0"` and `"1.0"` are different versions.
- Unknown function calls are silently ignored. This allows `package.tl` to contain fields that older compiler versions do not recognize.

### Source Files

The `source()` function declares where the compiler should find source files. It accepts file paths and directory paths:

```tl
source("src/")              // scan src/ recursively for *.tl files
source("main.tl")           // add a single file
source("src/", "extra.tl")  // multiple args, mix of dirs and files
```

Multiple `source()` calls accumulate. Directories are scanned recursively for files ending in `.tl`.

When `source()` is declared, commands like `tess run`, `tess exe`, `tess pack`, and `tess validate` can be run without listing files on the command line:

```bash
tess run                    # uses source() from package.tl
tess exe -o myapp           # uses source() from package.tl
tess pack -o mylib.tlib     # uses source() from package.tl
tess validate               # uses source() from package.tl
```

**CLI override**: If files are given on the command line, they take priority and `source()` entries are ignored. A warning is printed to stderr:

```bash
tess exe -o myapp src/main.tl   # ignores source(), warns on stderr
```

Projects without `package.tl` or without `source()` work as before — files must be listed on the command line.

---

## Creating Packages

### The `tess pack` command

```bash
tess pack -o output.tlib [-v]
tess pack <file1.tl> [file2.tl ...] -o output.tlib [-v]
```

When no files are listed on the command line, `tess pack` uses `source()` entries from `package.tl` to find source files automatically. If files are given explicitly, they override `source()` (a warning is printed).

The command:
1. Reads `package.tl` from the current working directory
2. Discovers source files from `source()` entries (or uses CLI arguments)
3. Resolves all `#import` directives from the source files (recursively)
4. Excludes standard library files
5. Validates that all `export()` modules exist in the source
6. Checks self-containment: every `#import "file.tl"` resolves to another file in the archive
7. Compresses and writes the `.tlib` archive

### Multi-file packages

A package can contain multiple modules across multiple files. With `source()` in `package.tl`, all files are discovered automatically. Imported files are also included automatically via `#import` resolution:

```
mylib/
  package.tl
  src/
    math.tl         (#module MathUtils, imports internal.tl)
    internal.tl     (#module MathUtils.Internal)
```

```bash
tess pack -o MathUtils.tlib
```

Both `math.tl` and `internal.tl` are included -- `math.tl` is found via `source("src/")` and `internal.tl` is found because `math.tl` imports it.

### Validating a package

```bash
tess validate
```

Runs the same checks as `tess pack` without producing an archive. Reads `package.tl` from the current working directory and uses `source()` entries to discover files. Like `tess pack`, files can be listed explicitly on the command line to override `source()`.

### Inspecting and extracting an archive

```bash
tess pack --list archive.tlib                # Show metadata and file list
tess pack --unpack archive.tlib [-o outdir]  # Extract files
```

---

## Consuming Packages

### How dependency loading works

When you run `tess exe` (or `tess run`, `tess c`, `tess lib`), the compiler:

1. Looks for `package.tl` in the current working directory
2. Discovers local source files from `source()` entries (if no files given on the command line)
3. For each `depend()` declaration, searches `depend_path()` directories for `<PackageName>.tlib`
4. Reads the archive and verifies the version matches exactly
5. Extracts source files to a temporary directory
6. Recursively loads transitive dependencies from the archive's metadata
7. Scans all source for `#module` directives and checks for conflicts
8. Compiles everything together (local source + all package source)
9. Tree shaking removes unreferenced code

### No explicit import needed

Package modules are loaded automatically based on `depend()` declarations. Consumer code accesses them via the standard qualified syntax:

```tl
x := MathUtils.clamp(150, 0, 100)
Logger.warn("something happened")
```

No `#import` directive is needed for package modules. The two existing `#import` forms are for local files only:

| Syntax | Purpose |
|--------|---------|
| `#import "file.tl"` | Local file (relative to importing file, then `-I` paths) |
| `#import <file.tl>` | Standard library (standard library paths, `-S` paths) |

### There are no `-l` or `-L` flags

All dependency information comes from `package.tl`. There are no CLI flags for specifying packages.

---

## Dependencies

### Direct dependencies

Consumers only declare their **direct** dependencies in `package.tl`:

```tl
depend(mylib, "1.0.0")
depend_path("./libs")
```

### Transitive dependencies

The compiler automatically resolves transitive dependencies. If `mylib` depends on `logging_lib`, the compiler reads that from the `mylib.tlib` metadata and searches the consumer's `depend_path()` directories for `logging_lib.tlib`.

```
MyApp
  depends on mylib (declared in package.tl)
    depends on logging_lib (resolved automatically from mylib.tlib metadata)
```

The consumer must have all transitive dependencies available in their `depend_path()` directories. If a transitive dependency cannot be found, the compiler emits an error naming the missing package and which package requires it.

### Strict version equality

All modules share a single global namespace. Two versions of the same package cannot coexist because they would define the same module names. This means dependency versions use strict equality -- `"1.0.0"` must match exactly.

If package A requires `logging_lib=1.0.0` and package B requires `logging_lib=2.0.0`, the compiler emits an error. There is no way to satisfy both.

### Diamond dependencies

When packages A and B both depend on package C at the same version, C is loaded once. Type identity is naturally preserved because C's source is compiled exactly once into a single compilation unit.

### Circular dependencies

Circular dependencies (A depends on B, B depends on A) are detected and produce an error listing the cycle path.

---

## Module Access Control

The `export()` declaration in `package.tl` documents which modules are the package's public API. Access control is **not enforced** by the compiler. All modules from a package (exported and internal) enter the global namespace because exported modules may depend on internal ones.

It is the producer's responsibility to name internal modules clearly:

```tl
// package.tl
export(MathUtils)    // public API

// src/math.tl
#module MathUtils              // public

// src/internal.tl
#module MathUtils.Internal     // internal (not in export())
```

The `export()` declaration serves three purposes:
1. **Documentation** -- clearly communicates which modules are the public API
2. **Tooling** -- future linters and documentation generators can use it
3. **Pack validation** -- `tess pack` verifies that exported modules exist in the source

Tree shaking removes unused internal modules from the final binary.

---

## Module Naming

### Single global namespace

All modules -- whether from local source or packages -- share a single global namespace. If two packages define a module with the same name, or if local code conflicts with a package module, the compiler emits an error.

### Package names vs module names

Package names (in `package.tl`) and module names (in source code) are independent. They may be the same or different:

| | Package name | Module name(s) |
|---|---|---|
| **In `package.tl`** | `package(logging_lib)` | `export(Logger)` |
| **In source code** | not visible | `Logger.warn(...)` |
| **In `depend()`** | `depend(logging_lib, "2.0.0")` | -- |
| **In `.tlib` filename** | `logging_lib.tlib` | -- |

Package names appear in `package.tl`, dependency declarations, and archive filenames. Module names appear in source code.

---

## End-to-End Example

This walks through a library with a dependency and a consumer using both.

### 1. logging_lib package

**logger.tl:**
```tl
#module Logger
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

**package.tl:**
```tl
format(1)
package(logging_lib)
version("2.0.0")
author("Bob")
export(Logger)
source("logger.tl")
```

```bash
tess pack -o logging_lib.tlib
```

### 2. mylib package (depends on logging_lib)

**src/math.tl:**
```tl
#module MathUtils
#import "internal.tl"

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

**src/internal.tl:**
```tl
#module MathUtils.Internal

validate_range(lo, hi) {
  if lo > hi { fatal("invalid range") }
}

fatal(msg) {
  Logger.error(msg)
  c_exit(1)
  void
}
```

**package.tl:**
```tl
format(1)
package(mylib)
version("1.0.0")
author("Alice")
export(MathUtils)
source("src/")

depend(logging_lib, "2.0.0")
depend_path("./libs")
```

```bash
tess pack -o mylib.tlib
```

The archive contains `math.tl` and `internal.tl` but not logging_lib's source. The metadata records the dependency: `logging_lib=2.0.0`.

### 3. Consumer application

**src/main.tl:**
```tl
#module main

main() {
  x := MathUtils.clamp(150, 0, 100)
  c_printf("clamped: %d\n", x)

  y := MathUtils.lerp(0.0, 10.0, 0.5)
  c_printf("lerp: %f\n", y)
  0
}
```

**package.tl:**
```tl
format(1)
package(myapp)
version("0.1.0")
source("src/")

depend(mylib, "1.0.0")
depend_path("./libs")
// logging_lib is NOT listed -- resolved automatically as a transitive dependency
```

```bash
tess exe -o myapp
```

The compiler auto-discovers `src/main.tl` via `source("src/")`, loads `mylib.tlib`, sees it requires `logging_lib=2.0.0`, finds `logging_lib.tlib` in `./libs`, verifies the version, and compiles everything together.

---

## `.tlib` Archive Format

A `.tlib` file is a custom binary format with uncompressed metadata followed by a deflate-compressed source payload.

### Layout

```
[4 bytes]  Magic: "TLIB"
[4 bytes]  Format version (big-endian u32)
           Metadata (uncompressed):
             Package name, author, version (u16-prefixed strings)
             Exported modules (u16 count + u16-prefixed strings)
             Required dependencies (u16 count + u16-prefixed strings)
             Optional dependencies (u16 count + u16-prefixed strings)
[4 bytes]  Uncompressed payload size (big-endian u32)
[4 bytes]  Compressed payload size (big-endian u32)
[N bytes]  Deflate-compressed payload (source files)
[4 bytes]  CRC32 checksum of all preceding bytes
```

Metadata is uncompressed so tools can inspect package metadata without decompressing the payload.

### Dependency encoding

Dependencies are stored as `"PackageName=Version"` strings (e.g., `"logging_lib=2.0.0"`).

### Integrity

A CRC32 checksum at the end covers all preceding bytes. Mismatches produce a corruption error.

### Compression

Only the payload (source files) is compressed, using deflate via vendored libdeflate.
