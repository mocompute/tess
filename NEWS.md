# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased] - 2026-03-15 to 2026-03-24 (c259bf19..49b0fde6)

### Highlights

- **Default string literals changed to `String`** *(breaking)*: `"foo"` now produces a `String`; use `c"foo"` for C strings (`Ptr[CChar]`)
- **Variadic functions with `...Trait` syntax**: last parameter accepts variable arguments dispatched through a trait bound
- **Threading standard library**: `Thread`, `Mutex`, `Cond`, and `Once` modules for cross-platform concurrency
- **`tess cbind` command**: generates `.tl` bindings from C headers automatically
- **`tess fetch` with lockfile support**: downloads package dependencies with SHA-256 verification

### Added

- **Variadic Functions**: The last parameter of a function can use `...Trait` syntax; the compiler applies the trait's unary function to each argument at the call site and packs results into a homogeneous `Slice`. Ships with `ToString` trait and variadic `Print.print`/`Print.println` built on it.
- **Threading Standard Library**: New modules `Thread` (spawn, join, detach, yield, sleep), `Mutex` (plain, recursive, error-check kinds), `Cond` (condition variables), and `Once` (one-time initialization). Cross-platform via pthreads (Unix) / Win32 (Windows). The compiler auto-links `-lpthread` when threading modules are imported.
- **File I/O Standard Library**: `File.tl` provides streaming I/O (open/close/read/write/seek/tell), whole-file convenience functions (`read_file`, `write_file`, `read_lines`), file metadata (`exists`, `is_directory`, `size`, `temp_dir`), and directory operations (`create_dir`, `scan_dir`). `File.Path` submodule handles cross-platform path manipulation.
- **`tess cbind` Command**: Generates `.tl` binding declarations from C headers by preprocessing with `cc -E -dD` and parsing the output. Handles multi-word type specifiers, pointer chains, const qualifiers, function pointers, variadic C functions, opaque structs, typedef aliases, anonymous structs, enums, and `#define` constants.
- **`tess fetch` Command**: Fetches package dependencies declared in `package.tl`, downloads `.tpkg` archives from URLs, and generates/verifies `package.tl.lock` files with SHA-256 hash verification.
- **Const Value Bindings**: `x: Const := 5` or `x: Const[Int] := 5` prevents reassignment and transpiles to C `const`. For-loop variables are implicitly `Const`.
- **C Compiler Predefined Macros**: The compiler queries `cc -dM -E` at startup, making platform macros like `__linux__`, `_WIN32`, and `__APPLE__` available to `#ifdef`/`#ifndef` without manual `-D` flags.
- **C Preprocessor Macros as CArray Sizes**: `CArray[Byte, c_TL_ONCE_SIZE]` uses platform-specific `#define` values for opaque type sizing via a new `tl_c_macro` monotype tag.
- **Opaque C Type Support**: Symbols with `c_` prefix are auto-registered as opaque nullary types, so `Ptr[c_FILE]` works without manual type definitions.
- **User-Defined Traits on Builtin Types**: Builtin types (Int, UInt, Float, Bool, etc.) now have module names, allowing user code to define trait implementations that dispatch correctly.
- **`s"..."` String Literal Prefix**: Convenient `String` construction — `s"hello"` instead of `String.from_cstr(c"hello")`.
- **`Result[Void, E]` Support**: Void fields in structs now transpile correctly, enabling the natural return type for fallible operations that produce no value.
- **Cross-Type `try`**: `try` now only requires matching error variant types between the operand and the enclosing function's return type, allowing `try` across different success types.
- **`ToInt` Trait**: Standard library trait for unary integer conversion, with implementations for Int, UInt, Float, Bool, CString, and String.
- **Non-Diverging Else in Variant Bindings**: The `else` branch of a variant binding can now produce a fallback value instead of requiring divergence.
- **Cross-Kind Function/Type Name Sharing**: Functions and types can share the same user-visible name within a module, since they occupy separate namespaces.
- **Cross-Chain Integer Widening for UFCS**: When UFCS resolves a trait method whose parameter is on a different integer subchain from the receiver, the compiler inserts a synthetic cast.
- **`CONFIG=coverage` Build**: LLVM source-based code coverage support.
- **Improved Error Messages**: Trait-qualified calls like `Hash.hash(42)` now suggest UFCS syntax; bare function names missing `/N` arity suffix are detected and reported.
- **Version/Date Comment in Transpiled Output**: Generated C files now include the tess version, git branch, and timestamp.

### Changed

