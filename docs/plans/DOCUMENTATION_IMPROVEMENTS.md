# Documentation Improvements

Actionable improvements for the README and docs/ directory, based on a review of the current state.

## High Priority

### 1. Complete Standard Library Reference

`docs/STANDARD_LIBRARY.md` is marked "Work in Progress". Five modules are completely undocumented:

| Module | File | Notes |
|--------|------|-------|
| **Str** | `src/tl/std/Str.tl` | String type with SSO — likely the most used module after Array |
| **HashMap** | `src/tl/std/HashMap.tl` | Robin Hood hash map — essential data structure |
| **Unsafe** | `src/tl/std/Unsafe.tl` | Cross-family integer conversions |
| **stdint** | `src/tl/std/stdint.tl` | Fixed-width integer types |

The documented modules (builtin, Array, Alloc) are also incomplete — Alloc is partially documented.

**Suggested order:** Str, HashMap, Alloc (complete), Unsafe, stdint, fatal.

### 2. Link WINDOWS_BUILD.md from Navigation

`docs/WINDOWS_BUILD.md` exists (88 lines, comprehensive CMake instructions) but is not linked from:
- `docs/README.md` — missing from the Documents table
- `README.md` — the Windows section shows commands inline but doesn't link to the detailed doc

**Fix:**
- Add a row to the `docs/README.md` table: `| [WINDOWS_BUILD.md](WINDOWS_BUILD.md) | CMake build instructions for Windows |`
- Add to `README.md` Windows section: `See [Windows Build Guide](docs/WINDOWS_BUILD.md) for detailed setup.`

### 3. CLI Command Reference

The `tess` subcommands and their flags are scattered across README.md, PACKAGES.md, and CLAUDE.md. There is no single reference for all commands.

Commands to document:

```
tess c <file.tl>              # Transpile to C (stdout)
tess exe <file.tl> -o <out>   # Compile to executable
tess run <file.tl>            # Compile and execute
tess lib <file.tl> -o <out>   # Compile to shared library
tess lib --static <file.tl>   # Compile to static library
tess init                     # Scaffold a new project
tess pack                     # Package a .tlib archive
tess validate                 # Validate package.tl
```

Common flags: `-v`, `--no-line-directive`, `--no-standard-includes`, `-I <path>`, `--time`, `--stats`, `-D <name>`, `-` (stdin).

**Options:**
- Add a "## CLI Reference" section to LANGUAGE_REFERENCE.md
- Or create a standalone `docs/CLI_REFERENCE.md`

## Medium Priority

### 4. Link Plans Directory from docs/README.md

`docs/plans/README.md` tracks implementation status of design documents but isn't referenced from `docs/README.md`. Add a section:

```markdown
## Design Documents

See [plans/](plans/) for design proposals and implementation status of past and future features.
```

### 5. Expand Quick Start in README

The Quick Start section shows `tess init` but doesn't explain what it creates. Add a brief note:

```markdown
This creates:
- `package.tl` — project manifest (name, version, dependencies)
- `src/main.tl` — entry point

See [Packages](docs/PACKAGES.md) for the full manifest format.
```

### 6. More Tutorials

`docs/tutorials/` has only one tutorial (operator overloading). Good candidates for new tutorials:

- **Getting started with packages** — creating, depending on, and publishing `.tlib` files
- **Closures and memory** — stack vs. allocated closures, arena patterns, shared mutable state
- **C interop patterns** — calling C from Tess, exporting Tess to C, working with Ptr types

## Low Priority

### 7. Inline C (`#ifc...#endc`) Examples

Documented in LANGUAGE_REFERENCE.md but lacks usage examples showing when and why to use it.

### 8. Conditional Compilation Examples

`#ifdef`/`#ifndef`/`#define`/`#undef`/`#endif` are mentioned in the README feature list and briefly in the reference, but could use more detailed examples showing practical patterns (debug logging, platform-specific code, feature flags).
