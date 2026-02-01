# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Tess is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner-style), generic types/functions, lambdas, closures, and C interoperability. The compiler generates C code which is then compiled to native executables or shared libraries.

## Documentation

The `docs/` directory contains detailed documentation about the compiler's design and implementation.

**IMPORTANT: Always read the documentation BEFORE exploring source code.** When learning about the Tess language or compiler internals, start with these documents rather than diving into the C source. The documentation explains design decisions, language semantics, and implementation strategies that are not obvious from reading code alone.

- `LANGUAGE_REFERENCE.md` - Language syntax, features, and type annotation rules
- `TYPE_SYSTEM.md` - Type system design and implementation
- `SPECIALIZATION.md` - Generic function specialization
- `TAGGED_UNIONS.md` - Tagged union implementation

## Build Commands

### Standard Build
```bash
make                    # Build the tess compiler (release mode)
make clean && make      # Clean build
make -j all             # Parallel build
```

### Build Configurations
```bash
make CONFIG=release     # Optimized build (default)
make CONFIG=debug       # Debug build with symbols
make CONFIG=asan        # Address sanitizer build for memory debugging
```

### Running Tests
```bash
make test               # Run all test suites
make -j test            # Run tests in parallel (preferred)
make -j test-mos           # MOS library tests only
make -j test-tess          # Compiler unit tests only
make -j test-tl            # Tess language integration tests only
```

### Full Test Suite
For a complete test run, use the debug configuration with a clean build:
```bash
(export CONFIG=debug && make clean && make -j all && make -j test)
```

### Single Test Execution
To run a single Tess language test:
```bash
./tess exe src/tess/tl/test_<name>.tl -o /tmp/test_output
/tmp/test_output        # Run the compiled test
```

### Using the Compiler
```bash
./tess c <file.tl>                    # Transpile to C (stdout)
./tess c -v <file.tl>                 # Transpile to C, verbose (stdout)
./tess exe <file.tl> -o <output>      # Compile to executable
./tess lib <file.tl> -o <output.so>   # Compile to shared library
./tess c --no-line-directive <file>   # Transpile without #line directives
./tess exe -v <file.tl> -o <output>   # Verbose compilation
./tess exe --no-standard-includes -I src/tl/std <file.tl> -o <output>  # Disable automatic std includes
./tess c --stats <file.tl>            # Report per-phase memory and time statistics
./tess c --time <file.tl>             # Report total elapsed time
```

The standard library paths are included automatically. Use `--no-standard-includes` to disable this and manually specify include paths with `-I`.

### Installation
```bash
make install PREFIX=/usr/local    # Install to /usr/local (default)
make install PREFIX=$HOME/.local  # Install to local user directory
```

## Building on Windows with CMake

On Windows, use CMake instead of the Makefile. The CMake build system supports MSVC, MinGW, and Clang.

### Prerequisites
- CMake 3.15 or later
- Visual Studio 2019 or later (for MSVC), or MinGW/Clang
- Git (optional, for version info)

### Initial Configuration
```powershell
# Configure with CMake (from repository root)
cmake -B out/build -S .

# Or specify a generator explicitly
cmake -B out/build -S . -G "Visual Studio 17 2022"
cmake -B out/build -S . -G "Ninja"
cmake -B out/build -S . -G "MinGW Makefiles"
```

### Build Configurations
```powershell
# Release build (optimized)
cmake -B out/build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --config Release

# Debug build (with symbols)
cmake -B out/build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build --config Debug

# Build with specific number of parallel jobs
cmake --build out/build --config Release -j 8
```

### Building the Compiler
```powershell
# Build all targets in parallel
cmake --build out/build --config Release -j

# Build specific target
cmake --build out/build --config Release --target tess

# Clean and rebuild
cmake --build out/build --config Release --target clean
cmake --build out/build --config Release
```

### Running Tests
```powershell
# Run all tests using CTest
cd out/build
ctest -C Release

# Run tests in parallel
ctest -C Release -j 8

# Run tests with verbose output
ctest -C Release --verbose

# Run specific test suite
ctest -C Release -R test-mos     # MOS library tests
ctest -C Release -R test-tess    # Compiler unit tests
ctest -C Release -R test-tl      # Tess language integration tests

# Run a specific test by name
ctest -C Release -R test_array
ctest -C Release -R test_tl_generic
```

