// Test: operator overload eq/neq with Ptr[Const[T]] parameters.
// Ensures auto-address-of works for trait conformance via operator syntax.

#module Foo

Foo: { v: Int }

eq(a: Ptr[Const[Foo]], b: Ptr[Const[Foo]]) -> Bool {
    a->v == b->v
}

#module main

main() {
    f1 := Foo(v = 1)
    f2 := Foo(v = 1)
    f3 := Foo(v = 2)

    r := 0

    // eq: same values
    if !(f1 == f2) { r = r + 1 }

    // eq: different values
    if f1 == f3 { r = r + 1 }

    // neq: different values
    if !(f1 != f3) { r = r + 1 }

    // neq: same values
    if f1 != f2 { r = r + 1 }

    r
}
