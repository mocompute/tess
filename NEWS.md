# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased] - 2026-02-16 to 2026-02-16 (51575693..ac4f2b5d)

### Highlights

- New `[[c_export]]` attribute for giving Tess functions stable C-compatible symbol names in shared libraries
- Automatic C header generation when compiling shared libraries with `tess lib`
- Namespaced `tl_init` per library to avoid symbol collisions when linking multiple Tess libraries
- Compile-time validation rejects `c_export` on functions with non-C-compatible types

### Added

- **`[[c_export]]` Attribute**: Tess functions can now be annotated with `[[c_export]]` or `[[c_export("custom_name")]]` to produce stable, unmangled C symbol names when compiled as shared libraries. The default export name follows `Module_func` for non-main modules and bare `func` for `#module main`; a custom string argument overrides this. The transpiler generates thin wrapper functions that delegate to the internal mangled implementations.
- **Automatic C Header Generation**: Running `tess lib foo.tl -o libfoo.so` now automatically writes a companion `libfoo.h` header alongside the shared library, containing include guards, standard includes, the init function prototype, and prototypes for every exported function.
- **`TL_EXPORT` Visibility Macro**: New macro in the standard preamble that expands to `__declspec(dllexport)` on MSVC and `__attribute__((visibility("default")))` on GCC/Clang. Exported wrappers and `tl_init` use `TL_EXPORT`; all internal functions remain `static`.
- **Namespaced Library Initialization**: When compiling `tess lib foo.tl -o libfoo.so`, the initialization function is now emitted as `tl_init_foo()` (derived from the output path), preventing symbol collisions when multiple Tess libraries are linked into the same program.
- **Compile-Time Export Validation**: The compiler rejects `c_export` functions whose parameter or return types are not C-compatible. Allowed types include `CInt`, `CSize`, `CFloat`, and other C numeric types, `Int`, `Float`, `Bool`, `Void`, `Ptr[T]`, and `c_symbol`-annotated types.
- **`c_export` Tests**: Comprehensive unit and integration tests covering wrapper generation, header file output, type validation, and end-to-end shared library compilation and linking from C.

### Changed

- **Internal Functions Static in Library Mode**: All internal `tl_fun_*` functions are now emitted as `static` in library mode. Only `TL_EXPORT` wrappers and the init function are externally visible, producing a cleaner shared library symbol table.
- **`sizeof`/`alignof` on Void and Unresolved Types**: `sizeof` on `void`, unresolved type variables, or `any` now emits `(size_t)0` instead of `sizeof(void)` (a GCC extension), and `alignof` emits `(size_t)1` instead of `_Alignof(void)` (which MSVC rejects).
- **Documentation**: Language reference updated with type definition ordering rules, lambda type parameter limitations, and `[[c_export]]` usage with examples. README updated with a `c_export` workflow example.
- **Windows Compatibility**: Library name and header guard derivation now uses `file_basename()` for cross-platform correctness. Test harness updated with platform-specific library suffixes and working directory handling for `cmd.exe`.

### Removed

- **`test_forward_ref_tagged_union.tl`**: Removed from known failures and deleted. The requirement that types used by value must be defined before the referencing type was reclassified as by-design and documented in the language reference.
- **`test_lambda_immediate_type_argument.tl`**: Removed from known failures and deleted. Explicit type arguments on lambdas were ruled out as a language design decision (lambdas remain generic through inference).


## [Unreleased] - 2026-02-15 to 2026-02-16 (ef1d4283..51575693)

### Highlights

- New `try` prefix keyword for structural error propagation on any two-variant tagged union (no special Result type required)
- New `defer` statement for scope-exit cleanup with correct interaction across `return`, `break`, `continue`, and `try`
- Extended allocator with aligned allocation, resize, arena checkpoints, and allocation statistics
- Compiler stress tests and fixes for closures in named functions and chained `.*.&` syntax

### Added

- **`try` Keyword**: New prefix operator that structurally unwraps the first variant of any two-variant tagged union, returning the inner value on success or early-returning the full union on error. Works purely on structure -- no special type names like `Result` required. Validates at compile time that the operand is a two-variant union with a single-field first variant, and constrains the enclosing function's return type to match. Correctly runs pending `defer` expressions on the early-return path.
- **`defer` Statement**: Schedules expressions to run at scope exit in reverse declaration order. Supports single-expression (`defer x = 0`) and block (`defer { ... }`) forms. Defers execute before `return`, `break`, and `continue`, with return values captured to a temporary first to prevent deferred side effects from corrupting the return value. Nested `defer` inside `defer` is disallowed.
- **Allocator Extensions**: The `Allocator` vtable gained `aligned_alloc`, `aligned_free`, and `resize` operations. Bump allocator implements in-place resize when the target is the most recent allocation. New APIs: `bump_reset`, `bump_save`/`bump_restore` for arena-style checkpoints, `bump_get_stats` for allocation statistics (allocated, capacity, peak, bucket count). Convenience wrappers added for the transient allocator.
- **Compiler Stress Tests**: Eight new stress tests covering deep nesting, when/case combinations, scope shadowing, generic types, closures, control flow, expression positions, and type features. Three known-failure tests added to document current compiler limitations.
- **`array_reset` Macro**: New `array_reset` in the MOS array library that resets size to 0 without freeing memory, allowing buffer reuse.

### Changed

- **Chained `.*.&` Syntax in When/Case**: Removed the restriction requiring `.&` operands to be bare symbols, enabling expressions like `p.*.&` (dereference then take mutable reference). Mutations now propagate through the original pointer rather than a temporary copy.
- **Closures in Named Helper Functions**: Fixed tree shaking to only add value let-ins unconditionally; let-in-lambdas are now discovered through normal reachability, preventing orphaned generic copies from causing `free_variable_not_found` errors.
- **Bump Allocator `calloc` Semantics**: `_bump_calloc` now correctly zero-fills memory, matching standard `calloc` behavior (previously used a debug sentinel).
- **Transpiler Counter Types**: Widened `next_res` and `next_block` counters from `u32` to `u64` and separated label generation onto its own counter.
- **Extra Substitution Pass Before Unresolved Type Errors**: The type checker now performs an additional substitution attempt on let-in names and reassignment types before reporting unresolved type errors, preventing false positives.
- **Codegen Refactoring**: Extracted `resolve_nullary_type_argument` and `should_assign_value` helpers to deduplicate type resolution and assignment emission logic.
- **Emacs `tl-mode.el`**: Keyword highlighting uses symbol boundaries instead of word boundaries, preventing false matches inside identifiers. Added `defer` and `try` keywords.
- **Documentation**: Language reference updated with full sections for `defer` and `try`. "Unwrap-or-bail" renamed to "let-else" throughout. README tagline and feature list updated.

