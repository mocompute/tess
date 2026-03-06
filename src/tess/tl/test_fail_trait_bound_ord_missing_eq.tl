#module Foo

Thing : { val: Int }

// Thing has cmp but NOT eq -- should NOT satisfy Ord (which inherits from Eq)
cmp(a: Thing, b: Thing) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}


#module main

is_sorted[T: Ord](a: T, b: T) -> Bool {
    a <= b
}

main() {
    a := Foo.Thing(val = 1)
    b := Foo.Thing(val = 2)

    // This should fail: Thing has cmp but not eq, so it doesn't satisfy Ord
    if is_sorted(a, b) { 0 } else { 1 }
}
