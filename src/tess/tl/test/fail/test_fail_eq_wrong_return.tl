// eq() returns CInt instead of Bool — should fail trait satisfaction.
#module Pt

Pt: { x: Int }

eq(a: Pt, b: Pt) -> CInt { if a.x == b.x { 1 } else { 0 } }

#module main

are_equal[T: Eq](a: T, b: T) -> Bool { a == b }

main() {
    a := Pt.Pt(x = 1)
    b := Pt.Pt(x = 1)
    if are_equal(a, b) { 0 } else { 1 }
}
