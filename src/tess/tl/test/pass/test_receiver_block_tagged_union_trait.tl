// Receiver block forward declaration on a tagged union resolves to the first
// variant type (_A) instead of the full tagged union (Foo) during trait bound
// checking.  The same forward declaration as a free function works correctly.
//
// Error: trait_bound_not_satisfied: eq has signature Ptr[Const[Foo___A]] ...
//        expected Foo__Foo ...

#module Foo

_A: { x: CInt }
_B: { y: CInt }
Foo: { | a: _A | b: _B }

a: Ptr[Const[Foo]], b: Ptr[Const[Foo]]: {
    eq () -> Bool
}

eq(a, b) { true }

#module main

check[T: Eq](a: T, b: T) -> Bool { a == b }

main() {
    a: Foo := void
    b: Foo := void
    if check(a, b) { 0 } else { 1 }
}
