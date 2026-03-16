# File IO Post Mortem — Language Issues

Issues encountered while implementing `File.tl` (the first non-trivial std library module with error handling).

## 1. ~~`Result[Void, E]` is not supported~~ (FIXED)

**Fixed:** The transpiler now maps `Void` fields to `char` (a 1-byte unit placeholder) wherever they appear: struct field declarations, function parameters, constructor calls, and function call arguments. `File.tl` write/seek/mkdir operations now return `Result[Void, IOError]` with `Ok(void)` / `Err(error)`.

## 2. ~~`try` requires matching `Ok` types~~ (FIXED)

**Fixed:** `try` now only constrains the error variant types to match. The transpiler constructs a new Result of the enclosing function's return type on the error path, copying the error value field-by-field. `File.tl` convenience wrappers (`write_file`, `write_file_bytes`, `append_file`) now use `try open(...)` instead of verbose `when` blocks.

## 3. ~~Divergent `when` arms don't unify with other arms~~ (FIXED)

**Fixed:** `infer_body` now detects diverging bodies (via `ast_node_is_diverging`) and leaves their type unbound instead of constraining it to the last expression's type. Unbound type variables unify freely with sibling arm types. Also fixes diverging `if-else` branches.

## 4. `if` without `else` is `Void`

```tl
if n >= 3 {
    if second.v == ':' { return true }
}
false  // conflict: Void vs Bool
```

The outer `if` evaluates to `Void` because it has no `else`, conflicting with the trailing `false`. Requires flattening with early returns. Arguably by design, but a frequent stumbling point.

## 5. let-else doesn't bind the non-matching variant

```tl
ok: Ok := some_result else { return Err(???) }
```

The `else` block can diverge but cannot access the `Err` value, so error propagation is impossible. Combined with issues 2 and 3, this makes cross-type error propagation quite verbose.

## Summary

Issues 1–3 had the most impact on API design and code quality. ~~Issue 1 forced a different return type convention for write operations.~~ (Fixed.) ~~Issue 2 made `try` unusable for convenience wrappers.~~ (Fixed.) ~~Issue 3 made inline error checking with `when` awkward.~~ (Fixed.) Issues 4–5 account for the remaining verbosity in the File module.
