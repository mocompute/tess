#module Shape

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | Rect   { h: Float, w: Float }
       | Empty  {}

#module main

// Basic: no-else, matching variant
test_match_no_else() {
    error             := 0
    s:    Shape.Shape := Shape.Circle(radius = 3)
    if c: Circle      := s {
        if c.radius != 3 { error += 1 }
        return error
    }
    1                                   // should not reach here
}

// Basic: no-else, non-matching variant (body is skipped)
test_no_match_no_else() {
    s:    Shape.Shape := Shape.Rect(h = 2, w = 3)
    if c: Circle      := s {
        return 1                        // should not enter
    }
    0
}

// With else, matching variant
test_match_with_else() {
    error              := 0
    s:     Shape.Shape := Shape.Square(length = 5)
    if sq: Square      := s {
        if sq.length != 5 { error += 1 }
    }
    else {
        error += 1
    }
    error
}

// With else, non-matching variant
test_no_match_with_else() {
    error              := 0
    s:     Shape.Shape := Shape.Circle(radius = 1)
    if sq: Square      := s {
        error += 1
    }
    else {
        return error                    // else taken
    }
    1                                   // should not reach here
}

// Access bound fields inside the if body
test_field_access() {
    error             := 0
    s:    Shape.Shape := Shape.Rect(h = 4, w = 6)
    if r: Rect        := s {
        if r.h != 4 { error += 1 }
        if r.w != 6 { error += 1 }
    }
    error
}

// Nullary variant (empty struct)
test_nullary_variant() {
    s:    Shape.Shape := Shape.Empty()
    if e: Empty       := s {
        return 0
    }
    1
}

// Multiple conditional bindings in sequence (chained single-variant checks)
test_sequential_checks() {
    error          := 0
    s: Shape.Shape := Shape.Square(length = 7)

    if c: Circle := s {
        return 1                        // not a circle
    }
    if r: Rect := s {
        return 1                        // not a rect
    }
    if sq: Square := s {
        if sq.length != 7 { error += 1 }
    }
    error
}

// Inside a loop
test_in_loop() {
    circle_count := 0
    i            := 0
    while i < 4, i += 1 {
        s: Shape.Shape := if i == 1 { Shape.Circle(radius = 1) }
        else { Shape.Square(length = 1) }
        if c: Circle := s {
            circle_count += 1
        }
    }
    if circle_count != 1 { return 1 }
    0
}

// Conditional variant binding as early return guard
test_early_return_guard() {
    s:    Shape.Shape := Shape.Rect(h = 10, w = 20)
    if c: Circle      := s {
        return 1                        // bail: not expected variant
    }
    if sq: Square := s {
        return 1                        // bail: not expected variant
    }
    if r: Rect := s {
        if r.h * r.w != 200 { return 1 }
    }
    0
}

// Binding name does not leak into outer scope (no-else case).
// After the if block, the same name can be rebound.
test_binding_scope() {
    error             := 0
    s:    Shape.Shape := Shape.Circle(radius = 9)
    if c: Circle      := s {
        if c.radius != 9 { error += 1 }
    }
    // 'c' is not in scope here; we can declare a fresh one
    c := 42
    if c != 42 { error += 1 }
    error
}

// With-else where both branches use return
test_both_branches_return() {
    s:    Shape.Shape := Shape.Rect(h = 1, w = 1)
    if c: Circle      := s {
        return 1
    }
    else {
        return 0
    }
}

main() {
    error: CInt := 0
    error += test_match_no_else()
    error += test_no_match_no_else()
    error += test_match_with_else()
    error += test_no_match_with_else()
    error += test_field_access()
    error += test_nullary_variant()
    error += test_sequential_checks()
    error += test_in_loop()
    error += test_early_return_guard()
    error += test_binding_scope()
    error += test_both_branches_return()
    error
}