- **Default String Literals are Now `String`** *(breaking)*: `"foo"` produces a `String` value; use `c"foo"` for C strings (`Ptr[CChar]`). An `upgrade_strings` migration tool was provided.
- **`Str` Renamed to `String`**: Throughout the language, standard library, tests, and documentation.
- **`Cmdline` Renamed to `CommandLine`**.
- **`std*.tl` Wrappers Renamed to `cstd*.tl`**: e.g., `stdlib.tl` to `cstdlib.tl`, `string.tl` to `cstring.tl`.
- **"Let-else" Renamed to "Variant Binding"**: and "let-in expression" renamed to "binding expression" in all user-facing documentation.
- **Hash Trait Moved to Userland**: `Hash` is now defined in `Hash.tl` with UFCS-based implementations. Callers use `x.hash()` instead of `hash(x)` and must `#import <Hash.tl>`.
- **`print()` Moved to `Print.tl`**: Now built on the variadic `...ToString` mechanism instead of being a builtin.
- **Bump Allocator Moved to `Alloc.BumpAllocator` Submodule**.
- **Versioned `.tpkg` Filenames Required**: Only `<Name>-<Version>.tpkg` is accepted; the unversioned fallback was removed.
- **`tess validate` Merged into `tess pack --validate`**: The standalone validate command was removed.
- **HashMap Performance**: Robin Hood hashing with backwards-shift deletion, murmurhash3 finalizer, hash-tag fast-reject in lookups, and compact entry metadata. Permanent hash caching for concrete monotypes.
- **`cstdio.tl` Expanded to Full C11 Coverage**: Type-safe `Ptr[c_FILE]` stream parameters throughout.
- **Formatter Improvements**: Fixed cross-alignment inside tagged union variant bodies, alignment for single-line function defs, type parameter constraint colons, arrow alignment, and binary operator spacing.
- **Documentation Overhaul**: Major updates to Language Reference (pointers, submodules, terminology, `main()` forms, bitwise operators, `#ifc`/`#endc`, builtin traits) and expanded CLAUDE.md.

### Removed

- **`unwrap()` Removed**: Variant bindings are the preferred pattern for extracting values.
- **`depend_optional()` Removed**: Optional dependencies had no mechanical effect; documentation handles that use case better.
- **Unused Sexp Parser Removed**.
- **Redundant `.&` in UFCS Calls Removed**: Implicit address-of makes explicit `.&` unnecessary throughout tests and stdlib.
- **Unnecessary `zu` Integer Suffixes Removed**: Type inference handles them automatically.

### Fixed

- Fixed fully-generic type alias segfault (`Foo = Option` without type parameters no longer crashes).
- Fixed `c_printf` declaration to return `CInt` instead of `Void`.
- Fixed trait dispatch for generic functions on nullary types (e.g., `CLongDouble`).
- Fixed `CLongDouble` truncation in float formatting.
- Fixed `Ptr`-receiver trait methods on builtin types.
- Fixed weak int defaulting overriding resolved type bindings.
- Fixed Windows `small` macro from `Windows.h` breaking `String` field access.
- Fixed missing return for `when`-expression with diverging arm.
- Fixed diverging `when`/`if-else` arms in tail position not detected correctly.
- Fixed parse type dropping return type on partially-annotated arrows.
- Fixed `if-then-else` divergence detection.
- Fixed `cbind` const pointer handling (`T * const` now correctly wraps `Const` outside `Ptr`).
- Fixed `needs_pthread` uninitialized in `state_init` (debug builds always linked `-lpthread`).
- Fixed stdlib path normalization preventing `tess pack` failures with installed tess.

### Security

- Version string validation now rejects `=` characters to prevent ambiguous dependency encoding in `.tpkg` archives.
- SHA-256 hash verification for downloaded `.tpkg` packages in the `tess fetch` pipeline.

## [Unreleased] - 2026-03-11 to 2026-03-15 (ec70d2ab..c259bf19)

### Highlights

- **Hash trait and HashMap redesign**: compiler-provided `Hash[T]` trait with FNV-1a hashing, replacing HashMap's internal function pointer with trait-based dispatch
- **UFCS receiver coercion**: the `.` operator now auto-references and auto-dereferences receivers, eliminating the need for separate `_ptr` function variants; the `->` UFCS operator is removed
- **Embedded standard library**: the compiler binary bundles all `.tl` standard library files, making it fully self-contained
- **Cmdline standard library module**: declarative command-line parsing with flags, options, positional arguments, and auto-generated help
- **Standard library migrated from UInt to CSize**: all size/count/index values now use `CSize` (`size_t`), matching C conventions

### Added

