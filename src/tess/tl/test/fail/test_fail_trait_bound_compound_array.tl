// T: Greet only appears inside Array[T]. Thing has no greet impl, so the
// bound should fail — but the current bare-symbol fallback in
// resolve_type_param_concrete misses compound annotations.

#import <Array.tl>

#module Foo

Thing: { x: CInt }

#module main

Greet[T]: {
    greet(self: T) -> CInt
}

first_greet[T: Greet](arr: Array[T]) -> CInt {
    0
}

main() -> CInt {
    arr := Array.init(Foo.Thing(x = 1), 1zu)
    first_greet(arr)
}
