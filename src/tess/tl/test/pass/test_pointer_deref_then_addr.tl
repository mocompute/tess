#module main

// p.*.& (dereference then take address) should be valid syntax.
// Currently fails with syntax error: expected_star.

Shape: | Circle { radius: Int }
       | Square { length: Int }

main() {
    s := Circle(3)
    p := s.&

    // Dereference pointer, then take mutable reference for mutable when
    when p.*.& {
        c: Circle { c->radius = 99 }
        else { }
    }

    result := when s {
        c: Circle { c.radius }
        else { 0 }
    }
    ret: CInt := result - 99
    ret
}
