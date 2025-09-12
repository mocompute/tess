// -*- mode: fsharp -*-

// Basics

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
// Any symbol not in this list is a valid identifier.
//
// begin, else, end, false, fun, if, in, let, then, true
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

let

// Top level: struct

struct Point =
    x : int ;
    y : int
end


// Expressions

// Let expression

// Simple let expression

// Matching let expression

// Block expression

// Multiple expressions separated by `;`. Expression value is the
// value of the final expression.

// Type annotations

// Type annotations on let symbols

// Type annotations on function parameter symbols

// Type annotations on function values

// Tuples

// Labelled tuples



struct point =
       x : int ;
       y : int
end

// recall we want to keep everything prefix or infix, not postfix

let pp : * point = c_malloc 16

// what about

let pp : *.point = c_malloc (_tl_sizeof_ point) // this sizeof doesn't work yet

*.(int, int) or *(int, int)
*(a -> b) vs ( *a -> b )

// chaining blocks, only in some cases
let x1 = val
// in begin are implied
step1; step2; result end

let x1 = val in
let x2 = step1 x1 in
let x3 = step2 x2 in
let x4 = step3 x3 in
  x4
end

let x1 = val in
  x1 |> step1 |> step2 |> step3
end
// or with keyword "it"
pipe x1 = val
  in  step1 it
  and step2 it extra
  and step3 flipped it more
  and it became a function
end
// again, prefer prefix notation for everything





let p = point 1 2 in ...

let tup : (int, int) = (1, 2)
let ptup : *(int, int) = &tup


let main ()  =
    let (p : ptr point) = c_malloc 16 in
    let obj = deref p in
    begin
        p x := 1 ;
        p.x := 1 ;

        p..x := 1;
        .p.x := 1;


        .p.x := 1;              // no good because of chaining .p.x.y.z what is going on?
        .p.x = 1 ;


        !p.x := 1;
        p!.x := 1;

        (deref p) . x := 1 ;


        p:x := 1;
        p:x <- 1;

        a : int <- 1;           // oof

        * ptr . x := 1
        // chaining?
        * ptr . * pp ...
        // or
        ptr . * . pp . * . x ?
        ptr.*.pp.*.x
        ptr.*.y := 2
        (assign (dot (deref ptr) x) 2)


        obj.x := 1 ;
        obj.y := 2 ;
        0 ;
    end


let map f xs =
     for

// compiler intrinsics

let x = _tl_sizeof_ foo
    => (intrinsic-application _tl_sizeof_ (symbol foo))
