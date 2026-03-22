// Test let-else with user-defined 3-variant tagged unions and complex else blocks.
#module Shape

Shape: | Circle   { r: CInt }
       | Square   { s: CInt }
       | Triangle { b: CInt, h: CInt }

#module main

test_let_else_3_variant() {
    error          := 0
    s: Shape.Shape := Shape.Circle(r = 5)
    c: Circle      := s else { return 1 }
    if c.r != 5 { error += 1 }
    error
}

test_let_else_bail_on_wrong_variant() {
    s: Shape.Shape := Shape.Square(s = 4)
    // Try to extract Circle from a Square — should bail
    c: Circle := s else { return 0 }
    // Should not reach here
    1
}

test_let_else_multiple_bindings_in_else() {
    s: Shape.Shape := Shape.Triangle(b = 6, h = 4)
    c: Circle      := s else {
        x := 1
        y := 2
        z := x + y
        return z - 3
    }
    // Should not reach here
    1
}

test_let_else_in_loop() {
    error        := 0
    circle_count := 0
    i            := 0
    while i < 3, i += 1 {
        s: Shape.Shape := if i == 0 { Shape.Circle(r = i) } else { Shape.Square(s = i) }
        c: Circle      := s else { continue }
        circle_count += 1
    }
    if circle_count != 1 { error += 1 }
    error
}

main() {
    error: CInt := 0
    error += test_let_else_3_variant()
    error += test_let_else_bail_on_wrong_variant()
    error += test_let_else_multiple_bindings_in_else()
    error += test_let_else_in_loop()
    error
}
