// A type with eq but NOT cmp should NOT satisfy Ord.
// Ord requires cmp directly; the eq-from-cmp derivation does not help here.

#module Foo

Thing: { val: Int }

eq(a: Thing, b: Thing) -> Bool {
    a.val == b.val
}


#module main

is_sorted[T: Ord](a: T, b: T) -> Bool {
    a <= b
}

main() {
    a := Foo.Thing(val = 1)
    b := Foo.Thing(val = 2)

    // This should fail: Thing has eq but not cmp, so it doesn't satisfy Ord
    if is_sorted(a, b) { 0 } else { 1 }
}
