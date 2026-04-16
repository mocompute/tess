// T: Greet bound where T is inside Ptr[Const[T]], called with a Const argument
// whose type has no greet implementation. Const is stripped from both sides so
// T = Thing, and the check correctly reports that Thing does not satisfy Greet.
#module Foo

Thing: { x: CInt }

#module main

Greet[T]: {
    greet(self: T) -> CInt
}

poke_const[T: Greet](p: Ptr[Const[T]]) -> CInt { 0 }

main() {
    t: Const := Foo.Thing(x = 1)
    poke_const(t.&)
}
