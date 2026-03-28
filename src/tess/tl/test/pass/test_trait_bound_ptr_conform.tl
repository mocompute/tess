// Test: trait bound [T: Eq] accepts types with Ptr-taking eq functions.

#module Baz

Baz: { v: Int }

eq(a: Ptr[Const[Baz]], b: Ptr[Const[Baz]]) -> Bool {
    a->v == b->v
}

#module main

are_equal[T: Eq](a: T, b: T) -> Bool { a == b }

main() {
    b1 := Baz(v = 42)
    b2 := Baz(v = 42)
    b3 := Baz(v = 99)

    r := 0

    if !(are_equal(b1, b2)) { r = r + 1 }
    if are_equal(b1, b3) { r = r + 1 }

    r
}
