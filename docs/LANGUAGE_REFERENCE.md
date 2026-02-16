# Tess Language Reference

Tess is a statically-typed, compiled programming language that transpiles to C. It features type inference (Hindley-Milner style), generic types and functions, lambdas, closures, and C interoperability.

## Key Language Characteristics

**Expression-based:** Nearly everything in Tess is an expression that produces a value. Control flow constructs like `if`, `case`, and `when` can be used anywhere an expression is expected. Functions implicitly return the value of their final expression—no `return` keyword needed (though early `return` is supported). Note: assignment with `=` is a statement, not an expression, and has no value.

**Implicit returns:** Functions return their last expression automatically. For functions that should return nothing, use `void` as the final expression after an expression that produces a value:

```tl
greet(name) { c_printf("Hello, %s\n", name)  void }
```

If a function ends with an assignment statement (`=`), it already returns `Void` since assignments have no value:

```tl
set_name(n) { name = n }   // No void needed - assignment has no value
```

**Parentheses for grouping:** Use `( )` to group expressions and control evaluation order, or to introduce local bindings with let-in style:

```tl
result := (x := compute()
           x * x + 1)
```

**Curly braces for blocks:** Code blocks use `{ }` and contain one or more expressions. The block's value is its final expression.

**Minimal syntax:** Tess favors consistency over special-purpose constructs. A colon (`:`) always introduces a type—whether annotating a variable (`x: Int`), defining a type (`Point : { ... }`), or declaring a field. Parentheses (`()`) always mean application: calling a function, constructing a value, or instantiating a generic. Braces (`{ }`) delimit bodies uniformly across functions, types, and blocks. Statements and expressions are separated by whitespace (conventionally newlines)—there are no semicolons. Pointer operators are postfix (`.&`, `.*`, `->`) rather than prefix, reading left-to-right like field access. The result is a small grammar with few special cases.

**Distinct declaration vs assignment:** `:=` declares a new binding (expression with value); `=` mutates an existing one (statement with no value). This enables predictable scoping and intentional shadowing.

## Program Structure

A Tess program consists of one or more **modules**. Each module begins with a `#module` directive:

```tl
#module main
main() { 0 }
```

The entry point is the `main` function in the `main` module.

## Modules and Imports

```tl
#module ModuleName      // Declare a module
#import <filename.tl>   // Import another Tess file
#include <header.h>     // Include a C header
```

Access module members with dot notation: `ModuleName.function()` or `ModuleName.Type`

### Packages

Modules can be distributed as `.tlib` packages. When a package is declared as a dependency via `depend()` in `package.tl`, all its modules are loaded automatically -- no `#import` needed. Consumer code accesses package modules with the same qualified syntax: `Module.function()`.

See [PACKAGES.md](PACKAGES.md) for the full package system reference.

### Module Initialization

Modules can define a `__init()` function that runs automatically when the module is loaded:

```tl
#module Config

default_value := 0

__init() {
  default_value = load_from_env()
}
```

Module initialization functions are called in dependency order before `main()` executes.

**Note:** `__init()` only works in named modules (those with a `#module` declaration other than `main`).

## Types

### Primitive Types

- `Int` - 64-bit signed integer
- `Float` - 64-bit floating point
- `Bool` - Boolean (`true` or `false`)
- `Void` - No value
- `Type` - Type values (used with `sizeof`, `alignof`)

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
Ptr[T]              // Mutable pointer to type T
Ptr[Const[T]]       // Const pointer to type T (read-only)
CArray[T, N]        // C-style fixed-size array
```

`Const[T]` is a built-in type qualifier. See [Const Pointers](#const-pointers) for details.

### Generic Types

Types can have type parameters:

```tl
Point[a] : { x: a, y: a }
```

### Type Aliases

```tl
Pt = Point[Int]    // Creates a type alias
```

### Explicit Type Parameters

Generic functions declare type parameters using square brackets. Square brackets always denote type arguments; parentheses always denote value arguments or constructors:

```tl
// Function with explicit type parameter
empty[T]() -> Array[T] {
  with_capacity[T](16)
}

