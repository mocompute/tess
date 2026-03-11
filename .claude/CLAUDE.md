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
./tess run src/tess/tl/test/pass/test_<name>.tl
```

Compiler: `./tess <command> <file.tl>` — commands: `c` (transpile to C), `exe` (compile), `run` (compile+execute), `lib` (shared/static library). Common flags: `-v`, `--no-line-directive`, `-I <path>`, `--time`, `--stats`.

## Tess Language Quick Reference

Key syntax to remember when writing `.tl` code:

- **Binding**: `x := 42` or `x: Int := 42` — introduces a new variable (let-in semantics)
- **Reassignment**: `x = 99` — mutates an existing binding (statement, not expression)
- **There is NO `mut` keyword.** All bindings are reassignable by default. Do not write `mut` anywhere.
- Compound assignment: `x += 1`, `x -= 1`, etc.
- See `docs/LANGUAGE_REFERENCE.md` for full syntax

## Code Style

- **C11 with NO extensions** - Must be compatible with cross-platform C compilers
- **Arena-based allocation** - Explicit allocator passing; most data uses arena allocators
- **Naming**: `tl_*` (Tess language), `mos_*` (MOS library), snake_case throughout
- **Booleans** - Use 1 and 0, not true/false

## Testing

- **MOS tests** (`src/mos/src/test_*.c`) - Data structure unit tests
- **Tess tests** (`src/tess/src/test_*.c`) - Compiler unit tests
- **TL tests** (`src/tess/tl/test/`) - Language integration tests, auto-discovered by directory:
  - `test/pass/` - Expected to compile and run successfully
  - `test/pass_optimized/` - Need compiler optimization (e.g. tail calls)
  - `test/fail/` - Expected compile-time failures (compiler must reject)
  - `test/fail_runtime/` - Expected runtime failures (compile OK, must fail at runtime)
  - `test/known_failures/` - Tests that should pass but don't yet
  - `test/known_fail_failures/` - `test_fail_*` tests the compiler doesn't reject yet

### Testing Requirements

**Always take a test-first approach.** Write tests BEFORE implementation:
- Write a test for the desired behavior, verify it fails (or add to known failures), then implement
- Run `make -j test` to verify all tests pass before committing
- **Bug workflow**: Write a minimal failing test case in `test/known_failures/` before any fix work
- **Adding tests**: Just drop a `test_<name>.tl` file in the appropriate `src/tess/tl/test/` subdirectory — both Makefile and CMake auto-discover tests

## Build System

**Two build systems must be kept in sync.** Any change to one must be applied to both:
- **Makefile** (`Makefile`) - Primary for Linux/macOS
- **CMake** (`CMakeLists.txt`, `src/*/CMakeLists.txt`) - Primary for Windows (see `docs/WINDOWS_BUILD.md`)

This includes: source files, build flags, and build configuration. TL tests are auto-discovered from `src/tess/tl/test/` subdirectories.
