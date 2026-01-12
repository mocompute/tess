# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Tess is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner-style), generic types/functions, lambdas, closures, and C interoperability. The compiler generates C code which is then compiled to native executables or shared libraries.

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
make -j test            # Run tests in parallel
make test-mos           # MOS library tests only
make test-tess          # Compiler unit tests only
make test-tl            # TL language integration tests only
```

### Single Test Execution
To run a single TL language test:
```bash
./tess exe -I src/tl/std src/tess/tl/test_<name>.tl -o /tmp/test_output
/tmp/test_output        # Run the compiled test
```

### Using the Compiler
```bash
./tess c -I src/tl/std <file.tl>                    # Transpile to C (stdout)
./tess exe -I src/tl/std <file.tl> -o <output>      # Compile to executable
./tess lib -I src/tl/std <file.tl> -o <output.so>   # Compile to shared library
./tess c --no-line-directive -I src/tl/std <file>   # Transpile without #line directives
./tess exe -v -I src/tl/std <file.tl> -o <output>   # Verbose compilation
```

The `-I src/tl/std` flag is required to include the standard library (builtin.tl, Array.tl, etc).

### Installation
```bash
make install PREFIX=/usr/local    # Install to /usr/local (default)
make install PREFIX=$HOME/.local  # Install to local user directory
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
2. Searching include paths (`-I` flags) for imported modules
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

## Testing Infrastructure

- **MOS tests** (`src/mos/src/test_*.c`) - Unit tests for data structures
- **Tess tests** (`src/tess/src/test_*.c`) - Compiler unit tests
- **TL tests** (`src/tess/tl/test_*.tl`) - Integration tests covering:
  - Type inference and generics
  - Lambdas, closures, pattern matching
  - Memory management, pointers
  - Module system, mutual recursion
  - C interoperability
  - Expected failure tests (prefixed with `fail_`)

## Build System Notes

The Makefile supports:
- Parallel builds with `-j`
- Multiple configurations (release/debug/asan)
- Verbose output with `V=1`
- Cross-platform C compiler detection
- Automatic dependency tracking
- Colored output for build status

CMake and Nix Flake support are also available for alternative build systems.
