# Tess Language Model

This document explains the conceptual foundations of Tess — the ideas behind the syntax.
It is meant for readers who want a precise understanding of how expressions, bindings,
and scoping work, and why the language behaves the way it does.

You don't need this document to write Tess. The [Language
Reference](LANGUAGE_REFERENCE.md) covers the practical syntax. But if you're wondering
*why* `:=` works the way it does, why there are two assignment operators, or what
"variant binding" really means, this is the right place.

## Expressions and Values

Tess is an **expression-based** language. This means that nearly every construct
produces a value. An `if` block has a value. A function body has a value (its last
expression). Even a sequence of statements in a block has a value (the last one).

```tl
// 'if' produces a value — no ternary operator needed
max := if a > b { a } else { b }

// A function returns its last expression
square(x) { x * x }
```

There is one deliberate exception: assignment with `=` is a **statement**, not an
expression. It has no value. This is a design choice — it prevents accidental use of `=`
where `==` was intended, and it makes mutation visually distinct from expressions that
produce values. (More on this in [Mutation](#mutation-with-).)

## Bindings: The Binding Expression

The central concept in Tess is the **binding expression**. Understanding it makes
everything else fall into place.

### What is a binding expression?

A binding expression does two things:

1. It **binds** a name to a value.
2. It defines a **body** where that name is available.

The simplest way to think about it: "let *x* be *value* in *the rest of the code*."

In Tess, the `:=` operator creates a binding expression:

```tl
x := 5
x + 1
```

This means: "let `x` be `5` in the expression `x + 1`." The entire thing — the binding
*and* the body — is one expression, and its value is `6`.

### The implicit "in"

In ML-family languages, these are called *let-in expressions*, and use an explicit keyword to separate the
binding from the body. For example, in OCaml you write `let x = 5 in x + 1`. In Tess, the "in" is implicit:
everything after the `:=` line, up to the end of the enclosing block, is the body.

```tl
// Tess
x := 5
x + 1

// What this means conceptually:
// let x = 5 in (x + 1)
```

This implicit "in" makes Tess code look like conventional imperative code, but the
semantics are precise: each `:=` opens a new scope that extends to the end of the block.

### Sequential bindings are nested binding expressions

When you write multiple bindings in sequence, each one nests inside the previous:

```tl
a := 1
b := 2
a + b
```

This is equivalent to:

```
let a = 1 in
  let b = 2 in
    a + b
```

Each binding can see all the names introduced before it, and the final expression can
see all of them. The entire construct produces the value `3`.

### Trailing binding shorthand

When a binding appears at the very end of a block with no subsequent expression (no body),
it evaluates to the bound value. This:

```tl
foo() {
  result: Byte := value
}
```

is syntax sugar for:

```tl
foo() {
  result: Byte := value
  result
}
```

The binding is created, and because it is the last thing in the block, the block's value is
the bound variable itself. This is a natural consequence of the binding expression model:
a binding with an empty body evaluates to the bound value.

### Blocks delimit scope

Curly braces `{ }` define where a binding's body ends. A name introduced with `:=` inside a
block is not visible outside it:

```tl
if true {
  x := 42     // x exists from here to the closing }
  x + 1       // x is visible
}
// x is not visible here
```

Blocks can also be used purely for scoping, without producing a value.

```tl
{
    // zero or more statements or expressions.
    // any bindings created in this scope will not be visible after the block
}
```

### Parenthesized expressions

When you need a more complex expression to produce a value, you can use parentheses to delimit the scope.
This is especially useful when you want to use a binding expression in the middle of a larger expression:

```tl
result := (
  a := 10
  b := 20
  a + b       // this is the value of the parenthesized expression
)
// result is 30; a and b are not visible here
```

```tl
foo(first, (tmp := 10; tmp * 2), third)
// produces value `20` as second argument to foo()
```

Note that brace-delimited blocks cannot be used in positions where a value is required, such as the above
examples. You must use parenthesized expressions in those positions.

```tl
foo(first, { tmp := 10; tmp * 2 }, third) // ERROR: must use parenthesized expression
```

## Shadowing

Because each `:=` creates a *new* binding in a *new* scope, you can reuse a name:

```tl
x := 1
x := x + 1    // new binding; the old x is shadowed
x := x * 2    // another new binding; x is now 4
```

This is **not** mutation. Each `:=` introduces a fresh name that happens to have the
same spelling. The previous binding still exists — it's just no longer visible. This is
called **shadowing**.

Shadowing is a natural consequence of binding expression nesting. The code above is equivalent to:

```
let x = 1 in
  let x = x + 1 in
    let x = x * 2 in
      x         →  4
```

Each inner `x` refers to the `x` from the immediately enclosing scope on the right-hand
side, and introduces a new `x` that shadows it.

## Mutation with `=`

Tess also supports true mutation via `=`, which changes the value of an existing binding:

```tl
x := 0       // create a binding
x = 10       // mutate it — same binding, new value
```

The key differences from `:=`:

|                     | `:=` (binding)                 | `=` (mutation)                     |
|---------------------|--------------------------------|------------------------------------|
| Creates a new name? | Yes                            | No (name must already exist)       |
| Is an expression?   | Yes (has a value)              | No (statement, no value)           |
| Crosses scopes?     | No (scoped to enclosing block) | Yes (mutates the original binding) |

Mutation reaches into the scope where the name was originally bound:

```tl
val := 1
if true {
  val = 2      // mutates the outer binding
}
// val is 2
```

Compare with shadowing:

```tl
val := 1
if true {
  val := 2     // new binding, shadows the outer one
}
// val is 1 (the outer binding is unchanged)
```

## Type Annotations on Bindings

A binding can include a type annotation using `:` between the name and `:=`:

```tl
x: Int := 42
name: String := "hello"
```

This annotates the binding with a type — the compiler checks that the value matches. It
is part of the binding expression, not a separate construct.

### Type annotations as casts

Because type annotations are part of the binding syntax, they also serve as the cast
syntax for type conversions. There is no separate `as` keyword:

```tl
narrow: CInt := some_int_value       // integer narrowing cast
ptr: Ptr[Byte] := some_other_ptr     // pointer cast
```

Every conversion point in Tess is a `:=` binding with a type annotation, making casts
visually prominent and easy to find.

## Variant Binding (for Tagged Unions)

Given the tagged unions:

```tl
Option[T]: | Some { value: T }
           | None

Result[T, U]: | Ok  { value: T }
              | Err { error: U }

```

a variant binding tries to match a value against one of its variants. If the match succeeds, the unwrapped
value is bound for the rest of the scope. If it fails, the `else` block executes. The else block may either
**diverge** (`return`, `break`, `continue`) or **produce a fallback value**:

```tl
// Diverging: exit the function if no match
s: Some := map.get("key") else { return 0 }
// s is available here — this is the body of the binding expression
s.value + 1

// Non-diverging: use a fallback value if no match
s: Some := map.get("key") else { 0 }
// if map.get result was None, the whole expression evaluated to 0
// if map.get result was Some, s is bound and execution continues here
```

When the else block diverges, `s` is guaranteed to be bound for the rest of the scope —
the body (everything after the binding) only runs when the match succeeded.

When the else block produces a value, the overall variant binding expression evaluates to
that value if the match fails, and the continuation after the variant binding is not
reached.

The variant binding replaces a common `when` (pattern match) idiom where you only care
about one variant:

```tl
// Without variant binding: the unwrapped value is trapped inside the arm
when val {
    s: Some { use(s.value) }
    _: None { return 0 }
}
// can't use s here — it was scoped to the when arm

// With variant binding: the unwrapped value is available in the surrounding scope
s: Some := val else { return 0 }
use(s.value)   // s is in scope for the rest of the block
```

Compare also with `try`, usable with any two-variant tagged union, when the first variant is the most useful
result, and the second can be returned as an error signal.

```tl
good := try result
// good is bound to Ok value for the rest of scope

// equivalent to:
when result {
    ok: Ok {
        // use ok.value here...
    }
    err: Err {
        return err
    }
}
```

## Closures and Capture

A **lambda** is an anonymous function expression:

```tl
f := (x) { x + 1 }
f(5)                   // 6
```

A lambda becomes a **closure** when it references bindings from an enclosing scope. In
this means the lambda's body can see names introduced by `:=` in outer
scopes:

```tl
offset := 10
f := (x) { x + offset }   // captures 'offset' from the enclosing scope
f(5)                       // 15
```

Tess has two kinds of closures — **stack closures** and **allocated closures** — which
differ in how they capture bindings and where the captured state lives. Both use the
same `Closure[F]` type and calling convention; the difference is in capture semantics
and lifetime.

### Stack closures: capture by reference

Stack closures capture bindings **by reference**. The closure does not get a copy of
`offset` at the moment it is created — it gets a live reference to the binding. This has
two consequences:

Mutations after the closure is created are visible inside the closure:

```tl
x := 1
f := () { x }
x = 0           // mutate x after creating the closure
f()             // returns 0, not 1
```

And the closure can mutate captured bindings:

```tl
counter := 0
increment := () { counter = counter + 1 }
increment()     // counter is now 1
increment()     // counter is now 2
```

Note that `counter = counter + 1` uses `=` (mutation), not `:=` (which would create a
new binding local to the lambda, shadowing the outer one).

**The escape restriction.** Because stack closures capture by reference, and bindings
live on the stack, a stack closure **cannot outlive the scope that owns its captured
bindings**. If a function returns a closure, the captured bindings would be destroyed
when the function returns, leaving dangling references. The compiler rejects this:

```tl
// ERROR: Cannot return lambda from function
make_adder(n) { (x) { x + n } }
```

This restriction follows directly from the scoping model. The binding `n` exists in the
scope of `make_adder`'s parameter. When `make_adder` returns, that scope ends, so any
closure capturing `n` must not escape.

### Allocated closures: capture by value

Allocated closures solve the escape problem by **copying** captured values onto the heap
instead of referencing them on the stack. The captures are declared explicitly, and the
closure can be returned, stored in structs, and outlive its original scope.

The syntax uses attributes to declare the allocation and the capture list:

```tl
make_adder(n: Int) {
  [[alloc, capture(n)]] (x) { x + n }    // n is copied into a heap-allocated context
}

add5 := make_adder(5)
add5(10)   // 15
```

This works because `n` is copied at closure creation time. When `make_adder` returns,
the original binding is gone, but the closure's copy on the heap is independent.

**Capture by value means mutations are not shared:**

```tl
n := 5
f := [[alloc, capture(n)]] (x) { x + n }
n = 10          // mutate n after creating the closure
f(1)            // returns 6, not 11 — n was copied at creation
```

Compare with a stack closure, where the same code would return `11`.

**Explicit capture lists.** Unlike stack closures, which implicitly capture any
referenced binding, allocated closures require an explicit `capture(...)` list. Every
free variable in the body must be listed, and every listed name must actually be used.
This makes the capture boundary visible and prevents accidental captures:

```tl
// ERROR: body uses 'y' but it's not in the capture list
f := [[alloc, capture(x)]] () { x + y }

// ERROR: 'z' is listed but not used in the body
g := [[alloc, capture(x, z)]] () { x + 1 }
```

**Memory management.** The `[[alloc]]` attribute accepts an optional allocator. Without
one, it uses the default allocator. With an arena, all closures sharing the arena are
freed together:

```tl
// Default allocator
f := [[alloc, capture(x)]] (y) { x + y }

// Explicit arena — freed when the arena is destroyed
f := [[alloc(my_arena), capture(x)]] (y) { x + y }
```

### Shared mutable state with allocated closures

Since allocated closures capture by value, multiple closures cannot share mutable state
through captured bindings the way stack closures can. To share mutable state, capture a
**pointer** — the pointer is copied by value, but both closures point to the same
location:

```tl
make_counter(start: Int, arena: Ptr[Allocator]) -> Counter {
  state: Ptr[Int] := Alloc.create(arena, Int)
  state.* = start

  inc := [[alloc(arena), capture(state)]] () {
    state.* += 1
    state.*
  }
  get := [[alloc(arena), capture(state)]] () { state.* }

  Counter(inc = inc, get = get)
}

c := make_counter(0, arena)
c.inc()   // 1
c.inc()   // 2
c.get()   // 2
```

### Stack vs. allocated closures: summary

|                   | Stack closure          | Allocated closure                       |
|-------------------|------------------------|-----------------------------------------|
| Syntax            | `(x) { body }`         | `[[alloc, capture(...)]] (x) { body }`  |
| Captures          | Implicit, by reference | Explicit, by value                      |
| Mutations shared? | Yes                    | No (capture a pointer for shared state) |
| Can escape scope? | No                     | Yes                                     |
| Memory            | Stack (automatic)      | Heap (freed via allocator or arena)     |
| Type              | `Closure[F]`           | `Closure[F]` (same)                     |

The two kinds are interchangeable at call sites. A function that takes a `Closure[(Int)
-> Int]` accepts either kind — the calling convention is the same.

## Pattern Matching and Scope

Tess has two pattern matching expressions: `when` for tagged union destructuring, and
`case` for value matching. Both are **expressions** — they produce a value.

### `when`: tagged union destructuring

The `when` expression matches a tagged union value against its variants. Each arm
**binds** the unwrapped variant and introduces a **new scope** — the binding is a binding
expression whose body is the arm's block:

```tl
area := when shape {
    c: Circle { c.radius * c.radius * 3.14 }
    s: Square { s.length * s.length }
}
// c and s are not visible here — each was scoped to its arm
```

This is the standard binding behavior: `c: Circle` binds `c` for the duration of the `{
... }` block, and then the binding ends. The `when` expression as a whole evaluates to
the value of the matched arm.

`when` expressions must be exhaustive — every variant must have an arm, or there must be
an `else` arm.

### `case`: value matching

The `case` expression matches a value by equality rather than by type. It does not
destructure — each arm tests whether the scrutinee equals the given value:

```tl
name := case n {
    0    { "zero" }
    1    { "one" }
    else { "other" }
}
```

Like `when`, `case` is an expression and its value is the matched arm's value. `case`
can also take a custom predicate function instead of using `==`.

`case` has a second form for tagged unions when the type cannot be inferred from the
scrutinee alone — see the [Language
Reference](LANGUAGE_REFERENCE.md#explicit-type-annotation-case-expression) for details.

### Arm scoping and variant binding

The scoping of `when` arms creates a practical problem. If you only care about one
variant and want its value for the rest of the function, `when` traps it inside the arm:

```tl
when val {
    s: Some { use(s.value) }
    _: None { return 0 }
}
// s is gone — it was scoped to the arm
```

Variant binding solves this by creating the binding in the **enclosing** scope instead:

```tl
s: Some := val else { return 0 }
// s is bound here — the "in" part is the rest of the enclosing block
use(s.value)
```

The connection is direct: `s: Some := val else { return 0 }` is a binding expression
where the binding either succeeds (and the body is the rest of the block) or the else
block executes (diverging so the body is never reached, or producing a fallback value for
the overall expression).

## Summary

The core ideas:

- **`:=` is a binding expression.** It binds a name and opens a scope that extends to the
  end of the enclosing block.
- **Sequential `:=` statements nest.** Each new binding creates a new scope inside the
  previous one.
- **Shadowing is rebinding, not mutation.** Reusing a name with `:=` creates a new
  binding; the old one is hidden but unchanged.
- **`=` is mutation.** It changes an existing binding in place, reaches across scopes,
  and is a statement with no value.
- **Type annotations live on bindings.** The `:` in `x: Int := v` annotates the binding
  expression, which is also why casts use the same syntax.
- **Stack closures capture by reference and cannot escape.** Because bindings live on
  the stack, a stack closure cannot outlive the scope that owns the captured bindings.
- **Allocated closures capture by value and can escape.** With `[[alloc,
  capture(...)]]`, captured values are copied to the heap, so the closure is independent
  of the original scope.
- **Pattern matching arms are scopes.** Each arm's binding is a binding expression whose
  body is the arm's block.
- **Variant binding extends binding expressions with pattern matching.** The binding
  succeeds or the else block diverges, guaranteeing the name is always valid in the body.
  It exists because `when` arms scope their bindings too tightly for the common "unwrap
  or bail out" pattern.

## Further Reading

- [Language Reference](LANGUAGE_REFERENCE.md) — Complete syntax reference with examples
- [Type System](TYPE_SYSTEM.md) — How inference, unification, and specialization work
