// T: Greet is reachable only through Array[T] (parameter) and T (return).
// No bare-symbol parameter mentions T, so the current fallback in
// resolve_type_param_concrete can't find a concrete binding and the bound
// is silently skipped.

#import <Array.tl>

#module Foo

Thing: { x: CInt }

#module main

Greet[T]: {
    greet(self: T) -> CInt
}

first[T: Greet](arr: Array[T]) -> T {
    arr.front()
}

main() -> CInt {
    arr := Array.init(Foo.Thing(x = 7), 1zu)
    t   := first(arr)
    t.x
    0
}
