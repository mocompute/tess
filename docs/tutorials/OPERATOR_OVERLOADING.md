# Tutorial: Operator Overloading and Traits

This tutorial shows how to define custom operators for your types in Tess, and how
the trait system lets you write generic code that works across types sharing the same
operations.

**Prerequisites:** Familiarity with static typing and generics in another language
(Rust, Go, C++, etc.). No prior Tess knowledge required — the tutorial is self-contained.

**Running the examples:** Every code block is a complete program. Save it to a `.tl`
file and run it with:

```
$ tess run example.tl
```

> For the full operator and trait reference, see the
> [Language Reference: Operator Overloading](../LANGUAGE_REFERENCE.md#operator-overloading)
> and [Language Reference: Traits](../LANGUAGE_REFERENCE.md#traits) sections.

---

## 1. Your First Operator Overload

Tess lets you overload arithmetic, comparison, and bitwise operators for any struct or
tagged union. You do this by defining a function with a specific name in the type's module
— no special syntax, annotations, or `impl` blocks needed.

Let's start with a 2D vector type and teach it to use `+`:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

#module main

main() {
    a := Geo.Vec2(x = 3, y = 4)
    b := Geo.Vec2(x = 1, y = 2)

    c := a + b
    c_printf(c"a + b = (%d, %d)\n", c.x, c.y)
    0
}
```

```
$ tess run add.tl
a + b = (4, 6)
```

That's it. When the compiler sees `a + b` and `a` is a user-defined type, it looks for
a function named `add` in that type's module. Both operands must be the same type, and the
return type must also match.

> **Convention: one module, one type.** Overloads are resolved by function name and arity
> within a module, so each type that defines operators should live in its own module. If you
> need two types with `+`, put them in separate modules. See the
> [Language Reference](../LANGUAGE_REFERENCE.md#one-module-one-type) for details.

Here is the full mapping from operator to function name:

| Operator | Function | Operator | Function |
|----------|----------|----------|----------|
| `+` | `add(a: T, b: T) -> T` | `==` | `eq(a: T, b: T) -> Bool` |
| `-` | `sub(a: T, b: T) -> T` | `!=` | derived from `eq` |
| `*` | `mul(a: T, b: T) -> T` | `<` `<=` `>` `>=` | derived from `cmp(a: T, b: T) -> CInt` |
| `/` | `div(a: T, b: T) -> T` | `-` (unary) | `neg(a: T) -> T` |
| `%` | `mod(a: T, b: T) -> T` | `!` (unary) | `not(a: T) -> Bool` |
| `&` | `bit_and` | `~` (unary) | `bit_not(a: T) -> T` |
| `\|` | `bit_or` | `<<` | `shl(a: T, b: T) -> T` |
| `^` | `bit_xor` | `>>` | `shr(a: T, b: T) -> T` |

## 2. More Operators: Subtraction, Negation, Equality

You can define as many or as few operators as make sense for your type. Let's flesh out
`Vec2` with subtraction, unary negation, and equality:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

sub(a: Vec2, b: Vec2) {
    Vec2(x = a.x - b.x, y = a.y - b.y)
}

neg(v: Vec2) {
    Vec2(x = 0 - v.x, y = 0 - v.y)
}

eq(a: Vec2, b: Vec2) {
    a.x == b.x && a.y == b.y
}

#module main

main() {
    a := Geo.Vec2(x = 3, y = 4)
    b := Geo.Vec2(x = 1, y = 2)

    c_printf(c"a + b  = (%d, %d)\n", (a + b).x, (a + b).y)
    c_printf(c"a - b  = (%d, %d)\n", (a - b).x, (a - b).y)
    c_printf(c"-a     = (%d, %d)\n", (-a).x, (-a).y)
    c_printf(c"a == a: %d\n", a == a)
    c_printf(c"a != b: %d\n", a != b)
    0
}
```

```
$ tess run ops.tl
a + b  = (4, 6)
a - b  = (2, 2)
-a     = (-3, -4)
a == a: 1
a != b: 1
```

Notice that `!=` works automatically — the compiler derives it as `!eq(a, b)`. You never
define `!=` yourself.

## 3. Ordering and the `cmp` Function

For the comparison operators (`<`, `<=`, `>`, `>=`), define a single `cmp` function
returning `CInt` — negative if less, zero if equal, positive if greater:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

cmp(a: Vec2, b: Vec2) {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else { 0 }
}

#module main

main() {
    a := Geo.Vec2(x = 1, y = 2)
    b := Geo.Vec2(x = 1, y = 2)
    c := Geo.Vec2(x = 3, y = 4)


    c_printf(c"a == b: %d\n", a == b)
    c_printf(c"a != c: %d\n", a != c)
    c_printf(c"a < c:  %d\n", a < c)
    c_printf(c"c > a:  %d\n", c > a)
    c_printf(c"a >= b: %d\n", a >= b)
    0
}
```

```
$ tess run cmp.tl
a == b: 1
a != c: 1
a < c:  1
c > a:  1
a >= b: 1
```

All six comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) work from just `cmp`.
The compiler derives `eq` from `cmp` when no separate `eq` function exists. If you want
`==` to use different logic than `cmp(a, b) == 0`, define both `eq` and `cmp`.

## 4. Compound Assignment

Compound assignment operators (`+=`, `-=`, `*=`, etc.) are derived automatically from the
corresponding binary operator. `a += b` desugars to `a = add(a, b)`:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

sub(a: Vec2, b: Vec2) {
    Vec2(x = a.x - b.x, y = a.y - b.y)
}

#module main

main() {
    pos := Geo.Vec2(x = 0, y = 0)
    vel := Geo.Vec2(x = 3, y = 4)

    pos += vel
    c_printf(c"after += : (%d, %d)\n", pos.x, pos.y)

    pos += vel
    c_printf(c"after += : (%d, %d)\n", pos.x, pos.y)

    pos -= Geo.Vec2(x = 1, y = 1)
    c_printf(c"after -= : (%d, %d)\n", pos.x, pos.y)
    0
}
```

```
$ tess run compound.tl
after += : (3, 4)
after += : (6, 8)
after -= : (5, 7)
```

---

## 5. Introduction to Traits

So far we've been defining operators for a specific type. But what if you want to write
a function that works with *any* type that supports `+`, or *any* type that supports `==`?

This is what **traits** are for. A trait describes a set of functions a type must provide.
Tess comes with built-in traits for every overloadable operator — `Add`, `Sub`, `Eq`,
`Ord`, and so on. When you define `add` for `Vec2`, your type automatically satisfies the
`Add` trait. No declaration needed.

### Writing a Generic Function with a Trait Bound

A **trait bound** constrains a generic type parameter. The syntax is `T: TraitName`:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

eq(a: Vec2, b: Vec2) {
    a.x == b.x && a.y == b.y
}

cmp(a: Vec2, b: Vec2) {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else { 0 }
}

#module main

// Works with any type that supports +
double[T: Add](x: T) { x + x }

// Works with any type that supports ==
are_equal[T: Eq](a: T, b: T) { a == b }

// Works with any type that supports <, <=, >, >=
smallest[T: Ord](a: T, b: T) {
    if a <= b { a } else { b }
}

main() {
    a := Geo.Vec2(x = 3, y = 4)
    b := Geo.Vec2(x = 1, y = 2)

    d := double(a)
    c_printf(c"double(a) = (%d, %d)\n", d.x, d.y)

    c_printf(c"are_equal(a, a): %d\n", are_equal(a, a))
    c_printf(c"are_equal(a, b): %d\n", are_equal(a, b))

    s := smallest(a, b)
    c_printf(c"smallest(a, b) = (%d, %d)\n", s.x, s.y)
    0
}
```

```
$ tess run bounds.tl
double(a) = (6, 8)
are_equal(a, a): 1
are_equal(a, b): 0
smallest(a, b) = (1, 2)
```

The key insight: `Vec2` satisfies `Add` because the `Geo` module has an `add` function
with the right signature. It satisfies `Ord` because it has `cmp` (and `Eq` is derived
from `cmp` automatically). The compiler checks this at the call site — if you try to call
`double` with a type that has no `add`, you get a compile-time error.

These generic functions work with built-in types too. `Int` satisfies `Add` (it has `+`),
`Float` satisfies `Eq` (it has `==`), and so on — the compiler recognizes that built-in
types conform to the traits matching their intrinsic operators. So `double(7)` and
`smallest(3, 8)` work just as you'd expect.

## 6. Defining Your Own Traits

The built-in traits like `Add` and `Eq` are provided by the compiler, but they're not
magic. A trait declaration looks like a struct, but with function signatures instead of
fields. Here's what `Add` would look like if you defined it yourself:

```tl
Add[T] : {
    add(a: T, b: T) -> T
}
```

The type parameter `T` stands for the implementing type. Any type whose module has an
`add` function with this shape structurally conforms to `Add` — which is exactly what
happened with `Vec2` in the earlier sections.

You can't redefine `Add` (it's reserved by the compiler), but you can define your own
traits using the same syntax. You can also **combine** existing traits through
inheritance. Let's define `Arith` as a trait that requires `Add`, `Sub`, and `Mul`:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

sub(a: Vec2, b: Vec2) {
    Vec2(x = a.x - b.x, y = a.y - b.y)
}

mul(a: Vec2, b: Vec2) {
    Vec2(x = a.x * b.x, y = a.y * b.y)
}

eq(a: Vec2, b: Vec2) {
    a.x == b.x && a.y == b.y
}

#module main

// A combined trait: requires Add, Sub, and Mul
Arith[T] : Add[T], Sub[T], Mul[T] { }

// Use the combined bound
evaluate[T: Arith](a: T, b: T, c: T) {
    a + b * c - a
}

main() {
    a := Geo.Vec2(x = 1, y = 2)
    b := Geo.Vec2(x = 3, y = 4)
    c := Geo.Vec2(x = 5, y = 6)

    r := evaluate(a, b, c)
    c_printf(c"a + b * c - a = (%d, %d)\n", r.x, r.y)
    0
}
```

```
$ tess run arith.tl
a + b * c - a = (15, 24)
```

The empty braces `{ }` mean `Arith` adds no additional function requirements beyond
what it inherits. A type satisfies `Arith` if it has `add`, `sub`, and `mul`.

Traits can also introduce new function requirements of their own:

```tl
Printable[T] : {
    to_string(a: T) -> CString
    print(a: T) -> CInt
}
```

A type satisfies `Printable` by having both `to_string` and `print` with matching
signatures in its module.

> **Ad-hoc multi-bounds** like `T: Add + Eq` are not supported. Define a named
> combined trait instead. See
> [Language Reference: Trait Inheritance](../LANGUAGE_REFERENCE.md#trait-inheritance).

## 7. Conditional Conformance

What if you have a generic type like `Pair[T]`, and you want `==` to work on it — but
only when the inner type `T` itself supports `==`? This is **conditional conformance**:

```tl
#module Geo

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

eq(a: Vec2, b: Vec2) {
    a.x == b.x && a.y == b.y
}

#module Wrapper

Pair[A] : { fst: A, snd: A }

// Pair[T] satisfies Eq when T satisfies Eq
eq[T: Eq](a: Pair[T], b: Pair[T]) {
    a.fst == b.fst && a.snd == b.snd
}

// Pair[T] satisfies Add when T satisfies Add
add[T: Add](a: Pair[T], b: Pair[T]) {
    Pair(fst = a.fst + b.fst, snd = a.snd + b.snd)
}

#module main

are_equal[T: Eq](a: T, b: T) { a == b }

main() {
    p1 := Wrapper.Pair(fst = Geo.Vec2(x = 1, y = 2), snd = Geo.Vec2(x = 3, y = 4))
    p2 := Wrapper.Pair(fst = Geo.Vec2(x = 1, y = 2), snd = Geo.Vec2(x = 3, y = 4))
    p3 := Wrapper.Pair(fst = Geo.Vec2(x = 5, y = 6), snd = Geo.Vec2(x = 7, y = 8))

    c_printf(c"p1 == p2: %d\n", p1 == p2)
    c_printf(c"p1 == p3: %d\n", p1 == p3)
    c_printf(c"are_equal(p1, p2): %d\n", are_equal(p1, p2))

    p4 := p1 + p3
    c_printf(c"(p1 + p3).fst = (%d, %d)\n", p4.fst.x, p4.fst.y)
    0
}
```

```
$ tess run conditional.tl
p1 == p2: 1
p1 == p3: 0
are_equal(p1, p2): 1
(p1 + p3).fst = (6, 8)
```

The compiler reasons through this transitively:

1. `are_equal` requires `T: Eq`
2. `T` is `Pair[Vec2]` — does it satisfy `Eq`?
3. The `Wrapper` module has `eq[T: Eq](Pair[T], Pair[T])` — yes, but only if `Vec2: Eq`
4. The `Geo` module has `eq(Vec2, Vec2)` — confirmed

This chain can be arbitrarily deep. `Box[Pair[Vec2]]` works if each layer provides
a conditionally conforming `eq`.

---

## Summary

| Concept | How It Works |
|---------|-------------|
| **Operator overloading** | Define a function with the right name (`add`, `eq`, `cmp`, ...) in the type's module |
| **Derived operators** | `!=` from `eq`; `<`, `<=`, `>`, `>=` from `cmp`; `==` from `cmp` if no `eq` |
| **Compound assignment** | `+=` desugars to `a = add(a, b)` — no extra code needed |
| **Structural conformance** | A type satisfies a trait by having the right functions — no `impl` blocks |
| **Trait bounds** | `[T: Eq]` constrains a generic parameter; checked at the call site |
| **Custom traits** | `MyTrait[T] : { func(a: T) -> T }` — or inherit: `Combined[T] : A[T], B[T] { }` |
| **Conditional conformance** | `eq[T: Eq](a: Box[T], b: Box[T])` — conformance depends on the inner type |

### What Cannot Be Overloaded

- `&&`, `||` — short-circuit semantics, always `Bool`
- `.`, `->` — struct/pointer access
- `.&` (address-of), `.*` (dereference) — pointer operations
- `=` — assignment
- `::` — type predicate
- `[]` — indexing

### Limitations to Keep in Mind

- Both operands must be the **same type**. `vec * 2` won't work — use a named function
  like `scale(v, n)` instead.
- Dispatch is on the **left operand**. `2 * vec` won't find your overload since `Int` is
  a built-in type.
- Traits are **compile-time only** — no trait objects or dynamic dispatch. Use tagged
  unions for runtime polymorphism.

> For the complete reference, see the
> [Language Reference](../LANGUAGE_REFERENCE.md#operator-overloading) and the
> [Traits and Operators design document](../plans/TRAITS_AND_OPERATORS.md).
