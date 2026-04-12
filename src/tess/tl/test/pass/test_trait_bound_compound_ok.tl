// Positive regression for the structural fallback in resolve_type_param_concrete.
// Each generic function has its bound type parameter only inside a compound
// annotation (Array[T], Ptr[T], return T). Thing DOES implement greet, so
// all three calls must still compile and run.

#import <Array.tl>

#module Thing

Thing: { x: CInt }

greet(self: Thing) -> CInt { self.x }

#module main

Greet[T]: {
    greet(self: T) -> CInt
}

first_greet[T: Greet](arr: Array[T]) -> CInt {
    0
}

poke[T: Greet](p: Ptr[T]) -> CInt {
    t := p.*
    t.greet()
}

first[T: Greet](arr: Array[T]) -> T {
    arr.front()
}

main() -> CInt {
    arr := Array.init(Thing(x = 7), 1zu)
    error := 0
    error += first_greet(arr)

    t := Thing(x = 42)
    error += if poke(t.&) == 42 { 0 } else { 1 }

    f := first(arr)
    error += if f.x == 7 { 0 } else { 1 }

    error
}