### Removed

- **`log_constraint_mono` Function**: Removed unused debug function from `infer.c` that used a GCC extension incompatible with MSVC.

### Fixed

- **Null Pointer Assignment**: Fixed `generate_let_in` incorrectly treating `null` the same as `void`, which caused `var: Ptr[T] := null` to generate an uninitialized declaration instead of assigning `NULL`.
- **AST Traversal of Defers**: Added missing DFS traversal of `body.defers` in `ast_node_each_node`, fixing type variable resolution inside non-trivial defer expressions.
- **ASAN Warning from Empty String Init**: Added guard to skip zero-length `memcpy` in `init_small` when initializing an empty string.

## [Unreleased] - 2026-02-10 to 2026-02-15 (1215d21..ef1d4283)

### Highlights

- Explicit type arguments with square bracket syntax: `Array[Int]`, `push[T](...)`, `sizeof[Int]()` (breaking change)
- New `.[]` indexing syntax replaces `[]` for pointer/array indexing, freeing `[]` for type arguments
- `when` keyword replaces `case` for tagged union pattern matching
- Unwrap-or-bail syntax for early-exit patterns: `val: Variant := expr else { return }`
- String literals reverted to plain C strings (`char const *`), removing the implicit `Str` wrapping
- Type literals removed from the type system (~370 lines deleted), no longer needed with explicit type arguments

### Added

- **Explicit Type Arguments (`[T]` syntax)**: Type parameters are now declared and passed using square brackets instead of parenthesized `T: Type` convention. Affects function definitions (`push[T](...)`), type constructors (`Array[Int]`), type identifiers, and builtins (`sizeof[Int]()`, `alignof[T]()`). AST nodes gained dedicated `type_parameters`/`type_arguments` slots, separating type arguments from value arguments structurally. All standard library and test files migrated to the new syntax.
- **`when` Keyword for Tagged Unions**: New `when` keyword for tagged union pattern matching, replacing `case`. All existing tests and documentation migrated.
- **Unwrap-or-Bail Syntax**: New syntactic sugar `val: Variant := expr else { diverge }` that desugars into a `when` expression where the else block must contain a diverging statement (`return`, `break`, or `continue`). Includes compile-time divergence checking.
- **`.[]` Indexing Syntax**: Pointer and CArray indexing changed from `[]` to `.[]`, fitting with the existing postfix dot family (`.field`, `.*`, `.&`, `->`).
- **Inference Sub-Phase Timing (`--stats`)**: The `--stats` flag now reports detailed per-phase timing for all 7 inference sub-phases along with operation counters for traversal, unification, substitution, and specialization.
- **Debug Instrumentation Framework**: Compile-time debug flags in `infer.c` and `type.c`: `DEBUG_INVARIANTS` (phase boundary assertions), `DEBUG_INSTANCE_CACHE` (specialization cache tracing), `DEBUG_RECURSIVE_TYPES` (recursive type parsing), `DEBUG_TYPE_ALIAS` (alias resolution tracing).
- **20+ new tests**: Tagged union tests covering many variants, CArray fields, Result types, generic variants, pointer fields, nested `when`, bail syntax, recursive types, and type argument annotations.
- **Documentation**: New `docs/ALPHA_CONVERSION.md` covering the variable renaming system. New `docs/plans/EXPLICIT_TYPE_ARGS.md` design record. Updated language reference for `[]` and `.[]` syntax.

### Changed

- **Square Bracket Syntax for Types (Breaking Change)**: All type constructors use `[T]` instead of `(T)`. Function signatures changed from `push(self: Ptr(Array(T)), x: T)` to `push[T](self: Ptr[Array[T]], x: T)`. Builtins `sizeof` and `alignof` now have nullary `sizeof[T]()` and unary `sizeof(x)` overloads.
- **String Literals Reverted to C Strings (Breaking Change)**: String literals (`"hello"`) now produce plain C strings (`char const *`) instead of `Str` values. The `ast_c_string` AST tag was removed; both `"..."` and `c"..."` now produce `ast_string` nodes.
- **`builtin.tl` Now Imports `fatal.tl`**: Import processing reordered to scan `builtin.tl`'s own `#import` directives before processing package and user files.
- **Build System**: `tess` binary now built inside the build directory and copied to the project root. `CFLAGS` handling fixed so user-supplied flags no longer silently override build-configuration flags. Test lists sorted alphabetically.
- **Emacs tl-mode**: Updated for `when` keyword highlighting, `[...]` type argument brackets in function/type definitions, font-lock rules for type parameters, and updated navigation/imenu.
- **Major Refactoring of `infer.c` and `type.c`**: ~2000 lines touched. Extracted `tl_monotype_children()` helper, data-driven builtin type init, and numerous phase/traversal functions. Removed dead `DEBUG_SPECIALIZE` block.
- **Transpiler: Dependency-Ordered Type Emission**: Synthesized (specialized) user types are now topologically sorted before emitting as C structs, fixing incomplete-type errors for recursive and nested generic types.
- **Transpiler: `void*` Casts for Pointer Comparisons**: Inserts `(void*)` casts on both operands of pointer relational operators, eliminating `-Wcompare-distinct-pointer-types` warnings.

### Removed

- **Type Literals**: Removed `tl_monotype_type_literal` and all associated infrastructure (~370 lines). With explicit `[T]` syntax, the type literal mechanism for passing types as values is no longer needed.
- **Annotation-Based Type Variable Discovery**: Removed `collect_annotation_type_vars()` and its heuristic uppercase-name scanning. Explicit type parameter declarations make this unnecessary.
- **`Array-tutorial.tl`**: Removed out-of-date tutorial file (331 lines).
- **Debug-Mode String Literal Tracking**: Removed `_STR_LITERAL_TAG`, `tl_str_literal_set_*` hash set, `_mark_literal()`, `_is_literal()`, and associated expected-failure tests.

### Fixed

