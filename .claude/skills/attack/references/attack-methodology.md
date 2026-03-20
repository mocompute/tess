# Attack Methodology

This is the shared adversarial testing procedure used by both `/attack` and `/random-attack`.

## Analyzing the Commit

Examine the commit's changes:

```bash
git show <hash> --stat
git diff <hash>~1 <hash>
```

Read the changed code carefully. Understand what the commit does — what behavior it adds, fixes, or modifies.

Use file paths to identify the compiler phase:

| Path pattern | Compiler phase |
|---|---|
| `parser*.c`, `parser_internal.h` | Parsing — syntax, AST construction |
| `infer*.c` | Type inference — HM unification, constraints, specialization |
| `type.c`, `type.h` | Type system — type registry, representations |
| `transpile.c` | Code generation — C emission |
| `src/tl/std/*.tl` | Standard library — runtime, C FFI bindings |
| `tokenizer.c`, `token.c` | Lexing — tokenization |
| `format.c` | Formatter |
| `ast.c`, `ast.h` | AST representation |

If the commit touches `.tl` standard library code, also read the relevant file to understand the API being changed.

## Devising Three Adversarial Attacks

Come up with three different test ideas that target plausible weaknesses in the changed code. The goal is to find real bugs, not just write more tests — think like an attacker trying to break the code.

Good adversarial angles:

- **Boundary conditions**: empty inputs, zero-length collections, single-element cases, maximum nesting depth
- **Type system stress**: unusual generic instantiations, recursive types, constraint edge cases, mixing type families (signed/unsigned, different widths)
- **Feature interactions**: how the changed feature combines with closures, lambdas, tagged unions, modules, traits, UFCS — bugs love the seams between features
- **Name resolution tricks**: shadowing, module name conflicts, importing the same name from different modules
- **Malformed but parseable input**: code that parses correctly but should be rejected at type-checking
- **Surprising but valid code**: unusual but technically correct usage the implementation might not handle
- **Mutation and aliasing**: pointer aliasing, const correctness edge cases
- **Integer edge cases**: overflow, narrowing conversions, mixing signed/unsigned in expressions

For each attack, write:
1. A one-line description of what it tests
2. Why it might break (what assumption in the code it challenges)
3. Whether it's **should_pass** (valid code that should compile and run) or **should_fail** (invalid code the compiler should reject)

Then **randomly pick one** to implement. Don't pick the easiest or most obvious — a random choice ensures coverage diversity over repeated invocations.

## Writing the Test

Before writing, review the .tl coding conventions in CLAUDE.md. If the test uses language features you're unsure about, read `docs/LANGUAGE_REFERENCE.md`.

### should_pass tests

File name: `test_adversarial_<short_hash>_<descriptive_name>.tl`

```tl
#module main

main() {
    error := 0

    // <what this edge case tests>
    // ... test code ...
    error += if <expected_condition> { 0 } else { 1 }

    error
}
```

Key rules:
- `#module main` at the top
- `main()` returns `CInt` (inferred, no annotation needed)
- Use the `error` accumulator pattern: return 0 on success
- `:=` to declare, `=` to reassign. No `mut` keyword.
- String literals are `Ptr[CChar]`; use `s"..."` for `String`
- Omit type annotations unless needed for disambiguation
- Keep it focused — one edge case per test
- **Imports**: Standard library modules need explicit `#import` to use their qualified names. Common ones: `#import <String.tl>` (for `String.*` calls), `#import <HashMap.tl>`, `#import <Array.tl>`, `#import <Hash.tl>` (for `hash()`), `#import <Alloc.tl>` (for explicit allocators). Check existing tests in `src/tess/tl/test/pass/` if unsure.
- **UFCS limitations**: UFCS (`.method()` syntax) does not work on numeric literals or all types. Use free-function syntax for trait calls on primitives: `hash(42)` not `42.hash()`. When unsure, use the free-function form.

### should_fail tests

File name: `test_fail_adversarial_<short_hash>_<descriptive_name>.tl`

```tl
#module main

main() {
    // <what invalid code this tests>
    // ... code that should fail to compile ...
    0
}
```

Key rules:
- Must be syntactically valid (the parser should accept it)
- Must contain a semantic error the compiler should catch (type error, constraint violation, undeclared variable, etc.)

## Checking for Duplicates

Before writing the test, check whether a test for this commit and attack angle already exists:

```bash
ls src/tess/tl/test/*/test_*adversarial_<short_hash>* 2>/dev/null
```

If a test with the same commit hash and a similar descriptive name already exists, pick a different attack from the three you devised. If all three angles are already covered, report that this commit has been fully attacked and skip it.

## Running the Test

Write the test file to a temp location first, then run it:

```bash
./tess run /tmp/test_adversarial_<name>.tl
```

Check the exit code and any error output.

## Placing the Test

Based on intent vs actual result:

| Intent | Actual Result | Destination | Meaning |
|--------|--------------|-------------|---------|
| should_pass | Compiles + runs (exit 0) | `src/tess/tl/test/pass/` | Compiler handles edge case correctly |
| should_pass | Compiler error or runtime failure | `src/tess/tl/test/known_failures/` | **Bug found!** |
| should_fail | Compiler rejects (exit != 0) | `src/tess/tl/test/fail/` | Compiler correctly rejects invalid code |
| should_fail | Compiles successfully | `src/tess/tl/test/known_fail_failures/` | **Bug found!** Compiler should reject but doesn't |

Copy the test from `/tmp/` to the appropriate directory.

## Reporting

Summarize:
1. Which commit was analyzed and what it changed (one line)
2. The three adversarial ideas (brief bullets)
3. Which one was picked and why it's interesting
4. The test result — passed or found a bug?
5. Where the test file was placed (full path)

If a bug was found, highlight it — that's the whole point.