- **Hash Trait**: A compiler-provided `Hash[T]` trait with FNV-1a hashing and intrinsic dispatch for builtin types. User-defined types can implement `hash()` in their module. HashMap now uses `Hash` and `Eq` traits instead of internal function pointers and `memcmp`.
- **`[[no_conform(Trait)]]` Attribute**: Opt-out mechanism for trait conformance, blocking trait bounds and operator dispatch for annotated types, with inheritance to derived types.
- **UFCS Receiver Coercion**: When calling `val.f()` where `f` expects `Ptr[T]`, the compiler implicitly takes the address of the receiver. Pointer receivers are auto-dereferenced when the function expects a value type.
- **Cmdline Standard Library Module**: Declarative command-line parsing supporting flags, string options, boolean options, and positional arguments with built-in help generation.
- **Implicit Submodule Visibility**: Within a module, child submodule names are accessible without the parent prefix (e.g., `Args.ArgValue` instead of `Cmdline.Args.ArgValue`). Bare submodule type names auto-collapse in type positions.
- **Embedded Standard Library**: The compiler binary bundles all `.tl` standard library files via a tpkg archive (built by a new `stdlib_pack` tool), making the compiler self-contained without needing to locate stdlib on disk.
- **Semicolons as Expression Separators**: Semicolons are now accepted in addition to commas for separating expressions.
- **Leveled Verbose Output**: Replaced the all-or-nothing `-v` flag with three levels (`-v`, `-vv`, `-vvv`): phase markers, key decisions, and full detail.
- **Diverging Function Detection**: `c_exit` and `tl_fatal` are now recognized as never-returning, enabling better control flow analysis.
- **Undeclared Variable Detection**: Using `=` (reassignment) on an undeclared name now produces a clear error instead of confusing downstream type inference failures.
- **License**: Added LICENSE (Apache 2.0) and NOTICE files.
- **Version String Improvements**: Version strings now include architecture and OS (e.g., `0.1.0-ad8458b-x86_64-linux`), with `GIT_HASH` env var support for Nix builds.
- **HOF/Closure Test Suite**: Comprehensive tests for higher-order functions, closures with captures, nested closures, closures in structs, and generic closures.

### Changed

- **Standard Library Migrated to CSize**: All size, count, index, and capacity values across Str, HashMap, Array, Alloc, and Unsafe now use `CSize` (maps to C `size_t`). Cross-subchain standalone integer conversions are rejected, requiring explicit type annotations.
- **tlib Renamed to tpkg**: The package archive format renamed across the entire codebase (`tlib.c` → `tpkg.c`, `tlib.h` → `tpkg.h`, all API functions).
- **Standard Library Modernized**: `Array.tl`, `Str.tl`, `Alloc.tl`, and `HashMap.tl` rewritten to follow new coding conventions: type annotations removed from implementations (relying on inference), `when/else` and `let-else` used throughout.
- **Module Re-opening**: Modules can now be re-opened after submodule definitions, allowing continued additions to a parent module.
- **Trait Bounds on Forward Declarations**: Forward-declared functions now support trait bound syntax.
- **Link-Time Optimization**: LTO enabled for release builds.
- **`make test` Independence**: Test targets now depend on the build properly, no longer requiring a separate `make all` first.
- **TMPDIR Respected**: Temp file and directory creation on POSIX now uses the `TMPDIR` environment variable.
- **CChar Signedness**: CChar is now neither signed nor unsigned, fixing contradictory type classification.

### Removed

- **UFCS `->` Operator**: Since `.` now auto-dereferences pointer receivers, `->` for UFCS is redundant. It remains available only for struct field access through pointers (`ptr->field`).
- **`Array.find_value`**: Removed from the standard library.
- **Obsolete Documentation**: Old tlib implementation document and hashmap iteration bug document deleted. The nonexistent `mut` keyword removed from documentation.

### Fixed

- Fixed `when/else` codegen on tagged unions where the else arm was not wrapped in an else block, causing incorrect C output.
- Fixed `let-else` with bindings in the else block producing incorrect code.
- Fixed forward declaration annotation copy losing type parameter names, and parameter type merging producing incorrect signatures.
- Fixed const stripping safety: added pre-unification guards for struct constructor fields, return statements, and reassignments, preventing `Ptr[Const[T]]` from being silently accepted where `Ptr[T]` was expected.
- Fixed for-loop iterator name collision by replacing hardcoded `gen_iter` with unique generated names, preventing silent collision with user bindings.
- Fixed `free_variable_not_found` errors now pointing to the exact usage site instead of the enclosing function's closing brace, with deduplication.
- Fixed format string injection in `_tl_fatal_` where user strings containing `%` were passed as format arguments.
- Fixed formatter inserting a blank line incorrectly in multi-line argument lists.
- Fixed alignment pass token ordering for tagged union variants in the formatter.
- Fixed UFCS dot operator on pointer receivers, resolution for types in submodules, and implicit address-of for nullary mutating functions.
- Fixed SSO use-after-scope in stdlib_pack entry names.
- Fixed Windows path normalization for drive prefix separators.
- Fixed CMake link error for `mos_embed` and TL test build with non-Ninja generators.
- Fixed version header staleness by tracking `.git/HEAD` and `.git/packed-refs` for regeneration.
- Fixed implicit submodule names incorrectly shadowing top-level modules.

### Security

- Fixed format string injection in transpile.c where user-provided strings were spliced directly into `fprintf`'s format position, making `%` characters in error messages undefined behavior.

## [Unreleased] - 2026-03-07 to 2026-03-11 (c5101ccd..ec70d2ab)

### Highlights

- **Heap-allocated closures** (`[[alloc]]` / `[[alloc(expr)]]`): closures can now safely escape their defining stack frame, with the compiler enforcing escape analysis and rejecting invalid captures
- **Package-versioned symbol names**: module symbols now embed their package version in the C name, enabling diamond dependency graphs without link-time symbol collisions
- **Multi-version package coexistence**: different packages can depend on different versions of the same library and coexist at link time
- **`c"..."` string literal prefix removed** *(breaking)*: plain string literals are now C strings directly; the `c` prefix no longer exists
- **Parser split into focused files**: `parser.c` refactored into `parser_expr.c`, `parser_types.c`, `parser_tagged_union.c`, and `parser_statements.c`

