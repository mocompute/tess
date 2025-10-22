// Basics

//

// Comments: line comments only, no block comments

//
// Whitespace is not significant.
//

// The parser determines that an expression has terminated when it
// encounters one of the following:
//
// - the `end` keyword
// - the semicolon: `;`
// - any builtin keyword which begins a new expression, such as `let` and `if`
// - end of file
//
// Note: the semicolon `;` is only valid for use in certain contexts:
// within a `struct` declaration and within a block expression
// (between `begin` and `end` keywords).
//

//
// Keywords
//
// begin, else, end, false, fun, if, in, let, struct, then, true
//

//
// Builtin types
//
// bool, int, float, nil, string
//
// The nil type corresponds to void, not null. That is, it indicates
// the lack of a value, rather than indicating a zero or empty value.
// For instance, a function which "returns" nil actually returns no
// value. nil is written `()` in TL programs.
//

//
// Reserved symbols
//
// - Anything that is surrounded by a single underscore, e.g. `_special_`
// - Anything that starts with `std_`
// - `std.tl` defines a number of arithmetic functions without the
//   `std_` prefix because they are judged to be universally useful.
//   The behaviour of a program which defines a function with the same
//   name as a function in `std.tl` is not defined.
//
// In addition, the symbol prefix `c_` is recognized as an invocation
// of a C function. For example, `c_malloc` can be used to access the
// `malloc` function.
//

//
// Compiler intrinsics
//
// These are functions which start with `_tl_` and end with `_`. They
// may or may not evaluate their arguments. They are intended for use
// by the `std` preamble. For example, `_tl_add_` is an intrinsic
// function that invokes the arithmetic addition operator `+`.
//
// Intrinsics are type-checked like all other functions. They are
// type-annotated in `std.tl`.

// Top level declarations:
// 1. let
// 2. struct

// Top level: let
//
// Used to define a function:

let fact x =
    if   lte x 1
    then 1
    else mul x (fact (decr x))

// Top level: struct

struct Point =
    x : int,
    y : int
end


// Expressions

// Let expression

// Simple let expression

// Matching let expression

let p = Point 1 2 in
let (left = x, top = y) = p in ...

// The ellipsis...

struct Big =
  a : int,
  b : int,
  c : int,
  d : string,
  // etc
end

let b = Big // ...
let (val = a, rest = ...) = b in
  // val =>  takes value of field a
  // rest => a labelled tuple of the remaining fields, e.g. (b = 1, c = 2, d = "hello")


// Block expression

// Multiple expressions separated by `;`. Expression value is the
// value of the final expression.

// Type annotations

// Type annotations on let symbols

// Type annotations on function parameter symbols

// Type annotations on function values

// Tuples

// Labelled tuples

//
// Match expression
//
// A expression with any number of conditional arms. The first one
// that matches correctly is evaluated and becomes the result of the
// expression.
//
// Note that the pattern matching is left-to-right, the same as when
// used in a let-match expression. That is, the label being set, or
// the value being tested is on the left of the equal sign, and the
// pattern being matched is on the right hand side. This differs from
// some other lanugages with similar syntax, but was chosen for its
// consistency: the right hand side always refers to the structure of
// the object whose values are being extracted.
//


let p = Point 1 2 in
let result =
  match p in
  | (0 = x) -> "zero"
  | (1 = x) -> "one"
  end

// Matching on simple values
let result =
  match val in
  | 0 -> "zero"
  | 1 -> "one"
  | else -> "something else"
  end

// Binding symbols to match
let p = Point 1 2 in
  let result =
    match p in
    | (top = y) -> mul top 3
    end
// => result = 6


//
// Arrays
//

// array indexing


let xs = [0, 1, 2] in
  xs.(0) := 1
  xs.[0] := 1
  xs[1] := 1



sizeof (struct Point)
sizeof int
sizeof (typeof Point)

typeof (Point)

let Point_array_create n : int -> *Point =
  let sz = mul n (_tl_sizeoft_ Point) in
  c_malloc sz

// made by compiler when a struct is defined
let Point_tl_sizeof : () -> int
let Point_tl_offset : int -> int // convert integer array index to byte offset

// what about allocator-aware interface??
let Point_anew : int -> *Point   // allocate array of size n?
let Point_aget : (*Point, int) -> Point
let Point_aset : (*Point, int, Point) -> ?
or
let Point_aget : (*Point, int) -> *Point

  (Point_aget ptr 3).*.x := 1
  (Point_aget ptr 3).* := (Point new val)




let points = Point_array_create 10 in
  points.(3) := (Point 1 2)
  Point_array_set points 3 (Point 1 2)
  Point_array_get points 3
  Point_aset points 3 (Point 1 2)
  Point_aget points 3


//
// Conditional
//

let res =
  cond
  | eq val 0 -> ...
  | eq val 1 -> ...
  | lt val 10 -> ...
  | else -> ...
  end




//
// Pipe
//

let x1 = val in
  x1 |> step1 |> step2 |> step3
end
// or with keyword "it"
pipe x1 = initval
  in  step1 it
  and step2 it extra
  and step3 flipped it more
  and it became a function
end
// again, prefer prefix notation for everything

let x1 = initval
    then step1 it
    then step2 it extra
    then step3 flip it
    then it x y z


let res =
    with initval
    do step1 it
    do step2 it extra
    do step3 flip it
    do it ()
    end

let res =
  with it = initval
  do step1 it
  do step2 it
  end

// equivalent to
let res = initval in
let res = step1 res in
let res = step2 extra in
let res = step3 flip res in
let res = res ()
in  res


//
// Some alternative syntax ideas
//

// Tuple notation instead of curried

let add : a * a -> a
let add (l, r) = l + r

// inline type annotation is unambiguous this way
let add (l, r) : a * a -> a = l + r
let foo () = 42
let unary(x) = 1
let main () = add (1, 2) ; foo() ; unary(3)

vs
let main () = add 1 2


// also visually more obvious that it's a function, since we use the same let keyword

let val = 123
let foo x = 123

vs

let val = 123
let foo (x) = 123

let lam = fun (x, y) -> x + y



//
// even crazier, completely different, imperative block style, no let
//

- everything is an expression: has a value
- early return using 'return' and 'break'
- conditional control flow: cond
- looping control flow: for

foo(x) {
  a = 1 b = 2
  c = add(a, b)
  do_something()
  l = () { a }
  if l() { ... } else if { ... } else { ... }
  for ( ... ) { ... }
  l()

 // sugar for if-elseif-else:
 cond {
  (expr) { ... }
  (expr) { ... }
  ()     { ... }
  }

  // lambda functions
  f = (...) { ... }




}

vs

let foo x =
 let a = 1 in
 let b = 2 in
 let c = add a b in
 let _ = do_something ()
 let l = fun () -> a in
 l ()


 EBNF
 definition   =
 concat       ,
 termination  ;
 alternation  |
 optional     [ ... ] none or once
 repetition   { ... } none or more
 grouping     ( ... )
 terminal     " ... "
 comment      (* ... *)
 special      ? ... ?
 exception    -
