// Test that Ptr[TaggedUnion] is auto-dereferenced in when/case,
// so the user doesn't need to write p.* explicitly.

#module Shape
Shape: | Circle { r: CInt }
       | Square { s: CInt }
       | Empty

(): { }

#module main

#import <String.tl>

// Basic auto-deref: when p { ... } where p: Ptr[Shape]
area_when(p: Ptr[Shape.Shape]) -> CInt {
    when p {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty  { 0 }
    }
}

// Auto-deref with explicit type annotation: case p: Shape.Shape { ... }
area_case(p: Ptr[Shape.Shape]) -> CInt {
    case p: Shape.Shape {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty  { 0 }
    }
}

// Auto-deref through Ptr[Const[Shape]]
area_const(p: Ptr[Const[Shape.Shape]]) -> CInt {
    when p {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty  { 0 }
    }
}

// Two-variant union with else binding through Ptr
#module Answer
Answer: | Yes { val: CInt }
        | No  { reason: String }

(): { }

#module main

check_answer(p: Ptr[Answer.Answer]) -> CInt {
    when p {
        y: Yes { y.val }
        else {
            0 - 1
        }
    }
}

// Conditional variant binding through Ptr
is_circle(p: Ptr[Shape.Shape]) -> CInt {
    if c: Circle := p {
        c.r
    } else {
        0
    }
}

main() {
    error: CInt := 0

    c  := Shape.Circle(r = 7)
    sq := Shape.Square(s = 3)
    em := Shape.Empty()

    // Basic auto-deref when
    if area_when(c.&)  != 7 { error += 1 }
    if area_when(sq.&) != 3 { error += 1 }
    if area_when(em.&) != 0 { error += 1 }

    // Auto-deref case with annotation
    if area_case(c.&)  != 7 { error += 1 }
    if area_case(sq.&) != 3 { error += 1 }
    if area_case(em.&) != 0 { error += 1 }

    // Ptr[Const[Shape]]
    if area_const(c.&)  != 7 { error += 1 }
    if area_const(sq.&) != 3 { error += 1 }
    if area_const(em.&) != 0 { error += 1 }

    // Two-variant else binding
    y := Answer.Yes(val = 42)
    n := Answer.No(reason = "nope")
    if check_answer(y.&) != 42 { error += 1 }
    if check_answer(n.&) != 0 - 1 { error += 1 }

    // Conditional variant binding
    if is_circle(c.&)  != 7 { error += 1 }
    if is_circle(sq.&) != 0 { error += 1 }

    error
}