### Added

- **Heap-Allocated Closures**: Closures annotated with `[[alloc]]` or `[[alloc(expr)]]` have their captured context heap-allocated, allowing them to outlive their defining stack frame. The compiler generates a separate `tl_alloc_ctx_` struct with value-copy fields. `[[alloc(expr)]]` accepts a custom allocator expression with enforced type validation. All Tess functions now receive a `void* tl_ctx_raw` first parameter enabling uniform direct and indirect dispatch via a unified `tl_Closure {fn, ctx}` struct; C FFI thunks are generated for raw function pointer interop.
- **Closure Escape Analysis**: The compiler now rejects closures that escape their stack frame without `[[alloc]]`. Escape checking, alloc validation, and capture scope validation are unified into a single pass. Transitive capture (outer closure calls inner closure by name) correctly propagates free variables.
- **Package-Versioned Name Mangling**: Module symbols in packages now receive a `pkg__ver__` prefix in their C names (e.g., `mylib__1_2_0__Math__add__2`), preventing symbol collisions in diamond dependency graphs. Applied at a single chokepoint in `mangle_str_for_module()`; standalone files, `main`, `builtin`, `c_*` symbols, and `c_export` wrappers are unaffected.
- **Multi-Version Package Coexistence**: Loaded dependencies are now keyed on `"name=version"` rather than bare name. Per-file prefix maps ensure each source file resolves symbols according to its owning package's dependency tree, so two packages depending on different versions of the same library coexist without conflict.
- **Stdin Compiler Input**: The `c`, `exe`, and `run` commands now accept `-` as a filename to read source from stdin (`echo '...' | tess run -`). Error messages and `#line` directives use `<stdin>` as the virtual filename.
- **`tl_source_scanner_collect_modules()` API**: Added to the shared source scanner API, replacing a fragile manual `#module` line scanner that incorrectly matched inside string literals and comments.
- **`platform_make_c_identifier` / `str_make_c_identifier` Helper**: Extracted from `embed.c` into `platform.c`. Fixed: now replaces all non-alnum characters with `_` (previously dropped them silently), adds proper `(unsigned char)` cast, and returns output length.
- **17 new stress-test integration tests**: Covering generic closures specialized at multiple types, trait-bounded generics with operator overloading, weak int literals through nested generics, generic tagged unions with type aliases, higher-order generic functions returning tagged unions, recursive generic types with closures, and module-scoped generic tagged unions.
- **New documentation**: `docs/LANGUAGE_MODEL.md` explains let-in expressions, scoping, mutation vs. rebinding, closures, and pattern matching. `docs/SPECIALIZATION.md` rewritten with the 7-phase inference pipeline, clone/rename process, and closure handling.
- **NixOS clangd config generator**: `tools/gen-clangd-config.sh` discovers the correct clang resource directory and glibc include path from the Nix environment and writes a `.clangd` config file.

### Changed

- **`c"..."` String Literal Syntax Removed** *(breaking)*: The `c"..."` prefix that produced raw C string pointers has been removed from the language. Plain string literals now produce C strings directly. All standard library files, tests, and examples updated; `Str.tl`'s `from_literal` prelude simplified.
- **Unified Calling Convention**: All transpiled Tess functions now accept `void* tl_ctx_raw` as their first parameter, enabling uniform dispatch whether called directly or through a closure pointer.
- **Parser Split Into Multiple Files**: `parser.c` (grown very large) was split into `parser_expr.c`, `parser_types.c`, `parser_tagged_union.c`, and `parser_statements.c`, with `parser_internal.h` for shared declarations. Makefile and CMakeLists.txt updated.
- **`str_qualify` Helper**: Added `str_qualify(alloc, parent, child)` in `ast.h` to replace the recurring `str_cat_3(alloc, lhs, S("__"), rhs)` pattern across 10 call sites in tagged union parsing.
- **Capture Scope Validation Simplified**: The separate two-pass capture scope checker replaced by a single-pass approach: captured symbols in scope get alpha-renamed; those out of scope keep an empty `symbol.original`, making scope errors detectable via a simple field check. Removed ~170 lines of `check_capture_scope_walk` and related code.
- **Module Symbol Storage Deduplicated**: `save_current_module_symbols()` previously stored symbols under both versioned and unversioned keys. The unversioned key was unreachable (resolver always prefers versioned), so the redundant `map_copy` and second allocation were removed.
- **`make_version_key` Helper Extracted**: Deduplicates versioned module symbol key construction (`pkg::ver::Module`) that previously appeared in three places.

### Removed

- **`c"..."` string literal syntax** — removed from tokenizer, parser, and AST; all usages updated to plain string literals.
- **`fatal.tl`** standard library file — functionality merged into `builtin.tl`; import statements updated.
- **Manual `#module` line scanner in `tess_exe.c`** — replaced by the comment/string-aware `source_scanner` API.
- **Redundant `map_copy` in `save_current_module_symbols`** — unreachable dead code removed.

