// Test: operator overload cmp with Ptr[Const[T]] parameters.
// Covers <, >, <=, >=, ==, != via cmp.

#module Bar

Bar: { v: Int }

cmp(a: Ptr[Const[Bar]], b: Ptr[Const[Bar]]) -> CInt {
    if a->v < b->v { -1 }
    else if a->v > b->v { 1 }
    else { 0 }
}

#module main

main() {
    a := Bar(v = 1)
    b := Bar(v = 2)
    c := Bar(v = 1)

    r := 0

    if !(a < b) { r = r + 1 }
    if !(b > a) { r = r + 1 }
    if !(a <= c) { r = r + 1 }
    if !(a >= c) { r = r + 1 }
    if !(a == c) { r = r + 1 }
    if !(a != b) { r = r + 1 }

    r
}
