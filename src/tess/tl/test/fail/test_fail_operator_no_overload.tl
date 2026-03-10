// Using + on a type that has no add function should be rejected.

#module Foo

Thing: { x: Int }

#module main

main() {
    a := Foo.Thing(x = 1)
    b := Foo.Thing(x = 2)
    c := a + b
    c.x
}