// Call with explicit type argument
arr := empty[Int]()           // Creates Array[Int]
floats := empty[Float]()      // Creates Array[Float]
```

This pattern is commonly used in the standard library for functions that need to create values of a generic type.

## Type Annotations

The Tess compiler uses Hindley-Milner style type inference. Annotations are optional in most cases.

### When Type Annotations Can Be Omitted

#### Variable Bindings with Inferrable RHS

| Pattern | Example | Reason |
|---------|---------|--------|
| Integer literals | `x := 42` | Literal type is `Int` |
| Float literals | `x := 3.14` | Literal type is `Float` |
| Struct constructors | `p := Point(x = 1, y = 2)` | Type inferred from constructor |
| Tagged union constructors (with constraining fields) | `opt := Some(42)` | Type parameter inferred from argument value |
| CArray declaration | `arr: CArray[Int, 10] := void` | CArray is a type annotation |
| Function calls | `result := add(1, 2)` | Return type inferred from function |

#### Function Parameters and Return Types

Functions that are **called directly** can have types inferred:

```tl
// All annotations can be omitted:
add(a, b) { a + b }           // Inferred from usage
apply(f, x) { f(x) }          // Inferred from how arguments are used
map(f, arr) { ... }           // Inferred from call sites
```

### When Type Annotations ARE Required

#### C FFI Declarations

```tl
c_malloc(size: CSize) -> Ptr[any]    // C functions need full signatures
c_printf(fmt: CString, ...) -> CInt
```

#### Pointer Casts

```tl
p : Ptr[Int] := some_ptr              // Casting from different pointer type
bytes : Ptr[CUnsignedChar] := int_ptr // Explicit cast required
```

#### C Type Disambiguation

Integer literals default to `Int`, float literals to `Float`. Use annotations to specify C types:

```tl
x : CInt := 42                  // Force CInt instead of Int
f : CFloat := 3.14              // Force CFloat instead of Float
sz : CSize := 1024              // Force CSize
```

#### c_malloc Return Type

`c_malloc` returns `Ptr[any]` which must be annotated:

```tl
p : Ptr[Int] := c_malloc(sizeof[Int]() * 10)    // Required
buffer : Ptr[CChar] := c_malloc(256)            // Required
```

#### Functions Only Used as Function Pointers

When a function is **never called directly** but only stored/passed as a pointer:

```tl
// Required - only referenced as `double/1`, never called directly
double(x: Int) -> Int { x * 2 }

// Later used as:
holder := FnHolder(fn = double/1)
```

#### Functions with Struct Parameters Containing Function Pointers

When a function takes a struct that has function pointer fields, annotations are required:

```tl
Handler : { fn: (Int) -> Int }

// Required - even though called directly, the struct has a function pointer field
apply_handler(h: Handler, x: Int) -> Int {
  h.fn(x)
}
```

#### Tagged Union Variants Without Type-Constraining Fields

When a constructor doesn't constrain all type parameters:

```tl
T[a] : | Some { v: a }
       | None

good: T[Int] := None()                // Required - None has no field with type `a`
opt := Some(42)                       // Not required - argument constrains `a = Int`
```

```tl
Either[a, b] : | Left  { v: a }
               | Right { v: b }