- **Recursive Tagged Union Types**: Self-referential tagged unions (e.g., `IntList` with `Ptr[IntList]` field) now resolve correctly via deferred placeholder mechanism.
- **Mutual Recursion in Type Placeholders**: Fixed orphaned type variables when mutually recursive types reference each other through `Ptr`, by unifying with correct quantifiers via union-find substitution.
- **Type Alias Enum Member Access**: Type aliases now registered in the type environment (not just type registry), enabling `infer_struct_access` to resolve alias names.
- **Nested `when` Inference**: Fixed two bugs: unresolved annotation symbols incorrectly registering parameter names as type variables, and bottom-up traversal leaving outer bindings unresolved. Added `prepare_tagged_union_bindings()` pre-pass.
- **Generic Tagged Union Variants in Specialized Functions**: Phase 7 traversals no longer overwrite already-specialized annotation types.
- **Specialization Cache with Explicit Type Args**: Cache keys now incorporate explicit type arguments, preventing incorrect reuse across different type argument sets.
- **Null-Pointer in `infer_return` with `break`**: `break` statements (with `value=null`) inside `when` arms no longer crash during specialization.
- **Forward Type Parameter Declarations**: Compiler now copies type parameters from forward declarations to definitions when the definition omits them.
- **`_tl_fatal_` Output**: Added newline after fatal error messages.

### Performance

- **`tl_monotype_hash64`**: Replaced per-call hashmap allocation for cycle detection with fixed-size stack-allocated array with linear search, eliminating allocation overhead.
- **`parse_type_ctx` Pre-Allocation**: Replaced per-call context allocation (7 `map_create` calls) with a single pre-allocated context using `map_reset`/`hset_reset` between uses.
- **Skip Redundant Specialization**: In Phase 7 (update types), already-specialized type constructors with unchanged arguments skip `specialize_type_constructor` entirely.

## [Unreleased] - 2026-02-03 to 2026-02-10 (5c41c59..1215d21)

### Highlights

- Complete package system with `.tlib` archive format for distributing reusable code libraries
- Major performance optimizations: arena allocator tail caching and hashmap improvements for faster compilation
- New `package.tl` DSL for declaring package metadata using native Tess syntax
- Transitive dependency resolution automatically loads and version-checks multi-level package dependencies
- Memory leak fixes and arena-based allocation throughout compiler infrastructure

### Added

- **Package System (`.tlib` archives)**: Complete library distribution system with binary archive format using DEFLATE compression and CRC32 checksums. Archives store package metadata (name, version, author, exported modules, dependencies) plus compressed source files in a portable, platform-independent format.
- **`package.tl` DSL**: Declarative manifest format using Tess function-call syntax, parsed by the existing TL parser. Supports `format()`, `package()`, `version()`, `author()`, `export()`, `depend()`, `depend_optional()`, and `depend_path()` declarations. Replaces earlier INI manifest design.
- **CLI Commands**: Three new subcommands for package management:
  - `tess pack` - Create `.tlib` archives from source files with automatic `package.tl` discovery
  - `tess validate` - Validate package structure and exports against manifest
  - `tess init` - Generate skeleton `package.tl` file
- **Import Resolution System**: Distinguishes between quoted imports (`"file.tl"` - relative then -I paths) and angle-bracket imports (`<file.tl>` - standard library only). Tracks canonical paths, detects import cycles, and filters stdlib files.
- **Source Scanner**: Extracts `#module` and `#import` directives from source without full parsing. Handles conditional compilation (`#ifdef`/`#ifndef`), validates module declarations, and checks self-containment.
- **Transitive Dependency Resolution**: Compiler automatically loads and version-checks multi-level package dependencies (A depends on B depends on C) from `depend_path()` directories.
- **Module-Level Access Control**: `export()` declarations in `package.tl` document public API modules. Internal modules are accessible but removable via tree-shaking.
- **Vendor Library**: Integrated libdeflate-1.25 for fast compression/decompression (15,000+ lines of vendored code).
- **Toplevel Function Call Parser Mode**: New `mode_toplevel_funcall` parser mode that parses only top-level function calls using speculative arena allocation. Enables `package.tl` DSL interpretation without polluting the type registry.
- **75+ new tests**: Comprehensive test coverage across unit tests (`test_tlib.c`, `test_manifest.c`, `test_source_scanner.c`, `test_import_resolver.c`, `test_deflate.c`), integration tests, and end-to-end package consumption tests.
- **Documentation**: Complete package system guide (`docs/PACKAGES.md`), detailed 10-phase implementation plan (`docs/plans/TLIB_LIBRARIES.md`), and Windows build instructions (`docs/WINDOWS_BUILD.md`).
- **GCC Support**: Enabled GCC in Nix devShells, disabled `-Wformat-truncation` warnings.

### Changed

- **Performance Optimizations**: Major compilation speed improvements:
  - Arena allocator now caches tail pointer for O(1) fast-path allocation (previously walked linked list on every allocation)
  - Hashmap optimizations: power-of-2 sizes for faster modulo via bitwise AND, cached entry size, increased default/initial sizes (8→64 or 1024)
  - Type inference optimizations: generation counters for cycle detection, generation-based memoization for hash computation
  - Larger initial hashmap sizes in type registry (64→1024 entries) to reduce rehashing
- **Memory Management**: Parser, compiler, and test infrastructure now use arena allocators instead of `default_allocator()`, enabling bulk cleanup. Zero-size allocation handling fixed to return NULL instead of treating as failures.
- **Module System**: Nested module validation now checks immediate parent declarations. Cross-file nested modules properly handled. Duplicate module names across packages generate warnings during dependency loading.
- **Compiler Behavior**: Searches executable directory for `src/tl/std` in addition to other include paths. Silently ignores unknown `package.tl` DSL functions for forward compatibility. Cleans up temporary directories created during package extraction.
- **Build System**: Makefile doesn't rebuild `version.h` on every invocation. Fixed object dependencies. Known failures framework distinguishes "known failures" (should pass but don't) from "known fail-failures" (`test_fail_*` tests not yet rejected).
- **ASAN Configuration**: More granular AddressSanitizer control, leak detection can be toggled independently.

### Fixed

- **Memory Leaks**: Fixed leaks in tlib reader, import resolver, parser, and various test infrastructure components.
- **Use-After-Free**: Fixed in tokenizer string/file lifetime management.
- **Zero-Length Data Handling**: Handle zero-length `.tlib` entry data without allocating/copying. Guard `calloc()` when `entry_count` is 0.
- **Nested Module Parent Check**: Validate that nested modules declare their immediate parent (not just any ancestor).
- **AST Cloning**: Deep copy hash command words in `ast_clone()` for correct package parsing.
- **Module Error Deduplication**: Remove duplicate "module already declared" errors from source scanner.
- **Windows Path Handling**: Normalize backslash/forward-slash in path comparisons. Handle executable suffix in Windows test paths. Fix cross-drive navigation and proper quoting of paths with spaces.
- **ASAN Build**: Re-enable and properly configure AddressSanitizer builds with proper leak detection settings.

