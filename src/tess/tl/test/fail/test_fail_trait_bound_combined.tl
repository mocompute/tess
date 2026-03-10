#module Foo

Thing: { val: Int }

// Thing only has add but not eq -- doesn't satisfy a combined AddEq trait
add(a: Thing, b: Thing) -> Thing { Thing(val = a.val + b.val) }


#module main

// Combined trait requiring both Add and Eq
AddEq[T]: Add[T], Eq[T] { }

process[T: AddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

main() {
    a := Foo.Thing(val = 1)
    b := Foo.Thing(val = 2)

    // Should fail: Thing has add but not eq
    r := process(a, b)
    r.val
}