x: Either[Int, Bool] := Left(42)     // Required - Left only constrains `a`
```

#### Functions Returning Untyped Values

```tl
foo() -> Ptr[any] { return null }     // Required - null has no type
```

### Quick Reference Table

| Scenario | Annotation Required? |
|----------|---------------------|
| Integer literal `42` | No (inferred as `Int`) |
| Float literal `3.14` | No (inferred as `Float`) |
| Struct constructor | No (inferred from constructor) |
| Tagged union with constraining field | No |
| Tagged union without constraining field | **Yes** (at binding site) |
| Tagged union `when` expression | No (type inferred from scrutinee) |
| Tagged union `case` expression | **Yes** (when type not inferrable) |
| CArray declaration | **Yes** (type annotation required) |
| CArray decay to pointer | **Yes** (explicit `Ptr[T]` annotation) |
| Pointer cast to different type | **Yes** |
| C type (CInt, CFloat, etc.) | **Yes** |
| c_malloc result | **Yes** |
| C FFI function declaration | **Yes** |
| Function called directly | No (params and return inferred) |
| Function only used as pointer | **Yes** |
| Function with struct param containing fn pointer | **Yes** |
| Return null | **Yes** |

## Variables and Assignment

Tess distinguishes between **binding** (`:=`) and **mutation** (`=`):

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

**Assignment is a statement, not an expression.** Unlike `:=` which produces a value, `=` has no value. This means functions that end with an assignment implicitly return `Void`:

```tl
set_value(v) {
  global_val = v     // Assignment is last - function returns Void
}
```

No explicit `void` is needed after an assignment statement.

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
- `:=` signals "I'm introducing a new name" (expression with a value)
- `=` signals "I'm changing an existing value" (statement with no value)

It also enables functional patterns where rebinding is preferred over mutation, while still allowing imperative style when needed.

## Functions

### Function Definition

```tl
add(a, b) { a + b }                    // Types inferred
add(a: Int, b: Int) -> Int { a + b }   // Explicit type annotations
log(msg) { c_printf("%s\n", msg)  void }  // void needed after expression
update(v) { value = v }                // No void needed - assignment has no value
```

The last expression in the function body is the return value. Use `void` as the final expression when the last statement is an expression (like a function call) but the function should return nothing. If the function ends with an assignment (`=`), no `void` is needed.

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

**Capture by reference:** Closures capture variables by reference, not by value. This means mutations after the lambda is created are visible inside the lambda:

```tl
x := 1
f := () { x }
x = 0              // Mutate x after creating lambda
f()                // Returns 0, not 1
```

This also means lambdas can mutate captured variables:

```tl
counter := 0
increment := () { counter = counter + 1 }
increment()        // counter is now 1
increment()        // counter is now 2
```

**Lambdas cannot be returned from functions:** Because lambdas capture variables by reference, returning a lambda from a function would create dangling pointers to stack variables that no longer exist. The compiler prohibits this:

```tl
// ERROR: Cannot return lambda from function
make_adder(n) { (x) { x + n } }
```

If you need to return a callable, use a named function instead:

```tl
add1(x) { x + 1 }
get_adder() { add1 }     // OK: returns function pointer, not lambda
```

### Function Pointers

To get a pointer to a function, use the function name followed by `/` and its arity (number of parameters):

```tl
add1(x) { x + 1 }
fp := add1/1         // Get pointer to add1 (arity 1)
apply(f, x) { f(x) }
apply(fp, 5)         // Pass function pointer as argument
```

Function pointers can be stored in structs:

```tl
f1() { 1 }
Ctx[T] : { callback: T }
ctx := Ctx(callback = f1/0)
ctx.callback()       // Call through struct field
```

### Generic Function Signatures

```tl
map[a, b](f: (a) -> b, arr: Arr[a]) -> Arr[b]
```

### Function Overloading by Arity

Functions with the same name can be defined with different numbers of parameters. The compiler resolves calls based on the number of arguments provided:

```tl
add(x) { x }
add(x, y) { x + y }
add(x, y, z) { x + y + z }

main() {
  a := add(1)        // Calls single-argument version
  b := add(1, 2)     // Calls two-argument version
  c := add(1, 2, 3)  // Calls three-argument version
}
```

Each overload must have a distinct number of parameters. Overloads are resolved at compile time based on argument count.

When taking a pointer to an overloaded function, the `/arity` syntax disambiguates which version:

```tl
fp := add/2          // Pointer to the two-argument version
```

### Tail Call Optimization

Tess defines tail call optimization (TCO) as part of the language semantics. The generated C code is structured to enable TCO, and the underlying C compilers implement the optimization on all supported platforms. Tail-recursive functions are compiled to loops, allowing deep recursion without stack overflow:

```tl
sum_to(n, acc) {
  if n == 0 { acc }
  else { sum_to(n - 1, acc + n) }  // Tail call - optimized to loop
}

