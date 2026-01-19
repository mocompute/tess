# TL Language Syntax Overview

TL is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner style), generic types and functions, lambdas, closures, and C interoperability.

## Key Language Characteristics

**Expression-based:** Nearly everything in TL is an expression that produces a value. Control flow constructs like `if` and `case` can be used anywhere an expression is expected. Functions implicitly return the value of their final expression—no `return` keyword needed (though early `return` is supported).

**Implicit returns:** Functions return their last expression automatically. For functions that should return nothing, use `void` as the final expression:

```tl
greet(name) { c_printf("Hello, %s\n", name); void }
```

**Parentheses for grouping:** Use `( )` to group expressions and control evaluation order, or to introduce local bindings with let-in style:

```tl
result := (x := compute()
           x * x + 1)
```

**Curly braces for blocks:** Code blocks use `{ }` and contain one or more expressions. The block's value is its final expression.

**Distinct declaration vs assignment:** `:=` declares a new binding; `=` mutates an existing one. This enables predictable scoping and intentional shadowing.

**Postfix pointer operators:** Address-of (`.&`) and dereference (`.*`) are postfix rather than prefix, enabling chained access like `ptr.*.field` or `obj.&`.

## Program Structure

A TL program consists of one or more **modules**. Each module begins with a `#module` directive:

```tl
#module main
main() { 0 }
```

The entry point is the `main` function in the `main` module.

## Modules and Imports

```tl
#module ModuleName      // Declare a module
#import <filename.tl>   // Import another TL file
#include <header.h>     // Include a C header
```

Access module members with dot notation: `ModuleName.function()` or `ModuleName.Type`

## Types

### Primitive Types

- `Int` - 64-bit signed integer
- `Float` - 64-bit floating point
- `Bool` - Boolean (`true` or `false`)
- `String` - String type
- `Void` - No value
- `Type` - Type literals (used with `sizeof`)

### C-compatible Types

**Integers:**
- `CChar`, `CShort`, `CInt`, `CLong`, `CLongLong`
- Unsigned variants: `CUnsignedChar`, `CUnsignedShort`, `CUnsignedInt`, `CUnsignedLong`, `CUnsignedLongLong`

**Fixed-width integers:**
- `CInt8`, `CInt16`, `CInt32`, `CInt64`
- `CUInt8`, `CUInt16`, `CUInt32`, `CUInt64`

**Floating point:**
- `CFloat`, `CDouble`, `CLongDouble`

**Size types:**
- `CSize`, `CPtrDiff`

### Pointer Types

```tl
Ptr(T)          // Pointer to type T
CArray(T, N)    // C-style fixed-size array
```

### Generic Types

Types can have type parameters:

```tl
Point(a) : { x: a, y: a }
```

### Type Aliases

```tl
Pt = Point(Int)    // Creates a type alias
```

## Variables and Assignment

TL distinguishes between **binding** (`:=`) and **mutation** (`=`):

### Binding with `:=`

The `:=` operator introduces a new name with ML-style let-in semantics. Each use of `:=` creates a fresh binding that shadows any previous use of the same name in an outer scope. The binding's scope extends to the end of the enclosing block, and the entire expression evaluates to the block's final value:

```tl
x := 42              // Introduce new binding 'x'
x : Int := 42        // With explicit type annotation

x := 1
x := x + 1           // New binding shadows the previous one; x is now 2
x := x * 2           // Another new binding; x is now 4
```

Since bindings are expressions, you can capture the result of a let-in block using parentheses:

```tl
result := (
  a := 10
  b := 20
  a + b              // Block evaluates to 30
)
// result is 30; a and b are not visible here
```

This is equivalent to ML's `let a = 10 in let b = 20 in a + b`.

Shadowing is lexically scoped—inner bindings don't affect outer ones:

```tl
val := 1
if true {
  val := 2           // New binding, shadows outer val within this block
  // val is 2 here
}
// val is 1 here (outer binding unchanged)
```

### Mutation with `=`

The `=` operator mutates an existing binding in place. It requires that the name already exists:

```tl
x := 0               // Create binding
x = 10               // Mutate existing binding (x is now 10)
x += 2               // Compound assignment (also -=, *=, /=)
```

Mutation affects the binding in its original scope:

```tl
val := 1
if true {
  val = 2            // Mutates the outer binding
}
// val is 2 here (outer binding was modified)
```

### Why Two Operators?

