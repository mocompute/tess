// Test mutable variant bindings: s: Variant := val.& else { ... }
// and conditional: if s: Variant := val.& { ... }
#module Shape

Shape: | Circle   { radius: CInt }
       | Square   { length: CInt }
       | Triangle { b: CInt, h: CInt }

#module Opt

Opt: | MySome { val: CInt }
     | MyNone {}

#module main

// Basic mutable variant binding with let-else
test_mutable_let_else() {
    s: Shape.Shape := Shape.Circle(radius = 5)
    c: Circle := s.& else { return 1 }
    c->radius = 99
    when s {
        c2: Circle { if c2.radius != 99 { return 1 } }
        else { return 1 }
    }
    0
}

// Mutable variant binding: non-matching variant takes else path
test_mutable_let_else_bail() {
    s: Shape.Shape := Shape.Square(length = 4)
    c: Circle := s.& else { return 0 }
    1  // should not reach here
}

// Mutable variant binding with complex scrutinee (function call)
get_shape() {
    s: Shape.Shape := Shape.Circle(radius = 10)
    s
}

test_mutable_let_else_complex_scrutinee() {
    s := get_shape()
    c: Circle := s.& else { return 1 }
    c->radius = 42
    when s {
        c2: Circle { if c2.radius != 42 { return 1 } }
        else { return 1 }
    }
    0
}

// Conditional mutable variant binding (if form, no else)
test_mutable_conditional_no_else() {
    s: Shape.Shape := Shape.Circle(radius = 3)
    if c: Circle := s.& {
        c->radius = 77
    }
    when s {
        c2: Circle { if c2.radius != 77 { return 1 } }
        else { return 1 }
    }
    0
}

// Conditional mutable variant binding: non-matching (body skipped)
test_mutable_conditional_no_match() {
    s: Shape.Shape := Shape.Square(length = 4)
    if c: Circle := s.& {
        return 1  // should not enter
    }
    0
}

// Conditional mutable variant binding with else
test_mutable_conditional_with_else() {
    error := 0
    s: Shape.Shape := Shape.Triangle(b = 3, h = 4)
    if t: Triangle := s.& {
        t->b = 10
        t->h = 20
    }
    else {
        error += 1
    }
    when s {
        t2: Triangle {
            if t2.b != 10 { error += 1 }
            if t2.h != 20 { error += 1 }
        }
        else { error += 1 }
    }
    error
}

// Mutable variant binding in loop
test_mutable_in_loop() {
    error := 0
    s: Shape.Shape := Shape.Circle(radius = 0)
    i := 0
    while i < 5, i += 1 {
        c: Circle := s.& else { return 1 }
        c->radius += 1
    }
    when s {
        c2: Circle { if c2.radius != 5 { error += 1 } }
        else { error += 1 }
    }
    error
}

// Mutable let-else with else binding (requires two-variant union)
test_mutable_let_else_with_else_binding() {
    o: Opt.Opt := Opt.MyNone()
    s: MySome := o.& else n {
        return 0  // MyNone path — expected
    }
    1  // should not reach here
}

main() {
    error: CInt := 0
    error += test_mutable_let_else()
    error += test_mutable_let_else_bail()
    error += test_mutable_let_else_complex_scrutinee()
    error += test_mutable_conditional_no_else()
    error += test_mutable_conditional_no_match()
    error += test_mutable_conditional_with_else()
    error += test_mutable_in_loop()
    error += test_mutable_let_else_with_else_binding()
    error
}