### Removed

## [Unreleased] - 2026-02-01 to 2026-02-03 (4699c0d..5c41c59)

### Highlights

- New string literal system: `"..."` literals now produce `Str` values with `c"..."` syntax for C interop (breaking change)
- Conditional compilation with `#ifdef`, `#ifndef`, `#define`, `#undef`, `#endif` directives and `-D` flag
- VS Code extension for TL language support with syntax highlighting and formatting integration
- Debug-mode string literal protection detecting invalid `Str.free` and `Str.push` on literals
- Critical hashmap corruption fix in generic specialization

### Added

- **Conditional Compilation System**: Full preprocessor-style conditional compilation with `#define`, `#undef`, `#ifdef`, `#ifndef`, `#endif` directives. Supports nested conditionals, `-D` command-line flag (including joined `-DFOO` form), and conditional import resolution (imports inside false `#ifdef` blocks are skipped). Auto-defines `DEBUG` or `NDEBUG` based on optimization mode.
- **C String Literal Syntax**: New `c"..."` syntax for C string literals (`const char*`) for C interoperability. Standard `"..."` literals now produce `Str` values using a dedicated string literal allocator.
- **Module Prelude System**: New `#module_prelude` directive allows defining module symbols that can be extended by a later `#module` of the same name. Embedded `prelude.tl` defines the `Str` type, enabling string literals without explicit imports.
- **VS Code Extension**: New TL language support extension for Visual Studio Code with TextMate-based syntax highlighting, bracket matching, comment toggling, and formatting integration via `tess fmt`.
- **Debug-Mode String Literal Protection**: In DEBUG builds, `Str.free` and `Str.push` on string literals produce fatal errors, catching common memory bugs. Small strings use a tag bit; big strings are tracked in a C-level pointer set.

### Changed

- **String Literals Now Produce Str Values (Breaking Change)**: `"hello"` now produces a `Str` value instead of `const char*`. Use `c"hello"` when C interop requires a C string. String literals use a dedicated bump allocator and should not be freed.
- **Const Handling in Type System**: Unification now looks through `Const` wrappers, const check moved to unification entry point.
- **Emacs tl-mode**: Underscore (`_`) no longer treated as part of identifiers, matching C mode behavior for easier navigation of `snake_case` names.
- **CTest Expected-Failure Tests**: Use CTest's native `WILL_FAIL` property instead of shell wrappers for expected-failure tests.
- **C Compiler Output Handling**: Compiler output is now captured and shown only on error. Verbose mode streams output in real-time.

### Deprecated

### Removed

### Fixed

- **Hashmap Corruption in Generic Specialization**: Fixed critical bug where iterating the env hashmap while `specialize_type_constructor_` inserted new entries caused Robin Hood hashing to relocate entries, corrupting unrelated type bindings.
- **Module-Mangle Toplevel Symbol Assignments**: Toplevel assignments now properly add module symbols and mangle names.
- **MSVC LTCG Miscompilation**: Fixed `str_hash32`/`str_hash64` miscompilation by avoiding `str_span` which MSVC whole-program optimization mishandled.
- **Valgrind: Uninitialized String Buffers**: All `str` construction paths now properly zero-initialize buffers. Big strings now always have null terminators written.
- **Windows Buffer Overflow**: Added bounds checking for command-line construction in `platform_exec()`.
- **Carriage Return Handling**: Improved `\r\n` handling in tokenizer and import line parsing. Fixed off-by-one error in `is_end_of_line`.
- **isspace Unsigned Char**: `isspace()` now receives `unsigned char` as required by C standard.
- **Format Operator Handling**: Added support for `<<=`, `>>=`, `%=`, `&=`, `^=`, `|=` operators in the formatter.
- **CMake Build Fixes**: Fixed helper scripts to pass build type and config correctly, copy `tess.exe` to project root after successful build, removed `-j` from Windows CI build.

### Security

## [Unreleased] - 2026-01-30 to 2026-02-01 (576c968..4699c0d)

### Highlights

- Const-correctness support with `Const(T)` type qualifier for const pointers with full compiler enforcement
- C array type redesign from value constructor to type annotation syntax with explicit pointer decay
- String standard library (`Str` module) with small string optimization supporting 14-byte inline strings
- Option and Result builtin types with generic unwrap support
- Centralized C keyword escaping throughout the transpiler

### Added

- **Const(T) type qualifier**: New type wrapper for const pointers that generates `const T*` in C output. The compiler enforces const correctness by rejecting mutation through const pointers, supporting implicit coercion from `Ptr(T)` to `Ptr(Const(T))`, detecting and rejecting const stripping at any pointer nesting level, and allowing struct field reads through `Ptr(Const(Struct))` while preventing mutations. Includes 8 new tests covering const correctness scenarios and expected failures.
- **String standard library (Str module)**: Comprehensive string type (367 lines of tests) with small string optimization that stores strings of 14 bytes or fewer inline without heap allocation. Features construction, comparison, concatenation, slicing, search operations, C interoperability, and dual allocator API.
- **Option and Result builtin types**: Added standard tagged union types to builtin module (`Option(T)` for optional values, `Result(T, U)` for error handling) with generic `unwrap()` function using compile-time type predicates to handle both types.
- **Type-predicate branching over generic tagged unions**: Support for compile-time type predicates in generic functions that branch over different tagged union types with automatic dead branch elimination in transpiled C code.
- **C keyword escaping system**: New `escape_c_keyword()` helper that automatically prefixes C reserved words with `tl_kw_` when emitting identifiers, covering variable declarations, struct fields, tagged union variants, function parameters, and local variables.
- **Documentation enhancements**: Extensive updates covering const pointer syntax and semantics, CArray type annotation syntax, named struct construction, and allocator API changes. Added incompatibility notes for `Const(T)` with generic type parameters.

### Changed

