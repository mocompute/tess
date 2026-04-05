# TL Coding Conventions

Guidelines for writing Tess library and example code. The canonical reference for these conventions is `src/tl/std/CommandLine.tl`.

## Quick Reference (for LLMs / coding assistants)

When generating or editing `.tl` code, follow these rules:

- **No `mut` keyword.** Bindings are reassignable by default. Do not write `mut` anywhere.
- **`:=` declares, `=` assigns.** `x := 42` creates a new binding; `x = 42` mutates an existing one.
- **Use `Const` for immutable bindings.** `x: Const := 5` or `x: Const[Int] := 5`. Prevents reassignment, transpiles to C `const`.
- **String literals are `String` (SSO).** `"foo"` is a `String`. Use `c"foo"` for a C string (`Ptr[CChar]`). Use `f"hello {name}"` for format strings with embedded expressions. The `s"foo"` prefix is still accepted but redundant.
- **`main()` returns `CInt`.** The compiler enforces this. No type annotation needed.
- **Omit type annotations in implementations.** Synopsis has full types; implementations use parameter names only. Inference handles the rest.
- **Omit integer suffixes.** Write `0`, not `0zu`. Use suffixes only when inference is ambiguous.
- **Use `Option` for absence, `Result` for errors.** Not null, not sentinel values.
- **Use tagged unions for alternatives.** Not integer codes or boolean flags.
- **`when`/`else` for multiple variants; variant binding for a single expected variant.**
- **Keep code flat.** Early returns and variant binding instead of deep nesting.
- **`self` is the receiver.** `Ptr[T]` for mutating methods, `T` by value for read-only.
- **Allocator overloads.** Provide explicit `Ptr[Allocator]` version + convenience version using default.
- **Private helpers start with `_`.** Types are PascalCase, functions are snake_case.
- **Run `tess fmt` before committing.**
- **One module, one type.** Name the type the same as the module (or `T`). Callers use dot-call syntax.
- **Named fields in struct construction.** `ArgSpec(long_name = name, kind = FlagBool)`
- **Implicit address-of in dot-call syntax.** Write `arr.push(x)`, not `arr.&.push(x)` — the compiler automatically takes the address when dispatching a value to a `Ptr[T]` parameter.

See the sections below for detailed explanations and examples.

---

## Module Organization

Modules are generally organised around a single type, plus the functions and methods that act on it. Name the primary type the same as the module (preferred) or `T` (alternative). This enables **auto-collapse**: users can write `Array[Int]` instead of `Array.Array[Int]` in type positions:

```tl
#module Array

// Primary type shares the module name
Array[T]: {
    v:        Ptr[T],
    size:     CSize,
    capacity: CSize,
}
```

Callers should prefer **dot-call syntax** to access functions, which reads naturally as method calls:

```tl
arr := Array.empty[Int]()

// Direct call: verbose
Array.push(arr.&, 42)

// Dot-call syntax: preferred, implicit address-of
arr.push(42)
```

## File Structure

Organize files in this order:

1. **Header comment**: brief description of the module's purpose
2. **`#module` declaration**
3. **`#import` directives**
4. **Type and function aliases**: shorthand for commonly used types and functions
5. **Type definitions**: structs and tagged unions
6. **Synopsis**: all public function signatures with full type annotations
7. **Private helper signatures**: under a separate `// -- Private helpers --` heading
8. **Implementation**: function bodies, grouped into logical sections

```tl
// CommandLine: Declarative command-line argument parsing.
#module CommandLine
#import <Alloc.tl>
#import <Array.tl>

Allocator = Alloc.Allocator

// -- Types --

ArgKind: | FlagBool | FlagCount | FlagValue

// -- Synopsis --

create(name: Str, description: Str) -> Ptr[Parser]
// ...

// -- Private helpers --

_find_spec_long(self: Ptr[Parser], name: Str) -> Option[CSize]

// -- Implementation --

create(name, description) { ... }
```

Use `// -- Section Name --` to divide the file into logical groups.

**Re-opening modules:** When a submodule must be defined mid-file, the parent module can be re-opened afterward. Keep definitions in the original section when possible and only use re-opening when the submodule must appear between them (e.g., when later definitions depend on the submodule).

## Naming Conventions

- **Types**: PascalCase: `ArgSpec`, `ParseError`, `Parser`
- **Functions**: snake_case: `get_flag`, `parse_or_exit`, `help_text`
- **Private helpers**: underscore prefix: `_find_spec_long`, `_store_bool`, `_S`
- **Type aliases**: PascalCase, matching the original: `Allocator = Alloc.Allocator`
- **Function aliases**: snake_case, matching the original: `println = Print.println`

## The `self` Convention

Methods that operate on a type take `self` as their first parameter. Use `Ptr[T]` when the method needs to mutate the receiver, and `T` by value for read-only access:

```tl
// Mutating methods take Ptr
flag    (self: Ptr[Parser], long_name: Str, desc: Str) -> Void
destroy (self: Ptr[Parser])                            -> Void

// Read-only accessors take by value
get_flag(args: Args, name: Str) -> Bool
has     (args: Args, name: Str) -> Bool
```

This convention enables dot-call syntax: `parser.flag("verbose", "Be verbose")` desugars to `CommandLine.flag(parser, ...)`.

## Pointers

Use `Ptr[T]` for mutable, long-lived, or heap-allocated data. Use value types for small, read-only data.

- `.&` takes the address of a value: `arr.&` yields `Ptr[Array[T]]`
- `.*` dereferences a pointer: `p.*` yields the pointed-to value
- `->` accesses a field through a pointer: `self->_name` is shorthand for `self.*._name`

## Synopsis and Type Annotations

The **synopsis** declares every public function with full type annotations. This serves as the module's API
documentation. It is not required by the compiler, but it is helpful. **Implementations omit type
annotations** — use parameter names only:

