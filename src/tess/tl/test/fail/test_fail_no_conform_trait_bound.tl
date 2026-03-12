#module Dist

// Distance has cmp but explicitly opts out of Ord conformance.
[[no_conform(Ord)]]
Distance: { value: Int }

cmp(a: Distance, b: Distance) -> CInt {
    da: CInt := a.value - b.value
    da
}

eq(a: Distance, b: Distance) -> Bool {
    a.value == b.value
}


#module main

is_less[T: Ord](a: T, b: T) -> Bool {
    a < b
}

main() {
    a := Dist.Distance(value = 1)
    b := Dist.Distance(value = 2)

    // This should fail: Distance has [[no_conform(Ord)]]
    if is_less(a, b) { 0 } else { 1 }
}
