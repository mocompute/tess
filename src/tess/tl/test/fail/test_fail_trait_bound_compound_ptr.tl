// T: Greet only appears inside Ptr[T]. Thing has no greet impl, so the
// bound should fail — but the current bare-symbol fallback in
// resolve_type_param_concrete misses compound annotations.

#module Foo

Thing: { x: CInt }

#module main

Greet[T]: {
    greet(self: T) -> CInt
}

poke[T: Greet](p: Ptr[T]) -> CInt {
    0
}

main() -> CInt {
    t := Foo.Thing(x = 1)
    poke(t.&)
}
