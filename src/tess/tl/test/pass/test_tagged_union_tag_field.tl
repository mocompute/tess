// Exercises the `.tag` field on tagged unions. The tag identifies which
// variant a value holds and enables fast-reject discriminant checks in
// custom equality (see docs/LANGUAGE_REFERENCE.md § Tag Field).

#module TU_TagShape
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | Empty

eq(a: Shape, b: Shape) -> Bool {
    if a.tag != b.tag { return false }
    when a {
        ca: Circle {
            cb: Circle := b else { return false }
            ca.radius == cb.radius
        }
        sa: Square {
            sb: Square := b else { return false }
            sa.length == sb.length
        }
        _: Empty { true }
    }
}

#module main

main() -> CInt {
    errors: CInt := 0

    a := TU_TagShape.Circle(2.0)
    b := TU_TagShape.Square(3.0)
    c := TU_TagShape.Circle(5.0)
    d := TU_TagShape.Circle(2.0)
    e: TU_TagShape.Shape := TU_TagShape.Empty

    // Different variants -> unequal tags.
    errors += if a.tag == b.tag { 1 } else { 0 }
    errors += if a.tag != b.tag { 0 } else { 1 }

    // Same variant, different payload -> equal tags.
    errors += if a.tag == c.tag { 0 } else { 1 }

    // Same variant, same payload -> equal tags.
    errors += if a.tag == d.tag { 0 } else { 1 }

    // Nullary variant.
    errors += if e.tag == a.tag { 1 } else { 0 }
    errors += if e.tag == e.tag { 0 } else { 1 }

    // Fast-reject custom eq: different variants -> false (tag check short-circuits).
    errors += if TU_TagShape.eq(a, b) { 1 } else { 0 }

    // Same variant, different payload -> false (when-arm compares fields).
    errors += if TU_TagShape.eq(a, c) { 1 } else { 0 }

    // Same variant, same payload -> true.
    errors += if TU_TagShape.eq(a, d) { 0 } else { 1 }

    // Nullary variant equality.
    errors += if TU_TagShape.eq(e, e) { 0 } else { 1 }

    errors
}
