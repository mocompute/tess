# Building Tess

## Prerequisites

- A C11 compiler (GCC or Clang)
- GNU Make
- Git (optional — used to embed version info in the binary)

On Windows, see [Building on Windows with CMake](#windows) instead.

## Quick Start

```bash
make -j all          # Build the compiler
make -j test         # Run all tests
```

The resulting `tess` binary is copied to the project root.

## Build Configurations

Set `CONFIG` to choose a build configuration:

| `CONFIG=`  | Flags | Output directory |
|------------|-------|------------------|
| `release` (default) | `-O2`, LTO | `build-release/` |
| `debug`    | `-g`, debug assertions | `build-debug/` |
| `asan`     | AddressSanitizer + UBSan | `build-asan/` |
| `coverage` | Clang source coverage | `build-coverage/` |

```bash
make CONFIG=debug -j all      # Debug build
make CONFIG=asan -j all       # ASAN build
```

Set `V=1` for verbose compiler output (shows full command lines).

## Installation

```bash
make install                          # Install to /usr/local
make install PREFIX=/opt/tess         # Custom prefix
make install DESTDIR=/tmp/staging     # Staged install (for packaging)
```

This installs:
- `$PREFIX/bin/tess` — the compiler binary
- `$PREFIX/lib/tess/std/*.tl` — the standard library source files

## Nix

The repository includes a `flake.nix`. To build:

```bash
nix build                # Release build (default)
nix build .#debug        # Debug build
nix build .#asan         # ASAN build
```

To install into your user profile:

```bash
nix profile install .    # Install from local checkout
nix profile upgrade tess # Upgrade after pulling new changes
```

To install directly from GitHub without a local checkout:

```bash
nix profile install github:mocompute/tess        # Latest commit on main
nix profile install github:mocompute/tess/v0.1.1 # Specific tagged release
```

To enter a development shell (includes Clang, CMake, LLDB, perf, Valgrind, and other tools):

```bash
nix develop
```

A Nix overlay is available at `overlays.default` for use in other flakes.

## The Standalone Binary

The `tess` binary embeds the entire standard library. During the build, a tool called `stdlib_pack` compresses all `.tl` files from `src/tl/std/` into a `.tpkg` archive that is linked directly into the executable. At runtime, the compiler extracts this archive to a temporary directory and uses it to resolve `#import <...>` paths.

The only dynamic dependency is libc. This means the binary is self-contained: you can copy it to another machine and it will work without any additional files.

When `tess` is installed via `make install`, the standard library source files are also placed at `<prefix>/lib/tess/std/`. The import resolver checks this path before the embedded copy, so the installed files take precedence.

## Windows

See [Building on Windows with CMake](WINDOWS_BUILD.md).