- **CArray syntax redesign (Breaking Change)**: Changed C arrays from value-position constructor (`arr := CArray(Int, 5)`) to type annotation syntax (`arr: CArray(Int, 5) := void`). Pointer decay is now explicit via `Ptr` cast annotations, array size is part of the type, and automatic pointer decay works for CArray struct field access.
- **Allocator API simplification**: Removed `.periodic` allocator and standardized on `Alloc.context.default` as the default allocator across Array.tl and Str.tl standard library modules.
- **String module renamed to Str**: Renamed `String` module to `Str` to avoid naming conflicts with C standard library `string.tl` bindings.
- **C standard library bindings**: Updated `stdio.tl`, `stdlib.tl`, and `string.tl` with `Const()` wrappers on const-correct parameters (e.g., `fopen`, `atoi`, `strlen`, `strcmp`).
- **Type inference improvements**: Enhanced constraint handling to prefer explicit case annotations as wrapper types in generic functions, make constraints non-fatal during generic pass for multi-branch type predicates, ensure reassignment LHS uses symbol's existing type, and support type-annotated void declarations followed by unannotated assignments.

### Deprecated

### Removed

- **Periodic allocator**: Removed `Alloc.context.periodic` from the standard library allocator API.

### Fixed

- **CArray struct field dangling pointer bug**: Fixed critical bug where accessing a CArray field in a struct would create a pointer into a dead temporary by generating lvalue-mode expressions for the entire access chain.
- **Formatter pipe alignment**: Fixed formatter to detect pipe alignment for C union syntax (`{ |`) in addition to tagged union syntax (`: |`).
- **C standard library bindings**: Fixed `c_memcmp` declaration in `string.tl` (was missing size parameter).
- **Name conflicts with Option type**: Updated 16 tagged union test files to avoid naming conflicts after adding `Option` to the builtin module.
- **Type inference edge cases**: Fixed const stripping detection at any pointer nesting level, struct field access through `Ptr(Const(Struct))`, and reassignment type inference to preserve symbol types.

### Security

## [Unreleased] - 2026-01-30 (660c97f..576c968)

### Highlights

- Major tagged union redesign with scoped variants, unscoped constructors, and make functions (breaking change)
- Support for existing types as tagged union variants with module-qualified syntax
- New stable merge sort implementation for arrays with 136 test cases
- Comprehensive standard library API reference documentation

### Added

- **Tagged Union Tests**: 5 new comprehensive tests covering unscoped constructors (`Circle(2.0)`), scoped variant structs (`Shape.Circle`), make functions (`make_Shape_Circle`), existing types as variants, and duplicate variant detection.
- **Standard Library Documentation**: New STANDARD_LIBRARY.md (358 lines) providing complete API reference for Array, Alloc, builtin functions, and stdlib bindings.
- **Array Sorting Functions**: Implemented stable bottom-up merge sort with `sort()` and `sorted()` functions, including comparator support and allocator variants (136 test cases).
- **Etags Support**: New `make tags` target with TL-aware regexes matching only definitions (functions, structs, unions, type aliases, modules, global bindings) while skipping forward declarations.
- **Reserved Type Keyword Validation**: 9 new expected-failure tests covering rejection of `any` and `void` as identifiers in all parser contexts (functions, structs, tagged unions, type aliases, enums, etc.).
- **Build Scripts**: PowerShell equivalents for CMake configuration and build-test scripts (`cmake-configure.ps1`, `cmake-build-test.ps1`) for Windows support.

### Changed

- **Tagged Union Desugaring (Breaking Change)**: Fundamental redesign of tagged union implementation:
  - Variant structs now scoped under union type (e.g., `Shape__Circle`) and accessed via dot syntax (`Shape.Circle`)
  - Constructor functions unscoped at module level (e.g., `Circle(2.0)`) returning the wrapped union type
  - Added per-variant make functions (e.g., `make_Shape_Circle`) that wrap bare variant values
  - Support for existing types as variants using module-qualified syntax (`| Foo.Special`, with `main.Type` for main module types)
  - All 13 existing tagged union tests updated to new syntax
- **Tagged Union Documentation**: Extensively rewritten TAGGED_UNIONS.md (+180 lines net, now 308 lines total) to document scoped variant structs, unscoped constructors, make functions, existing type variants, and double-underscore naming conventions.
- **Language Reference**: Updated LANGUAGE_REFERENCE.md with three tagged union construction methods, existing type variant syntax, and corrected examples throughout (80+ lines changed).
- **Build Scripts Organization**: Moved CMake helper scripts to `tools/` directory with renamed filenames (`cmake-configure`, `cmake-build-test`) to avoid autotools name clashes, improved shell portability and added Windows PowerShell equivalents.
- **Array Tutorial**: Synchronized Array-tutorial.tl with current Array.tl API: renamed `T(a)` to `Array(T)`, added allocator parameters with default-allocator overloads (223 lines changed).
- **Standard Library Type Correctness**: Changed malloc family functions to properly return `Ptr(any)` instead of using cast workarounds.
- **IndexedArray Naming**: Renamed `IndexedArray` to `Array.Indexed` following submodule syntax conventions.
- **Mass Formatting**: Applied `tess fmt` to all 179 .tl source and test files (2,324 insertions, 2,307 deletions).

### Deprecated

### Removed

### Fixed

- **Type Parameter Propagation**: Fixed forward declaration parameter annotations not being copied to function definitions, which caused `free_variable_not_found` errors when type variables from annotations were used in function body.
- **Type Environment Pollution**: Fixed struct field names with type annotations incorrectly polluting the type environment, causing conflicts when the same struct was constructed with different type parameters.
- **Ptr Cast in Struct Fields**: Fixed Ptr cast annotations in struct field initialization (e.g., `Foo(v: Ptr(Int) = c_malloc(...))`) by adding cast detection in both constraint and specialization phases.
- **Generic Existing Type Variants**: Fixed tagged union variants using generic existing types by parsing explicit type args (e.g., `| Foo.Pair(a)`) and properly wiring them to union field annotations.
- **Invalid Type Args Validation**: Added validation to reject invalid type arguments in existing type variant syntax (e.g., `| Foo.Pair(b)` where `b` is not a parent union type parameter), preventing crashes in type.c.
- **Reserved Type Keywords**: Added parser checks to reject `any` and `void` when used as identifiers in all contexts (function definitions, structs, tagged unions, type aliases, enums, unions, forward declarations, annotations).
- **Formatter Pipe Alignment**: Fixed `pipe_col` leaking across blank lines and into comments by resetting on blank lines and skipping pipe group detection on comment lines.

### Security

## [Unreleased] - 2026-01-23 to 2026-01-29