### Single Test Execution
To run a single Tess language test on Windows:
```powershell
.\out\build\tess.exe exe src/tess/tl/test_<name>.tl -o test_output.exe
.\test_output.exe
```

### Using the Compiler on Windows
```powershell
# Transpile to C (stdout)
.\out\build\tess.exe c <file.tl>

# Compile to executable
.\out\build\tess.exe exe <file.tl> -o output.exe

# Compile to shared library
.\out\build\tess.exe lib <file.tl> -o output.dll

# Verbose compilation
.\out\build\tess.exe exe -v <file.tl> -o output.exe
```

### Windows-Specific Notes
- The CMake build automatically detects available C compilers (MSVC, MinGW, Clang)
- Visual Studio solution files are generated when using the VS generator
- Build outputs go to `out/build/<config>/` by default
- The compiler executable is named `tess.exe` on Windows
- Shared libraries use `.dll` extension instead of `.so`
- Some features like address sanitizer may have limited support depending on the compiler

### Installation on Windows
```powershell
# Install to default location (requires admin privileges)
cmake --install out/build --config Release

# Install to custom location
cmake --install out/build --config Release --prefix C:\path\to\install
```

## Architecture Overview

### Compilation Pipeline

The compiler follows this multi-phase pipeline:

1. **Tokenization** (`tokenizer.c`) - Lexical analysis of `.tl` source files
2. **Parsing** (`parser.c`) - Two-pass parsing:
   - First pass: Extract module symbols for forward references
   - Second pass: Build complete AST
3. **Type Inference** (`infer.c`) - Core type system work:
   - Generate type constructors for user-defined types
   - Rename variables to enforce lexical scoping (unique names)
   - Assign type variables to every symbol
   - Collect type constraints from AST
   - **Generic specialization** - Create monomorphic versions of polymorphic functions for each call site
   - Satisfy constraints via unification
4. **Transpilation** (`transpile.c`) - Generate C code from type-checked AST
5. **C Compilation** - Invoke system C compiler (cc/gcc/clang) to produce binary

### Key Invariant: Generic Function Specialization

The type inference phase performs **generic function specialization** (step 8 in DEV.md). Every call to a generic function creates a specialized monomorphic version constrained to the argument types at that call site. This is similar to C++ template instantiation. After specialization, the AST is re-analyzed with the specialized functions added to satisfy all type constraints.

### Major Components

#### MOS Library (`src/mos/`)
Foundation library providing:
- **Memory management** (`alloc.c`) - Arena allocators, leak detection
- **Data structures** - Dynamic arrays (`array.c`), hash maps (`hashmap.c`)
- **String utilities** (`str.c`) - String building and manipulation
- **S-expressions** (`sexp.c`, `sexp_parser.c`) - Parsing and manipulation
- **File operations** (`file.c`)

#### Tess Compiler (`src/tess/`)
Core compiler implementation:
- **AST** (`ast.c`, `ast.h`) - Abstract syntax tree representation
- **Parser** (`parser.c`) - Two-pass parsing with module symbol extraction
- **Type System** (`type.c`, `type_registry.h`) - Monotypes, polytypes, type constructors
- **Type Inference** (`infer.c`) - Constraint generation, satisfaction, generic specialization
- **Transpiler** (`transpile.c`) - C code generation
- **CLI** (`tess_exe.c`) - Command-line interface, import resolution, C compiler invocation

#### Standard Library (`src/tl/std/`)
Runtime library written in Tess:
- `builtin.tl` - Built-in functions (sizeof, alignof)
- `Alloc.tl` - Memory allocation interface
- `Array.tl` - Generic dynamic array with comprehensive documentation
- `stdlib.tl`, `stdio.tl`, `string.tl`, `stdint.tl` - C library bindings
- `Unsafe.tl` - Low-level unsafe operations

### Embedded Standard Library

The standard library C stubs (`src/tess/embed/std.c`) are embedded into the compiler binary at build time using the `mos_embed` tool. This allows the compiler to generate self-contained C output without requiring external files.

### Import Resolution

The compiler automatically resolves imports by:
1. Parsing `#import` directives from source files
2. Searching standard library paths (automatic) and user include paths (`-I` flags) for imported modules
3. Building dependency graph and ordering files
4. Parsing all files in dependency order

### Type System Details

