#module Dist

// Distance has eq and cmp but explicitly opts out of Eq conformance.
[[no_conform(Eq)]]
Distance: { value: Int }

eq(a: Distance, b: Distance) -> Bool {
    a.value == b.value
}

cmp(a: Distance, b: Distance) -> CInt {
    da: CInt := a.value - b.value
    da
}


#module main

// Ord inherits Eq, so a type that denies Eq cannot satisfy Ord either.
is_less[T: Ord](a: T, b: T) -> Bool {
    a < b
}

main() {
    a := Dist.Distance(value = 1)
    b := Dist.Distance(value = 2)

    // This should fail: Distance has [[no_conform(Eq)]], and Ord requires Eq
    if is_less(a, b) { 0 } else { 1 }
}
