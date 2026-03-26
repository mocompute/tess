# CLAUDE.md

## Project Overview

Tess is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner-style), generic types/functions, lambdas, closures, and C interoperability.

## Documentation

**IMPORTANT: Always read `docs/` BEFORE exploring source code.** It explains design decisions and implementation strategies not obvious from code alone.

Key docs:
- `docs/LANGUAGE_REFERENCE.md` — Complete syntax reference
- `docs/LANGUAGE_MODEL.md` — Core semantics (binding expressions, closures, scoping)
- `docs/TL_CODING_CONVENTIONS.md` — Full coding conventions (quick reference is inlined below)
- `docs/TYPE_SYSTEM.md` — Type inference, generics, constraints
- `docs/SPECIALIZATION.md` — Monomorphisation pipeline
- `docs/plans/` — Design documents for planned/in-progress features

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

Build output: `build-release/` (release), `build-debug/` (debug).

Full debug test run:
```bash
(export CONFIG=debug && make clean && make -j all && make -j test)
```

Single test:
```bash
./tess run src/tess/tl/test/pass/test_<name>.tl
```

**After `git stash` or `git stash pop`, ALWAYS rebuild** (`make -j all`) before running tests — the binary on disk is stale.

Compiler: `./tess <command> <file.tl>` — commands: `c` (transpile to C), `check` (type-check only), `exe` (compile), `run` (compile+execute), `lib` (shared/static library). Common flags: `-v`, `--no-line-directive`, `-I <path>`, `--time`, `--stats`.

## Tess Language Quick Reference

Rules for writing `.tl` code (inlined from `docs/TL_CODING_CONVENTIONS.md`):

- **No `mut` keyword.** Bindings are reassignable by default. Do not write `mut` anywhere.
- **`:=` declares, `=` assigns.** `x := 42` creates a new binding; `x = 42` mutates an existing one.
- **Use `Const` for immutable bindings.** `x: Const := 5` or `x: Const[Int] := 5`. Prevents reassignment, transpiles to C `const`. For-loop variables are implicitly `Const`.
- **String literals are `String` (SSO).** `"foo"` is a `String`. Use `c"foo"` for a C string (`Ptr[CChar]`). The `s"foo"` prefix is still accepted but redundant.
- **`main()` returns `CInt`.** The compiler enforces this. No type annotation needed.
- **Omit type annotations in implementations.** Synopsis has full types; implementations use parameter names only. Inference handles the rest.
- **Omit integer suffixes.** Write `0`, not `0zu`. Use suffixes only when inference is ambiguous.
- **Use `Option` for absence, `Result` for errors.** Not null, not sentinel values.
- **Use tagged unions for alternatives.** Not integer codes or boolean flags.
- **`when`/`else` for multiple variants; variant binding for a single expected variant.**
- **Keep code flat.** Early returns and variant bindings instead of deep nesting.
- **`self` is the receiver.** `Ptr[T]` for mutating methods, `T` by value for read-only.
- **Allocator overloads.** Provide explicit `Ptr[Allocator]` version + convenience version using default.
- **Private helpers start with `_`.** Types are PascalCase, functions are snake_case.
- **Run `tess fmt` before committing.**
- **One module, one type.** Name the type the same as the module (or `T`). Callers use UFCS.
- **Named fields in struct construction.** `ArgSpec(long_name = name, kind = FlagBool)`
- **Implicit address-of in UFCS.** Write `arr.push(x)`, not `arr.&.push(x)` — the compiler automatically takes the address when UFCS dispatches a value to a `Ptr[T]` parameter.
- **Full C-style operators.** `&`, `|`, `^`, `<<`, `>>`, `~` and compound forms (`<<=`, `>>=`, etc.) all work on integer types.
- **`#ifc`/`#endc` for inline C.** Embeds raw C code in `.tl` files (used in `builtin.tl` for FFI primitives like hash functions).

See `docs/LANGUAGE_REFERENCE.md` for full syntax.

### Common Pitfalls

These mistakes have caused repeated debugging sessions — avoid them:

1. **Trait methods must be in a named module.** `hash(p: Point)` in `#module main` won't be found by trait dispatch. Define it in `#module Point`.
2. **Don't alias types over module names.** `Str = Str.Str` inside a module shadows the `Str` module. Auto-collapse handles bare `Str` in type positions.
3. **`:=` vs `=` confusion.** `n: Int = 10` (reassignment to undeclared `n`) is NOT a binding — it causes confusing downstream errors. Use `n: Int := 10`.
4. **`Hash` trait requires `#import <Hash.tl>`.** Unlike operator traits (compiler builtins), `Hash` is defined in the standard library. Any file using `.hash()` or `HashMap` must `#import <Hash.tl>`. The `Eq` trait and other operator traits remain compiler builtins in `infer.c`.
5. **Don't use explicit `.&` in UFCS calls.** `arr.&.push(x)` works but is redundant — implicit address-of means `arr.push(x)` is sufficient when `push` expects `Ptr[T]`.

## Source Architecture

The compiler pipeline in `tess_exe.c` runs three phases sequentially: **parse → infer → transpile**, then invokes the system C compiler.

### Compiler (`src/tess/`)

| File(s) | Role |
|---------|------|
| `tess_exe.c` | CLI entry point, orchestrates the pipeline |
| `tokenizer.c`, `token.c` | Lexer — source to token stream |
| `parser.c`, `parser_expr.c`, `parser_statements.c`, `parser_types.c`, `parser_tagged_union.c` | Two-pass parser: pass 1 collects module symbols, pass 2 builds AST |
| `ast.c` | AST node representation (s-expression based) |
| `source_scanner.c` | Pre-scan to discover files, modules, and import order |
| `import_resolver.c` | Resolves `import` declarations to file paths |
| `infer.c`, `infer_alpha.c`, `infer_constraint.c`, `infer_specialize.c`, `infer_update.c` | Type inference (HM unification), alpha conversion, constraint solving, monomorphisation |
| `type.c` | Type representation and operations |
| `transpile.c` | C code generation from typed AST |
| `format.c` | Source code formatter (`tess fmt`) |
| `manifest.c` | `package.tl` DSL parser |
| `tpkg.c` | Package archive (.tpkg) creation/extraction |
| `error.c` | Error formatting and reporting |

Key headers: `src/tess/include/` — public API (`ast.h`, `parser.h`, `infer.h`, `transpile.h`, `type.h`, `type_registry.h`, etc.).
Internal headers: `src/tess/src/parser_internal.h`, `infer_internal.h`.

### MOS library (`src/mos/`) — shared data structures

| File | Provides |
|------|----------|
| `alloc.c` | Arena allocator, malloc wrapper (fills 0xCD in debug) |
| `str.c` | Immutable string type (`str`) |
| `array.c` | Generic dynamic arrays (`str_array`, etc.) |
| `hashmap.c` | Generic hash map |
| `hash.c` | Hash functions |
| `file.c` | File I/O utilities |
| `platform.c` | Platform abstraction (timers, paths) |

Headers: `src/mos/include/`.

### Standard library (`src/tl/std/`) — Tess language runtime

Standard library `.tl` files (must be explicitly imported with `#import <Name.tl>`): `Alloc.tl`, `Array.tl`, `builtin.tl`, `CommandLine.tl`, `Cond.tl`, `File.tl`, `HashMap.tl`, `Hash.tl`, `Mutex.tl`, `Once.tl`, `Print.tl`, `Slice.tl`, `String.tl`, `Thread.tl`, `ThreadError.tl`, `ToInt.tl`, `ToString.tl`, `Unsafe.tl`, plus C FFI bindings (`cstdio.tl`, `cstdlib.tl`, `cstdint.tl`, `cstring.tl`).

## Code Style

- **C11 with NO extensions** - Must be compatible with cross-platform C compilers
- **Arena-based allocation** - Explicit allocator passing; most data uses arena allocators
- **Naming**: `tl_*` (Tess language), `mos_*` (MOS library), snake_case throughout
- **Booleans** - Use 1 and 0, not true/false
- **Debug allocator fills with 0xCD** — always initialize new struct fields explicitly

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