result := sum_to(1000000, 0)       // Works without stack overflow
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
"hello"             // String (CString)
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

### Block Expressions

A block can be used as an expression by wrapping it in parentheses: `({ ... })`. The block's value is its final expression:

```tl
result := ({
  setup()
  compute()
  cleanup()
  42           // Block evaluates to 42
})
```

This is useful when you need to execute multiple statements in an expression context, such as in the middle of a larger expression or as an argument to a function:

```tl
total := base_cost + ({
  discount := calculate_discount()
  apply_tax(price - discount)
})
```

Block expressions combine naturally with let-in bindings:

```tl
value := ({
  x := 10
  y := 20
  x * y        // Returns 200
})
```

### Type Predicates

```tl
x :: Int            // true if x has type Int
```

### Attributes

Attributes annotate declarations with metadata that can be queried at compile time.

#### Declaring Attributes

Prefix a declaration with `[[name]]`:

```tl
[[my_attr]] x := 42
[[my_attr]] foo() { ... }
```

Multiple attributes use a comma-separated list:

```tl
[[attr1, attr2]] bar() { ... }
```

Attributes can take arguments:

```tl
[[NFA(42)]] nfa_func() { ... }
```

Attributes work on both top-level and local declarations:

```tl
main() {
    [[local_attr]] x := 123
}
```

#### Attribute Predicates

Use `::` with an attribute set on the right-hand side to test whether a symbol has a given attribute. Like type predicates, attribute predicates are evaluated at compile time and produce a boolean value.

```tl
sym :: [[my_attr]]          // true if sym has attribute my_attr
foo/0 :: [[my_attr]]        // use arity-mangled name for functions
```

For attributes with arguments, an exact match requires the same arguments:

```tl
nfa_func/0 :: [[NFA(42)]]  // true  - exact match
nfa_func/0 :: [[NFA(99)]]  // false - argument mismatch
```

A general match (attribute name without arguments) matches regardless of the declared arguments:

```tl
nfa_func/0 :: [[NFA]]      // true  - matches any NFA(...) attribute
```

## Control Flow

### If Expressions

`if` is an expression that returns a value:

```tl
result := if condition { expr1 } else { expr2 }
result := if a { 1 } else if b { 2 } else { 3 }
```

### Case/Match (Value Matching)

