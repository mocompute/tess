#module Wrapper

Pair[A]: { fst: A, snd: A }

eq[T: Eq](a: Pair[T], b: Pair[T]) -> Bool {
    a.fst == b.fst && a.snd == b.snd
}


#module Container

Box[A]: { inner: A }

eq[T: Eq](a: Box[T], b: Box[T]) -> Bool {
    a.inner == b.inner
}


#module NoEq

// A type with NO eq function
Thing: { x: Int }


#module main

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    // Box[Pair[Thing]] should NOT satisfy Eq:
    // Box requires Pair[Thing]: Eq, Pair requires Thing: Eq, but Thing has no eq
    a := Container.Box(inner = Wrapper.Pair(fst = NoEq.Thing(x = 1), snd = NoEq.Thing(x = 2)))
    b := Container.Box(inner = Wrapper.Pair(fst = NoEq.Thing(x = 1), snd = NoEq.Thing(x = 2)))
    if are_equal(a, b) { 0 } else { 1 }
}