- **Monotypes** - Concrete types (e.g., `int`, `Point(int)`)
- **Polytypes** - Generic types with type variables (e.g., `forall a. a -> a`)
- **Type Constructors** - User-defined types like structs and unions
- **Type Registry** - Global registry mapping type names to constructors
- **Specialization** - Generic functions are monomorphized at each call site

### Memory Management Philosophy

The compiler heavily uses arena allocators (`arena_create()`, `arena_destroy()`) for:
- Fast bulk allocation
- Simplified lifetime management (allocate during compilation, free at phase end)
- Leak detection in debug builds

## Code Style Notes

- **C11 with GNU extensions** - Uses computed goto, statement expressions
- **Manual memory management** - Explicit allocator passing
- **Arena-based allocation** - Most compiler data structures use arena allocators
- **Naming conventions**:
  - `tl_*` prefix for Tess language types/functions
  - `mos_*` prefix for MOS library
  - Lowercase with underscores (snake_case)
- **Booleans** - C code uses 1 and 0 instead of true and false
- **Tess language naming** - In the Tess language itself, types may start with lowercase letters and cannot be distinguished from function names or variables based solely on spelling/case. The compiler must use structural information (AST node types, type system data) rather than naming conventions to differentiate between types and values.

## Testing Infrastructure

- **MOS tests** (`src/mos/src/test_*.c`) - Unit tests for data structures
- **Tess tests** (`src/tess/src/test_*.c`) - Compiler unit tests
- **Tess language tests** (`src/tess/tl/test_*.tl`) - Integration tests that exercise various language features. These tests include:
  - **Passing tests** - Tests expected to compile and run successfully, covering:
    - Type inference and generics
    - Lambdas, closures, pattern matching
    - Memory management, pointers
    - Module system, mutual recursion
    - C interoperability
  - **Expected failure tests** (prefixed with `test_fail_`) - Tests that verify the compiler correctly rejects invalid code and produces appropriate error messages
  - **Known failure tests** - Tests for features that aren't yet implemented or are currently broken; these are tracked separately and expected to fail. There are two categories:
    - **Known failures** (`TL_KNOWN_FAILURES` in Makefile, `tl_create_known_failure()` in CMake) - Tests that should compile and run successfully but currently fail due to compiler bugs or missing features
    - **Known fail-failures** (`TL_KNOWN_FAIL_FAILURES` in Makefile, `tl_create_known_fail_failure()` in CMake) - Expected-failure tests (`test_fail_*`) that the compiler doesn't reject yet

### Testing Requirements

**Always take a test-first approach.** Write tests BEFORE writing the implementation, not after:
- For each new feature or change, first write a test that exercises the desired behavior, verify it fails (or add it to known failures), and only then write the implementation to make it pass
- For multi-step implementation plans, write and verify the test for each step before writing the code for that step
- New MOS library functions should have corresponding unit tests in `src/mos/src/test_*.c`
- New compiler features should have Tess integration tests in `src/tess/tl/test_*.tl`
- Run `make -j test` to verify all tests pass before committing
- **Bug investigation workflow**: When you suspect a bug or are asked to work on one, first write a minimal test case that demonstrates the specific bug and add it to the known failure tests in **both** build systems (Makefile and CMake). Verify that the test actually fails before doing any further analysis or fix work. This prevents wasting time chasing imaginary bugs.

## Build System Notes

This project has two build systems that must be kept in sync:

1. **Makefile** - Primary build system for Linux and macOS
2. **CMake** - Primary build system for Windows (also works on Linux/macOS)

**Important:** Any changes to the build system (adding tests, source files, build flags, known failures, etc.) must be applied to both systems:
- Makefile: `Makefile` in the repository root
- CMake: `CMakeLists.txt` files (root and `src/*/CMakeLists.txt`)

### Makefile Features
- Parallel builds with `-j`
- Multiple configurations (release/debug/asan)
- Verbose output with `V=1`
- Cross-platform C compiler detection
- Automatic dependency tracking
- Colored output for build status

### CMake Features
- Windows support with MSVC, MinGW, and Clang
- Multi-configuration generators (Visual Studio, Ninja Multi-Config)
- CTest integration for running tests

Nix Flake support is also available as an alternative build system.

## Maintaining the Changelog