### Fixed

- Fixed type alias names not being normalized before specialization cache lookups, causing duplicate C struct instantiations when an alias like `"Point"` and its canonical form `"Point__T"` produced different cache keys.
- Fixed null pointer dereference in `add_generic` when a parameter's arrow annotation references an unknown type (`make_arrow` returns NULL; guarded with `tl_polytype_is_scheme` check).
- Fixed alpha conversion skipping annotations on toplevel let-in names (level 0), now properly traversed.
- Fixed stale type variables in sibling types at recursive generic multi-specialization boundaries.
- Fixed `registry_args_hash` ignoring arrow free variables nested inside structs (the old save/restore only stripped FVs from immediate arrow args); fixed with a `hash_ignore_fvs` flag propagated through `tl_monotype_hash64_`.
- Fixed `#module` false-matching inside string literals and comments in the old manual scanner.
- Fixed formatter emitting a spurious standalone `=` for 3-character compound assignment operators (`<<=`, `>>=`) — was skipping 1 character instead of 2.
- Fixed formatter not preserving spaces around `[[...]]` attribute brackets.
- Fixed codegen for allocating closures whose context returns a struct type.
- Fixed shorthand return of allocated closure values.
- Fixed transitive closure capture: outer closures calling inner closures by name now correctly inherit the inner closure's free variables.
- Fixed closure escape analysis not tracing alloc closures through if/else branches.
- Fixed free variable collection for named-function-application callee nodes (not just their arguments).

## [Unreleased] - 2026-03-04 to 2026-03-07 (eeee2aa4..c5101ccd)

### Highlights

- Operator overloading for user-defined types with automatic derivation of `!=`, comparisons, and compound assignments
- Traits system with declarations, bounds checking, inheritance (including diamond), and built-in type conformance
- Unknown type name detection in annotations, catching previously-silent errors in nested generics and return types
- Test infrastructure overhaul: ~350 test files reorganized into auto-discovered subdirectories, removing ~660 lines of manual test lists
- Unsuffixed integer literals (`42`) are now family-agnostic, unifying with any integer type

### Added

- **Operator Overloading**: Binary operators (`+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`, `==`, `!=`) and unary operators (`-`, `!`, `~`) on user-defined types are rewritten to function calls (e.g., `a + b` becomes `Module__add__2(a, b)`). The `!=` operator is automatically derived from `eq`, and comparison operators (`<`, `<=`, `>`, `>=`) are derived from `cmp`. Compound assignments like `+=` work with overloaded operators.
- **Traits System**: New syntax for declaring traits (`Name[T] : { sig(a: T) -> T }`) with trait inheritance, diamond inheritance support, and circular inheritance detection. Type parameters can be bounded with `[T: Trait]`, verified at specialization time with recursive conformance checking. Built-in types (Int, UInt, Float, Bool, etc.) are verified against built-in operator traits (Add, Sub, Eq, Ord, etc.).
- **Unknown Type Name Detection**: The compiler now rejects undefined type names in all annotation positions — simple variable annotations (`x: Quux`), arrow return types (`make() -> Quux`), and nested generics (`Ptr[Ptr[Quux]]`). Previously these silently created fresh type variables.
- **Module Type Auto-Collapse for `T`**: A module type named `T` (e.g., `Vec2.T`) is now automatically aliased to the bare module name (`Vec2`), extending the existing same-name collapse behavior.
- **Unsuffixed Integer Literal Polymorphism**: Unsuffixed integer literals like `1` and `42` are now family-agnostic — they can unify with any integer type (signed, unsigned, or standalone like CSize, CPtrDiff, CChar), fixing cases like `i += 1` when `i` is `UInt` or `CSize`.

### Changed

- **Test Auto-Discovery**: ~350 test files moved from a flat directory into categorized subdirectories (`pass/`, `pass_optimized/`, `fail/`, `fail_runtime/`, `known_failures/`, `known_fail_failures/`). Both Makefile and CMakeLists.txt rewritten to use wildcard auto-discovery, removing ~660 lines of manual test lists. Adding a test now requires only dropping a file in the right subdirectory.
- **`#alias` Operand Order Swap** *(breaking)*: `#alias` now uses LHS = RHS convention (`#alias NewName Source` instead of `#alias Source NewName`). All existing usage updated.
- **Explicit Type Parameters Required** *(breaking)*: The implicit `type_variable_sugar` fallback that silently created fresh type variables for unknown symbols in annotations was removed. Generic functions now require explicit type parameter brackets (e.g., `free[T](...)` instead of `free(... Ptr[T])`).
- **Standard Library Updates**: `unwrap` changed from `unwrap[T]` to `unwrap[T, U]` for proper `Result[T, U]` handling. `HashMap.tl` updated to use unsuffixed integer literals. `_tl_fatal_` forward declaration moved to `builtin.tl`.
- **Trait Internals**: `tl_trait_def` now stores a `source_node` pointer, eliminating an O(N) linear scan for trait source locations.