### Highlights

- New code formatter with sophisticated formatting capabilities
- Comprehensive array library with functional programming operations
- Attributes system for function and parameter annotations
- Arity-based function overloading

### Added

- **Code Formatter**: New `tess fmt` subcommand with sophisticated formatting capabilities including multi-line token alignment (`:`, `->`, `:=`, `=`), continuation line indentation, and comment alignment. Supports stdin/stdout and in-place editing with `-i` flag. Includes format-on-save support in Emacs `tl-mode.el`.
- **Array Standard Library**: Comprehensive Array.tl API with element access, mutation, sizing, reordering, bulk operations, search functions, and functional programming operations (`map`, `filter`, `reduce`, `foreach`, `any_of`, `all`, `find`, `find_value`).
- **Attributes System**: New `[[attribute]]` syntax for function and parameter annotations with predicate support (e.g., `x :: [[attribute]]`).
- **Arity-based Function Overloading**: Functions with the same name but different parameter counts can now coexist. Function pointers use `/arity` suffix syntax (e.g., `foo/2`).
- **Nested Structures and Modules**: Support for nested struct definitions and dotted module names (e.g., `#module Foo.Bar`). Added dot syntax for nested types and tagged union variants (e.g., `Shape.Circle`).
- **Short-circuit Evaluation**: Proper short-circuit semantics for `||` and `&&` operators.
- **Compiler Performance Tools**: New `--stats` flag showing per-phase memory and timing statistics, and `--time` flag for elapsed time reporting. Parser now uses speculative arenas for memory-efficient backtracking.
- **MOS Library Utilities**: New string utilities (`str_contains`, `str_starts_with`, `str_replace_char`, etc.), platform abstraction layer (`platform_command_exists`, `platform_temp_file_create/delete`, `platform_exec`, `hires_timer`), and arena statistics API.
- **Documentation**: Added NAME_MANGLING.md, FAQ.md, comprehensive type annotation guidelines, and documentation for attributes, type predicates, and function pointers.

### Changed

- **Name Mangling**: Changed separator from single underscore to double underscore (`__`) to prevent collisions. Parser now rejects identifiers containing `__` (except compiler-recognized prefixes like `__init` and `c__*`).
- **Type System**: Improved generic specialization to avoid over-generalization of concrete functions. Enhanced validation to reject unused generic type parameters in struct definitions.
- **Test Infrastructure**: Reorganized test categories (passing tests, expected failures, known failures, known fail-failures) with improved output and count summaries. Added 9 new function pointer integration tests.
- **Build System**: Enforced test-first development approach. Synchronized Makefile and CMake test definitions and known failures. Separated benchmarks from tests.

### Deprecated

### Removed

- **Positional Struct Initialization**: Removed support for positional struct value initialization. Only named field initialization is now supported.

### Fixed

- Fixed MSVC misoptimization of small string writes through `str_span` alias.
- Fixed specialization issues with lambda arguments, annotated parameters, and unused parameters causing undefined function references.
- Fixed tree shaking incorrectly removing specialized functions referenced as bare symbols or in case expressions.
- Fixed C code generation for functions returning function pointers.
- Fixed buffer overflow in `str_resize`, over-allocation in `str_fmt`, and input validation in `str_parse_num`.
- Fixed negative literals being misidentified as binary operators in formatter.
- Fixed cross-module symbol corruption during parser second pass.
- Fixed Windows compatibility issues including debug build GUI dialogs, incorrect use of temporary return values, and macro conflicts.

### Security

## [Unreleased] - 2026-01-16 to 2026-01-22

### Highlights

- Generic tagged unions with full implementation and comprehensive documentation
- Complete Emacs mode and GitHub Actions CI
- Extensive Windows/MSVC compatibility work (22+ commits)
- Binary literals, scientific notation, logical OR operator, block expressions
- Breaking changes: Tagged union syntax and reassignment type semantics

### Added

- **Generic Tagged Unions**: Full support for generic/polymorphic tagged unions (e.g., `Option(a)`, `Either(a, b)`) with specialization machinery, 9 comprehensive test cases, and 482-line technical documentation (TAGGED_UNIONS.md). Added mutable case binding syntax, exhaustiveness checking, and else clauses in case/match expressions.
- **Language Features**: Binary literal support with `0b`/`0B` prefix, scientific notation with signed exponents, logical OR operator (`||`), block expressions, return statements without arguments, and `String = Ptr(CChar)` type alias in standard library.
- **Developer Tooling**: Complete Emacs major mode (`tl-mode.el`) with syntax highlighting, intelligent indentation, imenu navigation, and comprehensive test suite (1,071 lines). GitHub Actions CI workflow for automated Linux, macOS, and Windows builds. Version flag (`--version`) with automatic version extraction.
- **Build System**: Automatic standard library include paths (searches `<exe_dir>/../lib/tess/std` and `<cwd>/src/tl/std` without requiring `-I` flags). Added `--no-standard-includes` flag to disable automatic includes.
- **Documentation**: Major expansion of LANGUAGE_REFERENCE.md with 200+ lines covering key characteristics, syntax examples, for-in loop iterator interface, mutually recursive types, and case/match predicates. Created 13 new test files covering previously undocumented features. Test-first approach documented in CLAUDE.md.
- **Windows Support**: Full Windows platform abstraction layer with MSVC-specific compiler invocation, proper command-line building, temp file handling, and cross-platform thread-local storage compatibility.

### Changed

- **Breaking Syntax Changes**: Tagged union syntax changed from `Name = | ...` to `Name : | ...`. Reassignments now always have void type to prevent confusing patterns.
- **Type System**: Removed experimental `String` builtin type and `escape_constraint` mechanism, replaced with simpler `Ptr(CChar)` alias. Improved error messages for type inference failures and tagged union issues.
- **Compiler Behavior**: Compiler now rejects returning lambda functions (limitation documented). Parser now allows `{ }` as sugar for `{ void }`.
- **Build & Testing**: Refactored number parsing in tokenizer. Optimized string operations. Simplified arena allocator. Emacs imenu changed to flat list for better usability. Windows CI updated to build and test Release configuration.

### Deprecated

### Removed

- Removed support for `alignof(expr)` due to MSVC incompatibility (kept `alignof(Type)` only).
- Removed experimental `String` builtin type and type constraint escape mechanism.
- Removed 75 lines of obsolete documentation from DEV.md.
- Removed zlib dependency from CMake.