This distinction makes code intent explicit:
- `:=` signals "I'm introducing a new name" (safe, no side effects on outer scope)
- `=` signals "I'm changing an existing value" (intentional mutation)

It also enables functional patterns where rebinding is preferred over mutation, while still allowing imperative style when needed.

## Functions

### Function Definition

```tl
add(a, b) { a + b }                    // Types inferred
add(a: Int, b: Int) -> Int { a + b }   // Explicit type annotations
log(msg) { c_printf("%s\n", msg); void }  // Use void for side-effect-only functions
```

The last expression in the function body is the return value. Use `void` as the final expression for functions that shouldn't return a meaningful value.

### Lambdas

```tl
f := (x) { x + 1 }           // Lambda expression
f := (x: Int) { x + 1 }      // With type annotation
(x) { x + 1 } (5)            // Immediate application (returns 6)
```

Lambdas capture variables from the enclosing scope (closures):

```tl
val := 10
f := () { val }    // Captures val
f()                // Returns 10
```

### Function Pointers

```tl
add1(x) { x + 1 }
fp := add1           // Get function pointer
apply(fp, 5)         // Pass as argument
```

### Generic Function Signatures

```tl
map(f: (a) -> b, arr: Arr(a)) -> Arr(b)
```

## Expressions

### Operators

**Arithmetic:** `+`, `-`, `*`, `/`, `%`

**Relational:** `==`, `!=`, `<`, `<=`, `>`, `>=`

**Logical:** `&&`, `||`, `!`

**Bitwise:** `&`, `|`, `~`, `^`, `<<`, `>>`

### Literals

```tl
42                  // Integer
1_234_567           // Integer with separators (underscores ignored)
0xFF                // Hexadecimal
0377                // Octal
3.14                // Float
1.5e-10             // Scientific notation
"hello"             // String
'a'                 // Character
'\n'                // Escape sequence
true, false         // Boolean
null                // Null pointer
void                // Void value
```

### Let-in Expressions

Variables can be declared inline within parentheses:

```tl
res := (x := 42
        x - 10)     // x is scoped to the expression; res = 32
```

### Type Assertions

```tl
x :: Int            // Assert that x has type Int
```

## Control Flow

### If Expressions

`if` is an expression that returns a value:

```tl
result := if condition { expr1 } else { expr2 }
result := if a { 1 } else if b { 2 } else { 3 }
```

### Case/Match

Pattern matching on values:

```tl
case n {
  0    { "zero" }
  1    { "one" }
  else { "other" }
}
```

With a custom predicate function:

```tl
case value, (a, b) { predicate(a, b) } {
  pattern1 { result1 }
  pattern2 { result2 }
}
```

### While Loop

```tl
while condition {
  body
}

// With update expression
while i < 10, i = i + 1 {
  body
}
```

### For-in Loop

The `for` statement iterates over collections using a module-based iterator interface:

```tl
for x in collection {
  use(x)           // x is a copy of the current element
}

for x.& in collection {
  x.* = 0          // x is a pointer; can mutate the element
}
```

#### Custom Iterator Modules

By default, `for` uses the `Array` module's iterator. You can specify a different iterator module:

```tl
for x in Module collection { ... }
for x.& in Module collection { ... }
```

For example, `IndexedArray` provides both the value and its index:

```tl
for it in IndexedArray xs {
  c_printf("index=%d value=%d\n", it.index, it.value)
}

for it.& in IndexedArray xs {
  it.ptr.* = it.index * 2    // Modify element using pointer
}
```

#### Iterator Interface

Iterator modules must implement these functions:

| Function | Signature | Purpose |
|----------|-----------|---------|
| `iter_init` | `(Ptr(T)) -> Iter` | Initialize iterator from collection pointer |
| `iter_value` | `(Ptr(Iter)) -> TValue` | Get current element value |
| `iter_ptr` | `(Ptr(Iter)) -> Ptr(TValue)` | Get pointer to current element |
| `iter_cond` | `(Ptr(Iter)) -> Bool` | Check if iteration should continue |
| `iter_update` | `(Ptr(Iter)) -> Void` | Advance to next element |
| `iter_deinit` | `(Ptr(Iter)) -> Void` | Clean up iterator resources |

The `Iter` type can contain arbitrary fields accessible in the loop body (like `index` in `IndexedArray`).

#### Desugaring

The statement `for x in Module xs { body }` desugars to:

```tl
iter := Module.iter_init(xs.&)
while Module.iter_cond(iter.&), Module.iter_update(iter.&) {
  x := Module.iter_value(iter.&)
  body
}
Module.iter_deinit(iter.&)
```

With `.&`, `iter_ptr` is called instead of `iter_value`.

### Return Statement

```tl
return value    // Early return from function
```

## Structs

### Definition

```tl
Point(a) : { x: a, y: a }              // Generic struct
Circle : { radius: Float }             // Concrete struct
Empty : { }                            // Empty struct
```

### Construction

```tl
p := Point(1, 2)                       // Positional arguments
p := Point(x = 1, y = 2)               // Named arguments
```

### Field Access

```tl
p.x                                    // Read field
p.x = 10                               // Write field
```

## Enums

```tl
Color : { Red, Green, Blue }           // Define enumeration
val := Color.Red                       // Access variant
```

Enums in a module are accessed with the module prefix:

```tl
#module Foo
Status : { Ok, Error }

#module main
s := Foo.Status.Ok
```

## Unions

```tl
Value : { | the_int: Int | the_float: Float }

v := Value(the_int = 42)
n := v.the_int
```

## Tagged Unions

```tl
#module Foo
Shape = | Circle    { radius: Float }
        | Square    { length: Float }

s := Foo.Shape_Circle(radius = 2.0)

// The type annotation on `s` is required, as are the annotations on
// each of the conditions.
area := case s: Foo.Shape {
  c:  Circle { c.radius * c.radius * 3.14159  }
  sq: Square { sq.length * sq.length }
}
```

Note that a tagged union is defined using the equal sign `=` rather
than the typical colon `:` used to declare other forms of types.

### Mutable tagged union case

```tl
case s.&: Foo.Shape {
  c:  Circle { c->radius *= 2.0  }
  sq: Square { sq->length * sq->length }
}
```

Use this syntax to access pointers to the each variant. Note the `.&`
suffix on the case variable. This is the same syntax used to access
mutable iterators with the `for` statement.

## Pointers

### Address-of and Dereference

```tl
ptr := obj.&          // Address-of (get pointer to obj)
val := ptr.*          // Dereference (get value at pointer)
```

### Arrow Operator

```tl
ptr->field            // Equivalent to ptr.*.field
```

### Pointer Indexing

```tl
ptr[i]                // Index into pointer (pointer arithmetic)
ptr[i] = value        // Write through pointer index
```

### Pointer Casts

Pointers can be cast implicitly via type annotation:

```tl
p : Ptr(Int) := c_malloc(sizeof(Int) * 10)
b : Ptr(Byte) := p    // Cast to different pointer type
```

## C Interoperability

### Embedding C Code

```tl
#ifc
int add(int a, int b) { return a + b; }
#endc
```

### Declaring C Functions

Functions with the `c_` prefix map directly to C functions:

```tl
c_printf(fmt: String, ...) -> CInt     // Declares printf
c_malloc(size: CSize) -> Ptr(any)      // Declares malloc
```

To call a C function named `foo`, declare it as `c_foo` in TL.

### Declaring C Symbols

```tl
c_INT_MAX : Int                        // C constant or variable
```

### Declaring C Struct Types

```tl
c_struct_foo : { x: CInt, y: CInt }    // Annotate C struct layout
```

### Built-in Functions

```tl
sizeof(T)           // Size of type or value in bytes
alignof(T)          // Alignment of type or value
```

## Global Variables

Variables declared at module scope are global:

```tl
#module Foo
counter := 0

increment() {
  counter = counter + 1
}
```

## Mutual Recursion

Functions can call each other without forward declarations:

```tl
is_even(n) { if n == 0 { true  } else { is_odd(n - 1)  } }
is_odd(n)  { if n == 0 { false } else { is_even(n - 1) } }
```

## Recursive Types

Types can reference themselves through pointers:

```tl
Node(a) : { value: a, next: Ptr(Node(a)) }

n1 := Node(value = 1, next = null)
n2 := Node(value = 2, next = n1.&)
```

Mutually recursive types are also supported—types can reference each other:

```tl
Tree(a)   : { value: a, children: Ptr(Forest(a)) }
Forest(a) : { trees: Array.T(Tree(a)) }

t1 := Tree(value = 1, children = null)
forest := Forest(trees = Array.with_capacity(Tree(Int), 16))
t1.children = forest.&
```

## Comments

```tl
// Single-line comment
```