### Removed

- **`type_variable_sugar`**: The implicit type variable creation fallback and all its call sites were removed entirely.
- **Manual Test Lists**: ~660 lines of explicit test file listings removed from Makefile and CMakeLists.txt, replaced by auto-discovery.

### Fixed

- Fixed stack-buffer-overflow in `replace_tv_mono` where a 4-byte type variable was stored into a hashmap expecting an 8-byte monotype pointer, causing out-of-bounds reads during memcpy.
- Fixed multi-type recursive type cycles (`A -> B -> C -> A`) by correctly deferring forward references during UTD parsing.
- Fixed nested tagged union divergence analysis so `case`/`when` nodes check whether all arms diverge, enabling nested let-else patterns inside case arms.
- Fixed false positives in unknown type detection by alpha-converting let-name annotations.
- Fixed diamond inheritance being incorrectly flagged as circular in trait inheritance checking.
- Fixed an integer narrowing issue with narrow integer types.

## [Unreleased] - 2026-02-28 to 2026-03-04 (c98a9048..eeee2aa4)

### Highlights

- Uniform Function Call Syntax (UFCS): `x.foo(a, b)` desugars to `foo(x, a, b)`, with struct field priority, chaining, and cross-module support
- Float type conversion system with weak float literals, directional width checking, and debug bounds-check assertions
- HashMap standard library module: Robin Hood open-addressing hashmap written in pure Tess
- `infer.c` split into 5 focused compilation units for maintainability
- Auto-collapse for same-name module types (e.g., `Array[Int]` instead of `Array.Array[Int]`)

### Added

- **Uniform Function Call Syntax (UFCS)**: `x.foo(a, b)` desugars to `foo(x, a, b)` when `foo` is not a struct field of `x`. Struct fields always take priority. Works with the `->` operator (passing a pointer as first argument), supports chaining (`v.scale(3).length_sq()`), and works with generic structs and generic functions. Cross-module UFCS is supported via `x.Mod.foo(a, b)` syntax.
- **Float Type Conversion System**: Full float sub-chain (`CFloat` < `CDouble` < `CLongDouble`) with width ordering, mirroring the integer sub-chain design. Float literals like `3.14` receive a weak polymorphic type that adapts to context (defaulting to `CDouble`). Implicit widening within the sub-chain, explicit narrowing and float-to-integer/integer-to-float casts via let-in annotation. Debug bounds-check assertions fire on overflow, precision loss, or NaN.
- **HashMap Standard Library Module**: Robin Hood open-addressing hashmap with backward-shift deletion, written in pure Tess. Includes `create`, `set`, `get`, `remove`, `contains`, `size`, `clear`, `keys`, `values`, `entries`, and custom hash/equality function support.
- **Auto-Collapse for Same-Name Module Types**: When a module defines a primary type with the same name as itself (e.g., `Array` module defines `Array[T]`), the compiler auto-registers the bare module name as a type alias. Client code can write `Array[Int]` instead of `Array.Array[Int]` — both forms are interchangeable.
- **Generic Function References with Explicit Type Arguments**: New `name[TypeArgs]/N` syntax (e.g., `_default_hash[Int]/1`) for taking references to specific specializations of generic functions as function pointers.
- **`&=` and `|=` Compound Assignment Operators**: The tokenizer now emits these as single tokens, enabling bitwise compound assignment.
- **`str_build_cat_n`**: New MOS library function to concatenate into a string builder without allocating an intermediate string.
- **Header Dependencies in Makefile**: Object files now correctly depend on their headers, triggering rebuilds when headers change.

### Changed

- **`infer.c` Split Into 5 Compilation Units**: The monolithic ~6000-line `infer.c` was split into focused files: `infer.c` (orchestration/API), `infer_alpha.c` (alpha conversion), `infer_constraint.c` (loading/inference/constraints), `infer_specialize.c` (monomorphization), and `infer_update.c` (tree shaking/type updates), with `infer_internal.h` for shared declarations.
- **Tagged Union Construction Returns Parent Type**: All tagged union construction paths now consistently return the parent union type. `Shape.Circle(radius = 2.0)` and `Circle(radius = 2.0)` both return `Shape`. Bare-symbol scoped variants (`Op.A`) also produce the parent type.
- **Specialization Pipeline Refactor**: Type argument resolution now happens once at the callsite, with pre-resolved monotypes passed throughout the specialization chain. A shared `parse_type_arg` helper consolidates resolution.
- **Integer Type Properties Data-Driven**: Replaced ~104 string comparisons across 5 functions with field accesses on a centralized `builtin_nullary[]` table extended with C type names and integer range fields.
- **Parser Deduplication**: Extracted shared `parse_param_list()` for arrow types and function definitions, unified struct/union type definitions via `create_utd()`/`finalize_type_definition()`, centralized tagged union field name literals as constants, and added CArray helper functions.
- **`unpack` Command Merged Into `pack`**: The standalone `unpack` command was absorbed into `pack` as the `--unpack` flag. `--list` also moved to `pack`.
- **Type Constructor Display Uses Brackets**: Type constructor arguments are now displayed with `[]` instead of `()` (e.g., `Array[Int]` not `Array(Int)`).
- **Type Annotation Style**: Documentation updated to use `x: Type` consistently (no space before colon).
- **`test_tlib` Dynamic Buffer**: Replaced fixed 8KiB static buffer with arena-based dynamic file reading, preventing silent truncation of large files.
- **CMake Runtime Fail Tests**: Wrapped with `cmake -E env` for portable SIGABRT handling via CTest's `WILL_FAIL`.

