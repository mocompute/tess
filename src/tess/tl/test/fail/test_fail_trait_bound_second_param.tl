#module Foo

Thing : { x: Int }

// Thing has no eq -- does not satisfy Eq


#module main

// Bound is on the second type parameter, not the first
compare_second[A, B: Eq](a: A, x: B, y: B) -> Bool {
    x == y
}

main() {
    a := 42
    x := Foo.Thing(x = 1)
    y := Foo.Thing(x = 2)

    // Should fail: Thing (B) doesn't satisfy Eq
    if compare_second(a, x, y) { 0 } else { 1 }
}
