# CLAUDE.md

## Git Rules

- NEVER commit on your own initiative. Always ask permission before committing, every time.

## Project Overview

Tess is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner-style), generic types/functions, lambdas, closures, and C interoperability.

## Documentation

**IMPORTANT: Always read `docs/` BEFORE exploring source code.** It explains design decisions and implementation strategies not obvious from code alone.

## Build Commands

```bash
make -j all                # Parallel build (release mode)
make CONFIG=debug          # Debug build with symbols
make CONFIG=asan           # Memory debug (ASAN) build
make -j test               # Run all tests in parallel (preferred)
make -j test-mos           # MOS library tests only
make -j test-tess          # Compiler unit tests only
make -j test-tl            # Tess language integration tests only
```

Full debug test run:
```bash
(export CONFIG=debug && make clean && make -j all && make -j test)
```

Single test:
```bash
./tess exe src/tess/tl/test_<name>.tl -o /tmp/test_output && /tmp/test_output
```

Compiler usage:
```bash
./tess c <file.tl>                   # Transpile to C (stdout)
./tess exe <file.tl> -o <output>     # Compile to executable
./tess lib <file.tl> -o <output.so>  # Compile to shared library
./tess c --stats <file.tl>           # Per-phase memory and time statistics
```

Common flags: `-v` (verbose), `--no-line-directive`, `--no-standard-includes`, `-I <path>`, `--time`.

## Code Style

- **C11 with NO extensions** - Must be compatible with cross-platform C compilers
- **Arena-based allocation** - Explicit allocator passing; most data uses arena allocators
- **Naming**: `tl_*` (Tess language), `mos_*` (MOS library), snake_case throughout
- **Booleans** - Use 1 and 0, not true/false

## Testing

- **MOS tests** (`src/mos/src/test_*.c`) - Data structure unit tests
- **Tess tests** (`src/tess/src/test_*.c`) - Compiler unit tests
- **TL tests** (`src/tess/tl/test_*.tl`) - Language integration tests
  - `test_fail_*` - Expected failures (compiler should reject these)
  - **Known failures** (`TL_KNOWN_FAILURES` / `tl_create_known_failure()`) - Tests that should pass but don't yet
  - **Known fail-failures** (`TL_KNOWN_FAIL_FAILURES` / `tl_create_known_fail_failure()`) - `test_fail_*` tests the compiler doesn't reject yet

### Testing Requirements

**Always take a test-first approach.** Write tests BEFORE implementation:
- Write a test for the desired behavior, verify it fails (or add to known failures), then implement
- Run `make -j test` to verify all tests pass before committing
- **Bug workflow**: Write a minimal failing test case and add it to known failures in **both** build systems before any fix work

## Build System

**Two build systems must be kept in sync.** Any change to one must be applied to both:
- **Makefile** (`Makefile`) - Primary for Linux/macOS
- **CMake** (`CMakeLists.txt`, `src/*/CMakeLists.txt`) - Primary for Windows (see `docs/WINDOWS_BUILD.md`)

This includes: adding tests, source files, build flags, and known failures.