The project maintains a changelog in `NEWS.md` following the [Keep a Changelog](https://keepachangelog.com/) format. This section describes how to add new entries when summarizing development work.

### Changelog Format

Each dated section in `NEWS.md` follows this structure:

```markdown
## [Unreleased] - YYYY-MM-DD to YYYY-MM-DD (git commit range)

### Highlights

- 3-5 bullet points summarizing the most important changes
- Focus on major features, breaking changes, and significant improvements
- Keep bullets concise and high-level

### Added
### Changed
### Deprecated
### Removed
### Fixed
### Security
```

**Date Format**: Use ISO 8601 format (YYYY-MM-DD) for all dates. Date ranges should be formatted as `YYYY-MM-DD to YYYY-MM-DD`.

**Section Guidelines**:
- **Highlights**: The 3-5 most significant changes from this period. This is what readers scan first.
- **Added**: New features, functionality, documentation, or tools that didn't exist before.
- **Changed**: Modifications to existing functionality, refactoring, performance improvements, or behavior changes.
- **Deprecated**: Features marked for future removal (include migration guidance if applicable).
- **Removed**: Features, code, or dependencies that were deleted.
- **Fixed**: Bug fixes, compilation errors, memory issues, or correctness improvements.
- **Security**: Security-related fixes or improvements (if any).

### Process for Adding New Entries

When adding a new changelog entry for a time period:

1. **Use an agent to research git commits**:
   - Launch a `general-purpose` agent to analyze commits over the target date range
   - Have the agent examine both commit messages AND actual diffs to understand what really changed
   - Ask the agent to identify broad themes and categorize changes into the standard sections

   Example agent prompt:
   ```
   Research the git commits from [start-commit] to [end-commit] in this repository.
   Look at the commit messages and the actual code changes to understand what work
   has been done during that period. Identify broad themes and categorize them into
   these sections: Added, Changed, Deprecated, Removed, Fixed, Security.

   Focus on:
   1. New features or functionality added
   2. Bug fixes and improvements
   3. Changes to existing behavior
   4. Removed functionality
   5. Breaking changes

   Don't list every commit - synthesize the work into high-level themes and notable
   changes. Look at the actual diffs to understand what really changed beyond just
   commit messages.
   ```

2. **Synthesize into high-level themes**:
   - Don't list individual commits - group related work into themes
   - Example: Instead of listing 10 commits about MSVC fixes, write "Fixed extensive Windows/MSVC compatibility issues (22+ commits): struct alignment, thread-local storage, temp file handling..."
   - Combine related small changes into single bullets

3. **Write in user-friendly language**:
   - Avoid technical jargon, error codes, or internal implementation details
   - Example: Use "incorrect use of temporary return values" instead of "MSVC C2102 errors"
   - Focus on what changed and why it matters, not how it was implemented
   - Describe features from a user's perspective

4. **Create the Highlights section**:
   - After categorizing all changes, identify the 3-5 most significant ones
   - These should be the changes that would most interest someone scanning the changelog
   - Include major features, breaking changes, significant improvements, and milestone achievements
   - Keep highlights concise - just the key point, not full details

5. **Maintain chronological order**:
   - Add new sections at the top of the file (after the header)
   - Most recent changes should appear first
   - This matches the Keep a Changelog convention

### Example Entry Structure

```markdown
## [Unreleased] - 2026-01-28 to 2026-01-30 (660c97f..576c968)

### Highlights

- New code formatter with sophisticated formatting capabilities
- Comprehensive array library with functional programming operations
- Attributes system for function and parameter annotations
- Arity-based function overloading

### Added

- **Code Formatter**: New `tess fmt` subcommand with multi-line token alignment...
- **Array Standard Library**: Comprehensive Array.tl API with element access...

### Changed

- **Name Mangling**: Changed separator from single underscore to double underscore...

### Removed

- **Positional Struct Initialization**: Removed support for positional struct...

### Fixed

- Fixed MSVC misoptimization of small string writes through `str_span` alias.
- Fixed specialization issues with lambda arguments...
```

### Tips

- **Group related changes**: If multiple commits address the same feature or bug, combine them into a single bullet
- **Be specific but concise**: Include enough detail to understand what changed, but don't write paragraphs
- **Use bold for major topics**: Start bullets with `**Topic Name**:` for important additions or changes
- **Quantify when helpful**: Mention counts like "60+ new tests" or "22+ commits" to show scope
- **Break changes are important**: Always highlight breaking changes prominently in the Highlights section
- **Empty sections are OK**: If a time period has no Deprecated or Security items, that's fine - leave the section empty
