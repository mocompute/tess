// Fail test: generic function with trait bound T: Eq called with a type
// that does NOT implement Eq. The compiler must reject this.

#module NoEq

// Widget has no eq function -- does not satisfy Eq
Widget : { id: Int, name: Int }

// Only has add, no eq
add(a: Widget, b: Widget) -> Widget { Widget(id = a.id + b.id, name = a.name) }


#module main

// Combined trait requiring both Add and Eq
AddEq[T] : Add[T], Eq[T] { }

// This function requires Eq
check_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

// This function requires the combined AddEq trait
add_if_new[T: AddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

main() {
    a := NoEq.Widget(id = 1, name = 10)
    b := NoEq.Widget(id = 2, name = 20)

    // Should fail: Widget does not satisfy Eq
    if check_equal(a, b) { 0 } else { 1 }
}