```tl
// Synopsis
create(alloc: Ptr[Allocator], name: Str, description: Str) -> Ptr[Parser]

// Implementation
create(alloc, name, description) { ... }
```

The same applies to local bindings — omit annotations unless inference cannot determine the type:

```tl
p := alloc->malloc(alloc, sizeof[Parser]())    // inference handles it
i: CSize := 0                                  // annotation needed
```

## Function Overloading

Place the full-parameter version first, then convenience overloads that delegate to it:

```tl
// Synopsis
create(alloc: Ptr[Allocator], name: Str, description: Str) -> Ptr[Parser]
create(name: Str, description: Str)                        -> Ptr[Parser]

// Implementation
create(alloc, name, description) {
    // full implementation
}

create(name, description) {
    create(Alloc.context.default, name, description)
}
```

Align overloaded signatures in the synopsis for readability:

```tl
flag    (self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
flag    (self: Ptr[Parser], long_name: Str, desc: Str)                    -> Void
counter (self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
option  (self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
option  (self: Ptr[Parser], long_name: Str, desc: Str)                    -> Void
```

Functions that allocate memory are a common case: provide one overload taking an explicit `Ptr[Allocator]`, and a convenience version that uses `Alloc.context.default`.

## Variadic Functions

Use variadic functions (`...Trait`) when you need to accept heterogeneous arguments that share a common conversion. Use `Array[T]` or `Slice[T]` when all arguments have the same type.

```tl
// Good: heterogeneous args, each converted via ToString
println(args: ...ToString) -> Void { ... }

// Good: homogeneous collection — no variadics needed
sum(numbers: Array.Array[Int]) -> Int { ... }
```

The variadic parameter must be last. Place fixed parameters before it:

```tl
// Fixed params first, variadic last
log(level: Int, args: ...ToString) -> Void { ... }
```

When defining a custom trait for variadic bounds, keep it to a single unary function with a concrete return type:

```tl
// Good variadic trait
Serialize[T] : { serialize(a: T) -> Bytes }

// Bad: binary function — can't be used as variadic bound
Compare[T] : { compare(a: T, b: T) -> CInt }
```

## Tagged Unions

Prefer tagged unions over integer codes, boolean flags, or sentinel values. Define variants inline, aligned with `|`:

```tl
ArgKind: | FlagBool | FlagCount | FlagValue

ArgValue: | BoolVal  { v: Bool }
          | CountVal { v: Int }
          | StrVal   { v: Str }
          | Absent
```

Use **`Option`** when a value may be absent; use **`Result`** when failure carries additional information:

```tl
_find_spec_long(self: Ptr[Parser], name: Str) -> Option[CSize]

parse(self: Ptr[Parser], argc: CInt, argv: Ptr[CString]) -> Result[Args, ParseErrors]
```

### Pattern Matching

Use **`when`** to match multiple variants. Use `else` as a fallback:

```tl
when spec.kind {
    fb: FlagBool  { _store_bool(values, name) }
    fc: FlagCount { _store_count(values, name) }
    fv: FlagValue { ... }
}
```

Use a **variant binding** when a single variant is expected and all others should diverge or produce a fallback:

```tl
s: Some := HashMap.get_copy(values, name) else { return 0 }
```

For two-variant unions, the else arm can bind the other variant to access its fields:

```tl
ok: Ok := do_something() else err { return err.error }
```

When the success value is not needed (e.g., `Result[Void, E]` or when intentionally discarding it), use **void-else** — the statement form without a left-hand binding:

```tl
validate(input) else err { return err.error }
```

## Control Flow

### Keeping Code Flat

Prefer early `return` and variant binding over deep nesting:

```tl
// Avoid: nesting pushes the main logic rightward
when _find_spec_long(self, name) {
    si: Some {
        spec := self->_specs.v.[si.v]
        when spec.kind { ... }
    }
    n: None {
        Array.push(errors, UnknownFlag(flag = arg))
    }
}

// Prefer: variant binding keeps the main logic flat
si: Some := _find_spec_long(self, name) else {
    Array.push(errors, UnknownFlag(flag = arg))
    return
}
spec := self->_specs.v.[si.v]
when spec.kind { ... }
```

This also applies inside `when` and `while` bodies: return, continue, or break as soon as the relevant condition is known.

### Loops

Use `while` with the comma-separated update form:

```tl
i: CSize := 0
while i < self->_specs.size, i += 1 {
    spec := self->_specs.v.[i]
    // ...
}
```

### Void Functions

Functions return the value of their last expression. For side-effect-only functions, the last expression typically returns `Void` automatically. If the last expression would produce an unwanted value, end with `void`:

```tl
log(msg) {
    c_printf(c"%s\n", msg)
    void
}
```

## Literals

**Strings**: `"foo"` is a `String` (SSO). Use `c"foo"` for a C string (`Ptr[CChar]`).
The `s"foo"` prefix is still accepted but redundant. Use `f"hello {name}"` for format strings (f-strings) with embedded expressions.


**Integers**: Do not use suffixes (`zu`, `z`, `u`) when the type is clear from context. Let inference resolve the type. Use suffixes only when inference genuinely cannot determine the type.

## Error Handling

Use `_tl_fatal_()` for unrecoverable errors (out of memory, programmer mistakes):

```tl
if p == null { _tl_fatal_("CommandLine: oom") }
```

For recoverable errors, return a `Result` (see [Tagged Unions](#tagged-unions)). Define error variants as a tagged union so callers can distinguish failure modes.

## Comments and Formatting

- Keep comments minimal: code should be self-explanatory
- Use the header comment to describe the module's purpose and usage pattern
- Inline comments are fine for non-obvious logic
- Run `tess fmt` before committing