### Removed

- **Tagged Union Make Functions and Existing-Type Variants**: Removed per-variant `make` wrapper functions and support for using existing types as tagged union variants. Scoped construction handles these cases directly.
- **Old-Style `Type(T, U)` Syntax**: The parser no longer accepts parenthesized type constructor arguments. The bracket syntax `Type[T, U]` is now the only form.
- **Dead Code Cleanup**: Removed `if(0)`/`if(1)` blocks, commented-out code, unused functions, stale `DEBUG_INVARIANTS` checks, and outdated plan documents from the inference and transpilation passes.

### Fixed

- **`sizeof[T]` Type Confusion Across Call Sites**: `sizeof[Inner[K,V]]()` and `sizeof[Outer[K,V]]()` inside generic functions no longer share a single specialization. Fixed by passing the outer type argument context and skipping the cache when type args remain non-concrete.
- **Generic Function Calls with Explicit Type Args**: Fixed two bugs preventing `make[Int]()` from working: type literal specialization incorrectly diverting value constructors, and type argument lookup being skipped.
- **Generic Types in Function Pointer Parameter Lists**: `Ptr[K]` in function pointer type parameter lists was parsed as bare identifier `Ptr`, leaving `[K]` unconsumed.
- **Generic Struct Null Function Pointer Fields**: `Foo[Int](f = null)` no longer leaves pointer type variables unresolved.
- **`str_init_f64` Buffer Overread**: Large doubles (e.g., `1e300`) could overflow the formatting buffer. Changed to `%.17g` with length capping and `.0` suffix for integer-valued outputs.
- **Type Pollution Across Specializations**: Type arguments from one specialization could leak into another during traversal.
- **Module Name Mangling for Function References**: Function references with explicit type args now correctly apply module mangling.
- **Aliased Type Mutation in `make_instance_key`**: Substitution could corrupt the AST by mutating a direct pointer. Fixed by cloning before substituting.
- **Formatter Fixes**: Preserve arity syntax after type args (e.g., `foo[Int]/1`), skip `#ifc`/`#endc` blocks in alignment pass.
- **Tokenizer `in_equal` EOF Fallback**: Copy-paste bug emitted wrong token type on EOF.
- **Empty `while` Body**: Parser now allows empty while loop bodies.
- **Arena Leak in `test_type_v2`**: Fixed for ASAN builds.

### Security

- **Debug Bounds Checking Extended to Float Types**: The runtime bounds-checking system now also checks float narrowing (verifies narrowed result is finite when original was) and float-to-integer conversion (verifies value is in target range and not NaN), aborting with a diagnostic message on failure.

## [Unreleased] - 2026-02-23 to 2026-02-28 (981ea8c5..c98a9048)

### Highlights

- Integer type safety: implicit widening within sub-chains, explicit narrowing and cross-chain conversions via let-in annotation casts
- Weak (polymorphic) integer literals: `42` adapts to any signed integer context, `42u` to unsigned, `42z`/`42zu` for `CPtrDiff`/`CSize`
- Seven integer sub-chains with width ordering replace the old bidirectional same-family unification
- Directional unification in the type checker (expected/actual semantics) for integer type checking
- Debug-mode runtime bounds checking on narrowing casts
- Tagged union cleanup: scoped construction always returns the union type, make functions and existing-type variants removed
- `Unsafe` integer conversion functions removed in favor of the let-in annotation syntax

### Added

