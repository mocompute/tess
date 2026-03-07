#module Foo

Thing : { x: Int }

// Thing has NO eq function — does not satisfy Eq


#module main

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    a := Foo.Thing(x = 1)
    b := Foo.Thing(x = 2)

    // This should fail: Thing does not satisfy Eq
    if are_equal(a, b) { 0 } else { 1 }
}
