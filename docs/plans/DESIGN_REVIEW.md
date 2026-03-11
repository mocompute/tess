  Tess Language Design & Implementation Review

  Critical Findings

  Type System

  3. Cross-subchain directed unification falls through to symmetric (type.c:2924-2940) — Standalone types (CSize, CPtrDiff, CChar) silently convert in TL_UNIFY_DIRECTED mode when they should require explicit annotation.

  Parser & AST

  5. ast_return is overloaded for break — No dedicated ast_break tag; every consumer must check an is_break_statement flag. Forgetting this check silently mishandles break.
  6. ast_nil is both null literal and internal sentinel — The else arm of case/when uses ast_nil as a sentinel in the conditions array, structurally indistinguishable from the user-visible null literal.

  Codegen

  7. _tl_fatal_ format string injection (transpile.c:3362) — User strings are spliced directly into fprintf's format position. Any % in the message is UB. Should use "%s" as format.
  8. Non-standard main(int argc) signature (transpile.c:956) — One-argument main is a GCC/Clang extension, rejected by MSVC in strict mode. Breaks the Windows build path.
  9. Closure context struct hash only uses variable names, not types (transpile.c:499-509) — Theoretically allows two closures with same-named but differently-typed captures to share a wrong struct layout. Low practical risk due to alpha conversion, but structurally unsound.

  Language Semantics & Stdlib

  10. Str value assignment aliases the heap buffer (Str.tl:20-30) — For strings >14 bytes, := copies only the struct; both copies share the heap buffer. Freeing either produces use-after-free on the other. No language-level protection, no documentation warning.
  11. Array.find_value aborts on no match (Array.tl:474-481) — Return type T implies guaranteed success, but it calls _tl_fatal_. Inconsistent with the rest of the search API which returns Option.
  12. STANDARD_LIBRARY.md says Array.size is Int, implementation uses UInt — All documented signatures show Int for indices/counts while the actual code uses UInt.

  Architecture

  13. Error reporting lacks source locations for many inference errors (infer.c:591-613) — Errors created with null node (e.g., tl_err_unknown_symbol_in_main) print no file/line. Users can't find the problem.
  15. traverse_ctx_create uses malloc with no structural enforcement (infer_constraint.c:419) — Has already caused a real bug (MEMORY.md documents skip_alloc_expr not initialized). Adding any new field to traverse_ctx without updating the init function produces debug-only failures.

  ---
  Important Design Concerns

  ┌──────────────┬──────────────────────────────────────────────────────────────────────────────────────────┬─────────────────────────────────────┐
  │     Area     │                                          Issue                                           │               Impact                │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Type System  │ specialize_arrow cache key computed before weak int defaulting (infer_specialize.c:1300) │ Duplicate specializations           │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Type System  │ TV resolved to non-narrow Int accepts narrow CInt in symmetric context                   │ Ordering-dependent inference        │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Codegen      │ unsigned long long hardcoded where size_t needed (transpile.c:706)                       │ 32-bit ABI mismatch                 │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Stdlib       │ HashMap.get returns interior pointer invalidated by _grow                                │ Undocumented dangling pointer       │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Stdlib       │ Str.push is O(N^2) when used in a loop                                                   │ Silent performance trap             │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Stdlib       │ from_cstr/1 uses context.default, comment says string_literals                           │ Wrong allocator or stale docs       │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Architecture │ Structural trait conformance has no opt-out mechanism                                    │ Accidental conformance at scale     │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Architecture │ Phase 4/5 pipeline description inconsistent across docs                                  │ Contributor confusion               │
  ├──────────────┼──────────────────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────┤
  │ Build        │ CMake uses configure-time git hash vs Makefile's build-time hash                         │ Stale version in incremental builds │
  └──────────────┴──────────────────────────────────────────────────────────────────────────────────────────┴─────────────────────────────────────┘

  ---

  DONE

  1. CChar has contradictory signedness metadata (type.c:181) — Marked unsigned_int=1 but given range [-128, 127]. The design doc correctly says CChar should be standalone, but the implementation puts it in the unsigned family. u-suffixed literals can bind to CChar incorrectly.

  2. Const stripping is incomplete (infer_constraint.c:1265, 1115) — The unifier strips Const symmetrically from both sides (type.c:2965-2967), with safety enforced only by pre-unification guards at function call sites. Struct field assignment and return statements bypass these guards, allowing Ptr[Const[T]] →
  Ptr[T] silently.

  4. for loop uses hardcoded "gen_iter" variable name (parser_expr.c:404) — Every other synthesized name uses the unique-name counter. This one will silently collide with any user binding named gen_iter.

  14. hot_parse_ctx reentrancy guard is assert-only (infer.c:106) — Compiled away with -DNDEBUG in release builds. Silent corruption if re-entrancy occurs in production.
