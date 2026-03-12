#module Dist

// Distance opts out of Ord but still satisfies Eq.
[[no_conform(Ord)]]
Distance: { value: Int }

eq(a: Distance, b: Distance) -> Bool {
    a.value == b.value
}

cmp(a: Distance, b: Distance) -> CInt {
    da: CInt := a.value - b.value
    da
}


#module main

// Generic function with Eq bound — Distance should still satisfy this.
are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    error := 0

    a := Dist.Distance(value = 1)
    b := Dist.Distance(value = 1)
    c := Dist.Distance(value = 2)

    // Eq bound works — only Ord is blocked
    error += if are_equal(a, b) { 0 } else { 1 }
    error += if are_equal(a, c) { 1 } else { 0 }

    // Direct function call still works (no_conform only blocks trait conformance)
    r := Dist.cmp(a, c)
    error += if r < 0 { 0 } else { 1 }

    error
}
