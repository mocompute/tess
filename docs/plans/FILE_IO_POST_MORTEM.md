# File IO Post Mortem — Language Issues

Issues encountered while implementing `File.tl` (the first non-trivial std library module with error handling).

## 1. ~~`Result[Void, E]` is not supported~~ (FIXED)

**Fixed:** The transpiler now maps `Void` fields to `char` (a 1-byte unit placeholder) wherever they appear: struct field declarations, function parameters, constructor calls, and function call arguments. `File.tl` write/seek/mkdir operations now return `Result[Void, IOError]` with `Ok(void)` / `Err(error)`.

## 2. `try` requires matching `Ok` types

`try open(path, Write)` fails inside a function returning `Result[String, IOError]` because `open` returns `Result[Ptr[File], IOError]`. The error types match but the `Ok` types don't. This makes `try` unusable in convenience wrappers that call functions with different success types, forcing verbose `when` blocks.

**Fix option:** Unify `try` only on the error variant; construct the early-return `Err` using the enclosing function's Result type.

## 3. Divergent `when` arms don't unify with other arms

```tl
when expr {
    e: Some { c_printf("error\n"); return 1 }  // diverges
    n: None { }                                  // Void
}
```

Type conflict between the diverging arm and the Void arm. Divergent branches should be compatible with any type.

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

Issues 1–3 had the most impact on API design and code quality. ~~Issue 1 forced a different return type convention for write operations.~~ (Fixed: `Result[Void, E]` now works.) Issue 2 made `try` unusable for convenience wrappers. Issue 3 made inline error checking with `when` awkward. Issues 2–3 account for most of the remaining verbosity in the File module's convenience functions.