### Fixed

- Fixed extensive Windows/MSVC compatibility issues (22+ commits): struct alignment, `_Thread_local` compatibility, empty struct emission, `typeof` and VLA usage, `max_align_t` typedef, temp file handling and race conditions, CMake compatibility, `alignof(expr)` usage, flexible array members, temp filename echo, and numerous warnings.
- Fixed bugs in Array.tl `reserve()`, break/continue statements in loops, generic tagged union case expressions, `ast_case` node cloning, returns from void functions, and string library operations.
- Fixed syntax errors in documentation, install target in Makefile, CMake version header generation, and missing test registrations.

### Security

## [Unreleased] - 2026-01-01 to 2026-01-15

### Highlights

- Type alias system expansion with module-qualified aliases and 13 new test files
- Module initialization system with `Module.__init()` infrastructure
- Major refactoring of type inference (reduced by ~270 lines)
- Case expression improvements with binary predicates and else cases
- Better cross-compiler support (GCC-15, Clang-19)

### Added

- **Type Alias System**: Expanded support for module-qualified type aliases, direct type alias usage as constructors, type aliases of enums, and generic type aliases with improved type literal unification. Added 13 new comprehensive test files covering local aliases, cross-module aliases, chained aliases, and generic aliases. Known limitation: Partial specialization of type aliases not yet supported (tracked with test).
- **Module Initialization System**: Support for `Module.__init()` functions that are automatically called at program startup in correct dependency order. Enables per-module initialization logic (initially used in `Alloc.tl` for default allocator setup).
- **Case Expression Improvements**: Support for binary predicates with proper type inference, `else` cases, explicit specialization of generic predicates, and improved handling of function pointers in case expressions.
- **MOS Library**: Added `str_ends_with()` function with unit tests and `platform.c` source file.
- **Testing Infrastructure**: Added tests for monkey patch prevention, specialized type constructor regression, allocator improvements, and language feature exploration.

### Changed

- **Type System Improvements**: Improved type alias compatibility for concrete alias target types, integer type literal unification logic, binary operator field name inference, and let-in annotation types as casts.
- **Allocator System**: Improved transient allocator initialization logic (simplified from 22 lines to 5), renamed allocator helper functions for clarity, enhanced module initialization for proper allocator setup.
- **Major Refactoring of Type Inference**: Decomposed massive `infer_traverse_cb()` function (reduced by ~270 lines) into focused helpers: `infer_case()` (58 lines), `infer_let_in()` (92 lines), `infer_named_function_application()` (97 lines), and other extracted helpers. Added documentation with section headers and algorithm overview. Consolidated and cleaned up ~85 lines for improved readability.
- **Build System**: Improved C compiler flag detection to avoid passing unsupported flags, updated Nix flake with build commands and improved Clang-19 compatibility, removed `-O` flag from debug builds for better debugging.
- **Documentation**: Added comprehensive CLAUDE.md (168 lines) documenting Windows/CMake build instructions. Updated documentation with type alias usage examples.

### Deprecated

### Removed

- Removed duplicate code in `infer.c`, unnecessary imports of `builtin.tl` from 10 type alias test files, `TYPE_ALIAS_TODO.md` after completing related work, and accidental debug flag from Makefile.

### Fixed

- Fixed specialized type constructor bug where type constructors weren't properly handled during specialization (added regression test).
- Fixed token line number tracking in tokenizer (off-by-one errors).
- Fixed incorrect buffer length calculation in `infer.c` (caught by GCC warnings).
- Fixed binary operator field name logic edge cases.
- Fixed install target to properly copy standard library files.
- Fixed compilation under GCC-15 and warnings in Nix build environment.
- Fixed Clang workaround to work with Clang-19.
- Fixed transient allocator initialization and lifecycle management issues.

### Security

## [Unreleased] - 2025-12-01 to 2025-12-31

### Highlights

- Iterator interface with for-loops and comprehensive language feature additions
- Type system maturity: Recursive types, type aliases, fixed-width integers, float types
- Standard library growth: Bump allocator, major Array.tl expansion, new utility modules
- Developer infrastructure: Comprehensive Makefile, Emacs mode, 6 major docs, 60+ new tests
- Performance and quality improvements with memory optimizations

### Added

- **Iterator Interface & For Loops**: Comprehensive iterator interface for collections enabling for-statement iteration with `break` and `continue` keywords.
- **Language Features**: C Array support with automatic pointer decay, anonymous lambda arguments, return type annotations, compound assignment operators (`+=`, `-=`, etc.), octal (`0o`) and hexadecimal (`0x`) integer literals, type predicates for runtime type checking, explicit pointer casting, and special `null` literal handling.
- **Type System Enhancements**: Proper recursive and mutually recursive type definitions with placeholder-based resolution, fixed-width integer types (`CInt8`, `CUInt8`, `CInt16`, `CUInt16`, `CInt32`, `CUInt32`, `CInt64`, `CUInt64`), float types (`CFloat`, `CDouble`, `CLongDouble`), comprehensive type alias system with generic parameters and module scoping, improved void/unit type handling with `ast_void`, and `Ptr(any)` support as equivalent to `void*`.
- **Standard Library**: Complete bump allocator in `Alloc.tl` with alignment support and reset functionality. Major Array.tl expansion with iterator interface, initialization functions (`Array.init`, `Array.with_capacity_undefined`, `Array.empty`), comprehensive documentation and tutorial. Added `Unsafe.tl` for low-level operations, `fatal.tl` for error handling, and C library bindings (`atexit`, `_Exit`, `c_assert`).
- **Build System & Tooling**: Comprehensive Makefile supporting multiple configurations (release, debug, asan), parallel builds, test execution, colored output, and installation. CLI options: `--time` flag for performance measurement and `--verbose-ast` for detailed AST output. Complete Emacs major mode with syntax highlighting, indentation, and imenu support.
- **Documentation**: Extensive documentation in `docs/` including LANGUAGE_REFERENCE.md, TYPE_SYSTEM.md, SPECIALIZATION.md, TAGGED_UNIONS.md, NAME_MANGLING.md, FAQ.md, CLAUDE.md for Claude Code guidance, and README.md.
- **Testing**: 60+ new tests covering iterators, for-statements, type system features, tagged unions, function pointers, integer/float types, pointer operations, control flow, assignment operators, lambdas, and expected-failure tests for error validation.

### Changed

