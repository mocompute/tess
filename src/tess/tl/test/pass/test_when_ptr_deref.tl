// Test that a pointer to a tagged union can be used as a `when` or `case`
// scrutinee by dereferencing with .* (non-mutable access).

#module Shape
Shape: | Circle { r: CInt }
       | Square { s: CInt }
       | Empty

(): { }

#module main

area_when(p: Ptr[Shape.Shape]) -> CInt {
    when p.* {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty  { 0 }
    }
}

area_case(p: Ptr[Shape.Shape]) -> CInt {
    case p.*: Shape.Shape {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty  { 0 }
    }
}

main() {
    error: CInt := 0

    c  := Shape.Circle(r = 7)
    sq := Shape.Square(s = 3)
    em := Shape.Empty()

    if area_when(c.&)  != 7 { error += 1 }
    if area_when(sq.&) != 3 { error += 1 }
    if area_when(em.&) != 0 { error += 1 }

    if area_case(c.&)  != 7 { error += 1 }
    if area_case(sq.&) != 3 { error += 1 }
    if area_case(em.&) != 0 { error += 1 }

    error
}
