// A type with neither eq nor cmp should NOT satisfy Eq.
// The eq-from-cmp derivation should not mask a truly missing conformance.

#module Foo

Thing : { val: Int }

add(a: Thing, b: Thing) -> Thing { Thing(val = a.val + b.val) }


#module main

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    a := Foo.Thing(val = 1)
    b := Foo.Thing(val = 1)

    // This should fail: Thing has neither eq nor cmp
    if are_equal(a, b) { 0 } else { 1 }
}