- **Type Inference**: Added post-inference pass to catch unresolved types early. Refactored generic specialization for better handling of operands, reassignments, and type constructors. Improved constraint generation and satisfaction for let-in expressions, reassignments, and function pointers. Enhanced error messages for better debugging.
- **Parser**: Made commas optional for separating body elements. Added dedicated `ast_reassignment` node for body element assignments. Improved function pointer parsing in reassignment contexts.
- **Transpiler**: Fixed void function body generation. Added type updating pass for C function calls. Added `_Thread_local` qualifier to toplevel values. Added proper pointer cast code generation. Added C comments in verbose mode for debugging.
- **Build System**: Added initial Windows/MSVC support with CMake improvements. Improved cross-platform compatibility for macOS, Linux, and Windows.
- **Performance**: Multiple memory optimizations including reduced allocations, avoided excessive `map_destroy` calls, reduced string creation in non-verbose mode, extensive use of transient allocator, memoized `parse_type`, and reduced default map load factor.

### Deprecated

### Removed

- Removed obsolete type system functions (`admit_generic_pointers`, `canonicalize_user_types`, redundant specials map, various memoization).
- Removed `tl_weak <-> void` hack after proper void type implementation.
- Removed excessive debug output (printfs, env dbg, resolve_node messages).
- Removed various unused functions, dead code paths, and obsolete C++ preprocessing options.

### Fixed

- Fixed recursive type handling, type annotation handling, struct field type assertions, unresolved types at reassignment sites, `return_null` return type constraints, type constructor specialization `is_specialized` flag, and buffer overflow with very long type names.
- Fixed parser line number tracking (off-by-one error), dot operator parsing, and range checking error.
- Fixed double-dereference bug in pointer handling, reassignment code generation, emission of functions with weak type variables, hash function ancestor detection, and boolean condition handling for `c_assert`.
- Fixed test output using `printf` instead of `echo` for escape sequences. Updated float size tests for macOS compatibility.

### Security

## [Unreleased] - 2025-11-01 to 2025-11-30

### Highlights

- Complete module system with `#module` directive, namespaces, and two-pass parsing
- Major type system enhancements: recursive types, type literals, type aliases, arrow type annotations
- User-defined types: enums, unions, tagged unions, empty structs
- Case expressions for pattern matching with predicates
- C interoperability improvements: direct compiler invocation, `#ifc` blocks, C character literals
- Breaking changes: Nil→Void rename, struct literal syntax changed to round braces `()`

### Added

- **Module System**: Complete module system with `#module` directive, module namespaces, `-I` flag for include paths, two-pass parsing for forward references, module qualifier support in type annotations, and auto-loading of `builtin.tl`.
- **Type System Enhancements**: Full support for recursive and mutually recursive types with pointer recursion detection. Major type literal rework during inference phase with `tl_literal` monotype variant. Type alias declarations and arrow type annotations for function types.
- **User-Defined Types**: Full enum support with C emission, complete union implementation including parsing and C code generation, tagged union variants with type checking, and empty struct support.
- **Control Flow**: Case expressions for pattern matching, case-with-predicate for advanced matching with ident and lambda predicates, while-update loops combining condition and update expressions, improved if expressions with void handling.
- **C Interoperability**: Direct C compiler invocation via fork/exec, `#ifc .. #endc` blocks for raw C code, C character literal support, integer type unification between C types, `c_struct_` syntax for C struct interop, `String`/`Ptr(CChar)` casting, boolean to integer conversions.
- **Build & Tooling**: `#line` directive emission for debugging, `--no-line-directive` flag, CMake build-fail test support, number separator support (underscore in numeric literals).
- **Standard Library**: Added Alloc.tl memory allocation interface, expanded Array.tl with tutorial comments, string comparison with `strcmp` bindings, `alignof` intrinsic (`_tl_alignof_`), `_tl_fatal_` intrinsic for fatal errors.
- **Testing**: 130+ test files covering recursive types, case expressions, module system, enums/unions, C interop, type annotations, lambdas/closures, while-update statements, and regression tests.

### Changed

- **Breaking Changes**: Renamed `Nil` type to `Void` throughout. Added `void` as keyword alias for `nil`. Changed struct literal syntax from `{}` to `()`. Removed `cond` expression (superseded by case). Removed `break` expression with value. Made `#module main` declaration required. Increased operator precedence for `.`, `->`, and `[`.
- **Type System Refactoring**: Major refactoring to parse types during inference phase. Improved type literal processing in operand positions. Enhanced handling of type arguments in function argument and annotation positions. Better distinction between type vs value contexts. Canonicalization of user types for recursive type support. Improved occurs check to allow `Ptr(self)`.
- **Parser Improvements**: Cleaned up type constructor parsing and creation. Better error reporting with file/line/column tracking on all AST nodes. Improved binary operator parsing, especially with integers. Better bitwise operator handling.
- **Code Quality**: Removed unnecessary type clone operations for performance. Improved name mangling for forward declarations and module symbols. Better handling of symbol namespaces and lexical scoping. Thread-local transient allocator for recursive type queries.
- **Build System**: Removed Doxygen from CMake, removed GLFW/GLAD/GNU readline from Nix flake, removed REPL stubs, removed `v3_` prefix from test names.

### Deprecated

### Removed

- Removed `cond` expression, `break` expression with value, `PtrOrNull` union, unity file support, `ptr_or_null` type, `ast_any` AST node variant, and ellipsis type.
- Removed Doxygen documentation generation, GLFW/GLAD graphics library support, GNU readline REPL stubs, and numerous dead/unused functions.
- Removed debugging printfs, logging statements, stub functions, and unnecessary type annotations.

### Fixed

- Fixed type variable sugar with proper type literal arguments, literal unification issues, type args in function positions, type divergence in `let_in` expressions, `sizeof` with type literals, type literal symbol handling, union/struct test failures, and potential type divergence bugs.
- Fixed hash computation for recursive types, forward declaration emission, filename string lifetime in `#ifc` blocks, and emission of unspecialized types.
- Fixed broken unary operator parsing, line number tracking (was skipping lines), and binary operator typing with bitwise operations.
- Fixed three incorrect array indexing bugs in erase operations, incorrect in-place array erase loop, multiple ASAN fixes, and added defensive clone operations for use-after-free prevention.
- Fixed name conflicts between mangled names and lexical names, handling of unknown free variables with proper detection/reporting, string comparison for sorting, function return type constraints, forced `main()` to return `CInt`, and improved error messages for unknown symbols.

### Security