- **Integer Sub-Chain Ordering**: Seven sub-chains define the integer type hierarchy: C-named signed (`CSignedChar` < `CShort` < `CInt` < `CLong` < `CLongLong`), C-named unsigned (`CUnsignedChar` < `CUnsignedShort` < `CUnsignedInt` < `CUnsignedLong` < `CUnsignedLongLong`), fixed-width signed (`CInt8` < `CInt16` < `CInt32` < `CInt64`), fixed-width unsigned (`CUInt8` < `CUInt16` < `CUInt32` < `CUInt64`), and three standalone types (`CSize`, `CPtrDiff`, `CChar`). Implicit widening is only permitted within a single sub-chain, following the width ordering.
- **Weak Integer Literals**: Integer literals are now polymorphic. Unsuffixed `42` receives a weak signed literal type that resolves to any signed integer from context (defaulting to `Int`). The `u`/`U` suffix produces a weak unsigned literal (defaulting to `UInt`). New `z`/`Z` and `zu`/`ZU` suffixes produce concrete `CPtrDiff` and `CSize` literals respectively for size-typed contexts.
- **Directional Unification**: The type checker now distinguishes expected (target) and actual (source) types in integer comparisons. Three modes: `SYMMETRIC` (legacy, no directional checking), `DIRECTED` (widening OK, narrowing rejected), and `EXACT` (same concrete type required). Function parameters, return types, let-in bindings, and assignments use `DIRECTED`; operators, conditionals, and generic type variables use `EXACT`.
- **Integer Cast Annotations**: The existing let-in type annotation mechanism (previously used for pointer casts) is generalized to all integer type conversions. Writing `narrow: CInt := int_val` performs an explicit narrowing cast; `fixed: CInt32 := cint_val` performs a cross-chain cast. This is the only syntax for explicit conversion — no `as` keyword or cast function.
- **Debug Bounds Checking**: In debug mode, narrowing conversions emit a runtime bounds check (`_tl_assert_bounds_`) before the C cast that verifies the value fits in the target type's range. Controlled by optimization mode (on for debug, off for release).
- **Compile-Time Literal Overflow Detection**: When a weak literal resolves to a concrete type, the compiler checks that the literal value fits in the target type's range, producing a compile-time error on overflow. Correctly handles negative literals (e.g., `-129` rejected for `CInt8`).
- **Tagged Union Tests**: New tests for scoped construction (`Shape.Circle(radius = 2.0)` returning `Shape`), cross-module positional construction, `when`/case with type annotations, generic scoped variants, bare-symbol scoped variants (`Op.A`), and multi-union-same-module scenarios.
- **30+ New Integer Tests**: Comprehensive test coverage including implicit widening, narrowing rejection, cross-chain rejection, cross-family rejection, cast annotations, weak literal resolution, `z`/`zu` literals, operator exact match, conditional exact match, generic exact match, bounds checking, literal overflow, and compound assignment type matching.
- **Tagged Union Parser Documentation**: New `docs/TAGGED_UNION_PARSER.md` documenting the three construction code paths, critical parser invariants, and the historical dead-code bug to prevent recurrence.

### Changed

- **Integer Unification is Now Directional (Breaking Change)**: Integer types no longer unify freely within the same family. Narrowing conversions (e.g., `Int` to `CInt`) and cross-chain conversions (e.g., `CInt` to `CInt32`) now require explicit let-in annotation casts. Existing code that relied on implicit narrowing or cross-chain conversion will need annotations added.
- **Operators Require Exact Type Match (Breaking Change)**: Arithmetic and comparison operators now require both operands to have the exact same integer type. `a: CInt + b: CShort` is a type error; widen explicitly or use weak literals.
- **Compound Assignment Requires Exact Type Match (Breaking Change)**: Compound assignment operators (`+=`, `-=`, etc.) now enforce that the RHS type exactly matches the LHS type, consistent with regular binary operators.
- **Conditionals and Case Arms Require Exact Type Match (Breaking Change)**: All branches of `if`/`else` and `when`/`case` expressions must have the exact same integer type.
- **Generic Type Variables Require Exact Match**: When a type variable `T` is bound to a concrete integer type, all other uses of `T` must match exactly — no implicit widening through generics.
- **Tagged Union Scoped Construction Returns Union Type (Breaking Change)**: `Shape.Circle(radius = 2.0)` now returns `Shape` (the parent union type) instead of the bare variant struct `Shape__Circle`. All construction paths — unscoped (`Circle(2.0)`), scoped (`Shape.Circle(...)`), and bare-symbol (`Op.A`) — consistently return the union type.
- **Standard Library Uses `UInt` Internally**: Tess-level code now uses `UInt` for sizes and counts, with `CSize` reserved for C FFI boundaries. Standard library bindings updated accordingly.
- **`stdint.h` Always Included**: The transpiler now always includes `<stdint.h>` since fixed-width types (`int8_t`, `uint64_t`, etc.) are built-in to the type system.
- **Test `main()` Returns `CInt`**: Test files updated to use `CInt` return type for `main()` (matching C convention) with explicit narrowing casts where needed.

### Removed

- **`Unsafe.unsigned_to_signed` and `Unsafe.signed_to_unsigned`**: Removed in favor of the let-in annotation syntax for cross-family integer conversions (e.g., `signed: Int := unsigned_val`).
- **Tagged Union Make Functions**: Removed per-variant `make_Shape_Circle` wrapper functions. Scoped construction (`Shape.Circle(...)`) now handles this directly.
- **Existing-Type Variants**: Removed support for using existing types as tagged union variants (`| Foo.Special` syntax). Variants are now always defined inline within the union declaration.

### Fixed

- **Unused Type Parameter False Positive**: `annotation_uses_type_param()` now recurses into arrow and tuple type nodes, fixing false "unused type parameter" errors on generic structs with function-pointer fields (e.g., `Processor[T] : { process: (T) -> T }`).
- **Negative Literal Overflow Detection**: Compile-time range checking now correctly handles negative literals (e.g., `-129` for `CInt8`), which were previously missed because the check only ran on positive values.


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