Pattern matching on values using equality by default. For tagged union destructuring, see [`when`](#pattern-matching-when-expression).

```tl
case n {
  0    { "zero" }
  1    { "one" }
  else { "other" }
}
```

#### Custom Predicates

By default, `case` uses `==` to compare the value against each pattern. You can provide a custom predicate function that takes two arguments (the value and the pattern) and returns a boolean.

**Using a named function:**

```tl
streq(a, b) { 0 == c_strcmp(a, b) }

result := case s, streq {
  "hello" { 0 }
  "world" { 1 }
  else    { 2 }
}
```

**Using an inline lambda:**

```tl
result := case s, (a, b) { 0 == c_strcmp(a, b) } {
  "hello" { 0 }
  "world" { 1 }
  else    { 2 }
}
```

In both forms, the predicate is called as `predicate(value, pattern)` for each arm until a match is found.

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

For example, `Array.Indexed` provides both the value and its index:

```tl
for it in Array.Indexed xs {
  c_printf("index=%d value=%d\n", it.index, it.value)
}

for it.& in Array.Indexed xs {
  it.ptr.* = it.index * 2    // Modify element using pointer
}
```

#### Iterator Interface

Iterator modules must implement these functions:

| Function | Signature | Purpose |
|----------|-----------|---------|
| `iter_init` | `(Ptr[T]) -> Iter` | Initialize iterator from collection pointer |
| `iter_value` | `(Ptr[Iter]) -> TValue` | Get current element value |
| `iter_ptr` | `(Ptr[Iter]) -> Ptr[TValue]` | Get pointer to current element |
| `iter_cond` | `(Ptr[Iter]) -> Bool` | Check if iteration should continue |
| `iter_update` | `(Ptr[Iter]) -> Void` | Advance to next element |
| `iter_deinit` | `(Ptr[Iter]) -> Void` | Clean up iterator resources |

The `Iter` type can contain arbitrary fields accessible in the loop body (like `index` in `Array.Indexed`).

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

### Break and Continue

Use `break` to exit a loop early, and `continue` to skip to the next iteration:

```tl
while i < 10, i = i + 1 {
  if i == 5 { break }       // Exit loop when i reaches 5
  if i % 2 == 0 { continue } // Skip even numbers
  c_printf("%d\n", i)
}

for x in collection {
  if x < 0 { continue }     // Skip negative values
  if x > 100 { break }      // Stop at first value over 100
  process(x)
}
```

Both `break` and `continue` work in `while` and `for` loops.

### Defer

`defer` schedules a statement or block to execute when the enclosing scope is exited, whether by `break`, `continue`, `return`, or falling through to the end:

```tl
defer x = 5                // Single statement

defer {                    // Block form
    a := 1
    x = 10
}
```

**Execution order:** Multiple defers in the same scope execute in LIFO (last-in, first-out) order:

```tl
x := 0
while true {
    defer x = x * 10 + 1   // Executes third
    defer x = x * 10 + 2   // Executes second
    defer x = x * 10 + 3   // Executes first
    break
}
// x is 321
```

**Works with all exit paths:** Defers run before `break`, `continue`, and `return`:

```tl
// With break
while true {
    defer x = 5
    break          // defer runs, then loop exits
}

// With continue
while i < 3, i = i + 1 {
    defer sum = sum + 1
    continue       // defer runs each iteration
}

// With return
while true {
    defer x = 0
    return x       // x's value is captured, then defer runs
}
```

**Capture-first returns:** When `return` is used with a defer, the return value is captured *before* defers execute. This also applies to implicit returns at the end of a function body:

```tl
f() {
    x := 42
    defer x = 0
    x              // Returns 42, not 0
}
```

**Nested scopes:** Each scope has its own defers. Inner defers run when the inner scope exits, outer defers run when the outer scope exits:

```tl
while true {
    defer x = x + 1       // Runs when outer loop exits
    while true {
        defer x = x + 10  // Runs when inner loop exits
        break
    }
    break
}
// Both defers have run
```

### Return Statement

```tl
return value    // Early return from function
```

Use `return` for early exit from a function. Since Tess is expression-based, implicit returns (the last expression in a function body) are preferred for normal control flow.

### Try

`try` is a prefix operator for error propagation. It works on any two-variant tagged union: it unwraps the first variant (success) or returns early from the enclosing function with the second variant (error).

```tl
Result[T, E] : | Ok { v: T } | Err { e: E }

parse(input) -> Result[Data, Error] { ... }

process(input) -> Result[Data, Error] {
    data := try parse(input)    // unwraps Ok, or returns Err
    transform(data)
}
```

This is purely structural — no special type names are required. Any two-variant tagged union works:

```tl
Option[T] : | Some { v: T } | None

lookup(key) -> Option[Value] { ... }

fetch(key) -> Option[Value] {
    val := try lookup(key)    // unwraps Some, or returns None
    validate(val)
}
```

`try` composes inline:

```tl
data := try parse(try read_file(path))
```

The enclosing function's return type must be compatible — it must be the same two-variant tagged union (or a type whose second variant matches the error being propagated).

`try` fully unwraps to the inner value of the success variant:

```tl
// These are equivalent:
data := try parse(input)

__tmp := parse(input)
__ok: Ok := __tmp else { return __tmp }
data := __ok.v
```

## Structs

### Definition

```tl
Point[a] : { x: a, y: a }              // Generic struct
Circle : { radius: Float }             // Concrete struct
Empty : { }                            // Empty struct
```

### Construction

```tl
p := Point(x = 1, y = 2)               // Named arguments required
```

### Field Access

```tl
p.x                                    // Read field
p.x = 10                               // Write field
```

### Uninitialized Fields

Use `void` to leave a field uninitialized during construction:

```tl
Buffer : { data: Ptr[Byte], size: Int, capacity: Int }

buf := Buffer(
  data = c_malloc(1024),
  size = void,         // Left uninitialized
  capacity = 1024
)
buf.size = 0           // Initialize later
```

This is useful when a field will be set immediately after construction or when working with low-level memory patterns.

### Nested Structs

Structs can contain nested struct definitions. Nested types are accessed using dot syntax (`Parent.Child`):

```tl
Outer[T] : {
  Inner : {
    value: T
  }
  contents: Inner
}

inner := Outer.Inner(value = 42)
outer := Outer(contents = inner)
outer.contents.value   // 42
```

Nested structs are desugared internally with double-underscore-separated names (e.g., `Outer__Inner`), but the user-facing syntax is always `Parent.Child`. This also works across modules:

```tl
#module Shapes
Canvas : {
  Layer : {
    name: CString
  }
  base: Layer
}

#module main
layer := Shapes.Canvas.Layer(name = "bg")
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
Shape : | Circle    { radius: Float }
        | Square    { length: Float }
        | None
```

### Construction

There are three ways to construct tagged union values:

**Unscoped constructor functions** take positional arguments and return the wrapped tagged union:

```tl
s := Circle(2.0)              // returns Shape
n := None()                   // returns Shape
```

From another module, prefix with the module name:

```tl
s := Foo.Circle(2.0)          // returns Foo.Shape
```

**Scoped type constructors** use named arguments and return the bare variant struct (not the tagged union):

```tl
c := Shape.Circle(radius = 2.0)   // returns bare Circle struct
```

**Make functions** wrap a bare variant struct into the tagged union:

```tl
c := Shape.Circle(radius = 2.0)   // bare Circle struct
s := make_Shape_Circle(c)         // wrapped Shape
```

### Existing Types as Variants

A variant can reference a pre-existing type using module-qualified syntax:

```tl
#module Geo

Point: { x: Float, y: Float }

Shape: | Circle { radius: Float }
       | Geo.Point
       | None
```

For types in the `main` module, use `main.TypeName`. A bare name (e.g., `| None`) always creates a new variant. No constructor function is generated for existing type variants — use the type's own constructor and the make function.

### Pattern Matching (When Expression)

The `when` keyword provides tagged union pattern matching with type inference. The tagged union type is inferred from the scrutinee — no type annotation needed:

```tl
area := when s {
  c:  Circle { c.radius * c.radius * 3.14159  }
  sq: Square { sq.length * sq.length }
}
```

Variant names in the arms are resolved automatically from the inferred type's module scope. For example, if `s` is a `Foo.Shape`, you write `Circle` in the arm — not `Foo.Circle`:

```tl
circle := Foo.Circle(2.0)
area := when circle {
  c:  Circle { c.radius * c.radius * 3.14159 }
  sq: Square { sq.length * sq.length }
  n:  None   { 0.0 }
}
```

`when` expressions must be exhaustive: there must be one arm per variant, or an `else` arm.

### Explicit Type Annotation (Case Expression)

When the tagged union type cannot be inferred from the scrutinee, use `case` with an explicit type annotation:

```tl
result := case opt: Option[Int] {
  s: Some { s.v }
  n: None { 0 }
}
```

This is needed when the scrutinee's type is ambiguous, such as inside a type predicate branch:

```tl
unwrap[T](opt_or_res, default: T) -> T {
    if opt_or_res :: Option[T] {
        case opt_or_res: Option[T] {
            s: Some { s.v }
            n: None { default }
        }
    } else if opt_or_res :: Result[T, U] {
        case opt_or_res: Result[T, U] {
            o: Ok { o.v }
            e: Err { default }
        }
    }
    else {
        _tl_fatal_(c"unwrap: invalid type")
    }
}
```

Prefer `when` when the type is known; use `case var: Type` when it isn't.

### Mutable when

```tl
when s.& {
  c:  Circle { c->radius *= 2.0  }
  sq: Square { sq->length * sq->length }
}
```

Use the `.&` suffix on the scrutinee to get pointers to each variant. This is the same syntax used to access mutable iterators with the `for` statement.

### Let-else

When you need a single variant's value for the rest of a scope, use let-else to unwrap it or exit early:

```tl
s: MySome := val else { return 0 }
// s is available for the rest of the scope
s.v + 1
```

The `else` block must diverge (`return`, `break`, or `continue`). This avoids trapping the unwrapped value inside a `when` arm when subsequent code needs it:

```tl
// Without let-else — value is trapped inside the arm
when val {
    s: MySome { use(s.v) }
    n: MyNone { return 0 }
}
// can't use s here

// With let-else — value available in the rest of the scope
s: MySome := val else { return 0 }
use(s.v)
```

For the conditional case (doing different things per variant), use `when` with `else`:

```tl
when val {
    s: MySome { s.v + 1 }
    else { fallback }
}
```

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
ptr.[i]               // Index into pointer (pointer arithmetic)
ptr.[i] = value       // Write through pointer index
```

### Pointer Casts

Pointers can be cast implicitly via type annotation:

```tl
p : Ptr[Int] := c_malloc(sizeof[Int]() * 10)
b : Ptr[Byte] := p    // Cast to different pointer type
```

### Const Pointers

`Ptr[Const[T]]` declares a pointer through which the target cannot be modified.
It transpiles to `const T*` in C.

```tl
read_value(p: Ptr[Const[Int]]) {
    p.*                       // OK: reading through const pointer
}

read_point(p: Ptr[Const[Point]]) {
    p->x + p->y              // OK: reading struct fields through const pointer
}
```

**Implicit coercion:** A mutable pointer can be passed where a const pointer is expected:

```tl
p : Ptr[Int] := c_malloc(8)
p.* = 42
val := read_value(p)         // OK: Ptr[Int] -> Ptr[Const[Int]]
```

**Mutation through const pointers is rejected:**

```tl
mutate(p: Ptr[Const[Int]]) {
    p.* = 10                  // Error: const violation
}
```

**Stripping const is rejected.** A const pointer cannot be passed where a mutable pointer is expected:

```tl
write(p: Ptr[Int]) { p.* = 10  0 }

pass(p: Ptr[Const[Int]]) {
    write(p)                  // Error: const violation
}
```

This applies at any pointer nesting level: `Ptr[Ptr[Const[T]]]` cannot be passed where `Ptr[Ptr[T]]` is expected.

**Limitation:** `Const[T]` cannot be used with generic type parameters. A function like `f(dst: Ptr[T], src: Ptr[Const[T]])` will fail because `T` cannot unify with both `X` and `Const[X]`. Use `Ptr[T]` for both parameters when `T` is generic, and reserve `Const` for concrete types like `Ptr[Const[CChar]]`.

### Array Decay

`CArray` is used as a type annotation to declare fixed-size C arrays. Decay to pointer must be explicit:

```tl
buffer: CArray[CChar, 256] := void    // Declare a fixed-size array
ptr: Ptr[CChar] := buffer             // Explicit decay to pointer
c_strcpy(ptr, "hello")                // Pass pointer to C functions
```

CArray supports direct indexing without decay:

```tl
arr: CArray[Int, 5] := void
arr.[0] = 42                          // Direct indexing on CArray
```

CArray fields in structs automatically decay to pointers on access:

```tl
Buffer: { data: CArray[CChar, 256], len: CInt }
b := Buffer(data = void, len = 0)
data_ptr: Ptr[CChar] := b.data        // Struct field access decays to Ptr
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
c_printf(fmt: CString, ...) -> CInt     // Declares printf
c_malloc(size: CSize) -> Ptr[any]       // Declares malloc
```

Use `Ptr[Const[T]]` for C functions that take `const` pointer parameters:

```tl
c_strlen(s: Ptr[Const[CChar]]) -> CSize           // const char*
c_strcmp(a: Ptr[Const[CChar]], b: Ptr[Const[CChar]]) -> CInt
```

To call a C function named `foo`, declare it as `c_foo` in Tess.

### Declaring C Symbols

```tl
c_INT_MAX : Int                        // C constant or variable
```

### Declaring C Struct Types

The `c_struct_` prefix transpiles directly to a C `struct` and is used
to annotate the types of the struct's fields. Interestingly, only the
fields used by the TL program need to be annotated.

```tl
c_struct_foo : { x: CInt, y: CInt }    // Annotate C struct layout
```

### Built-in Functions

```tl
sizeof[T]()         // Size of type T in bytes
sizeof(x)           // Size of value x in bytes
alignof[T]()        // Alignment of type T
alignof(x)          // Alignment of value x
```

### Compiler Intrinsics

These low-level intrinsics are used internally by the standard library:

```tl
_tl_sizeof_[T]()    // Type-parameterized sizeof (for generic code)
_tl_alignof_[T]()   // Type-parameterized alignof (for generic code)
_tl_fatal_(msg)     // Terminate with error message
```

The `_tl_sizeof_` and `_tl_alignof_` intrinsics differ from `sizeof`/`alignof` in that they work with type variables in generic functions:

```tl
allocate[a]() -> Ptr[a] {
  c_malloc(_tl_sizeof_[a]())   // Works with generic type parameter
}
```

## Global Variables

Variables declared at module scope are global variables:

```tl
#module Foo
counter := 0

increment() {
  counter = counter + 1
}
```

All global variables are thread-local. There are no non-thread-local variables.

## Mutual Recursion

Functions can call each other without forward declarations:

```tl
is_even(n) { if n == 0 { true  } else { is_odd(n - 1)  } }
is_odd(n)  { if n == 0 { false } else { is_even(n - 1) } }
```

## Recursive Types

Types can reference themselves through pointers:

```tl
Node[T] : { value: T, next: Ptr[Node[T]] }

n1 := Node(value = 1, next = null)
n2 := Node(value = 2, next = n1.&)
```

Mutually recursive types are also supported—types can reference each other:

```tl
Tree[T]   : { value: T, children: Ptr[Forest[T]] }
Forest[T] : { trees: Array.Array[Tree[T]] }

t1 := Tree(value = 1, children = null)
forest := Forest(trees = Array.with_capacity[Tree[Int]](16))
t1.children = forest.&
```

## Comments

```tl
// Single-line comment
```

## Naming Conventions

The standard library follows these conventions:

| Pattern | Meaning | Example |
|---------|---------|---------|
| `lowercase_snake` | Public functions | `with_capacity`, `iter_init` |
| `_leading_underscore` | Private/internal functions | `_bump_malloc`, `_find_bucket` |
| `c_name` | C function binding | `c_malloc`, `c_printf` |
| `c_struct_name` | C struct type annotation | `c_struct_timespec` |
| `PascalCase` | Types and modules | `Array`, `Alloc`, `Point` |
| `__double_underscore` | Special functions | `__init` (module initialization) |
| `_tl_name_` | Compiler intrinsics | `_tl_sizeof_`, `_tl_fatal_` |

Single-letter names like `a`, `b`, `T` are conventionally used for type parameters in generic definitions.

**Note:** Identifiers containing `__` (double underscore) are reserved for compiler name mangling and will be rejected by the parser. The only exceptions are `__init` (module initialization) and `c_` prefixed C interop symbols (e.g., `c__Exit`).
