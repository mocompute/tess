# TL Language Syntax Overview

TL is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner style), generic types and functions, lambdas, closures, and C interoperability.

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

```tl
x := 42              // Declaration with type inference (creates new binding)
x : Int := 42        // Declaration with explicit type annotation
x = 10               // Reassignment (mutates existing binding)
x += 2               // Compound assignment (also -=, *=, /=)
```

The `:=` operator declares a new variable; `=` reassigns an existing one. This enables lexical scoping with shadowing:

```tl
val := 1
if true {
  val := 2    // New binding, shadows outer val
}
// val is still 1 here
```

## Functions

### Function Definition

```tl
add(a, b) { a + b }                    // Types inferred
add(a: Int, b: Int) -> Int { a + b }   // Explicit type annotations
```

The last expression in the function body is the return value.

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

```tl
for x in collection {
  use(x)           // x is a copy
}

for x.& in collection {
  x.* = 0          // x is a pointer; can mutate
}
```

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

## Comments

```tl
// Single-line comment
```
