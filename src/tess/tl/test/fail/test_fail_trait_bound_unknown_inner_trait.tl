// An impl function carries an unknown trait name on its own type parameter.
// Reached via conditional-conformance recursion in check_trait_bound_:
// are_equal demands Pair[Int]: Eq, which finds Wrapper.eq, whose own
// quantifier T carries the bogus bound `T: Eqq`.

#module Wrapper

Pair[A]: { fst: A, snd: A }

eq[T: Eqq](a: Pair[T], b: Pair[T]) -> Bool {
    a.fst == b.fst && a.snd == b.snd
}

#module main

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() -> CInt {
    a := Wrapper.Pair(fst = 1, snd = 2)
    b := Wrapper.Pair(fst = 1, snd = 2)
    if are_equal(a, b) { 0 } else { 1 }
}
