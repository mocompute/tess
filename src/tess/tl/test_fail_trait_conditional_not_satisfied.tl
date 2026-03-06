#module Wrapper

Pair[A] : { fst: A, snd: A }

// Pair[T] satisfies Eq only when T satisfies Eq
eq[T: Eq](a: Pair[T], b: Pair[T]) -> Bool {
    a.fst == b.fst && a.snd == b.snd
}


#module NoEq

// A type with NO eq function — does not satisfy Eq
Thing : { x: Int }


#module main

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    // Pair[Thing] should NOT satisfy Eq because Thing does not satisfy Eq
    a := Wrapper.Pair(fst = NoEq.Thing(x = 1), snd = NoEq.Thing(x = 2))
    b := Wrapper.Pair(fst = NoEq.Thing(x = 1), snd = NoEq.Thing(x = 2))
    if are_equal(a, b) { 0 } else { 1 }
}
