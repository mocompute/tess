// Wrapper-matrix coverage for tagged-union case/when scrutinees.
// Exercises all 5 shapes from the Ptr/Const wrapper matrix:
//   T, Const[T], Ptr[T], Ptr[Const[T]], Const[Ptr[T]]
// against two modes:
//   - read-only: `when sh { ... }`       (value-typed variant bindings)
//   - mutable:   `when sh.& { ... }`     (pointer-typed variant bindings)
// Each combination must dispatch to the correct variant branch via
// effective_target.

#module Shape
Shape: | Circle { r: CInt }
       | Square { s: CInt }
       | Empty


#module main

// Bare value scrutinee.
area_value(sh: Shape) -> CInt {
    when sh {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty { 0 }
    }
}

// Const[T] scrutinee.
area_const(sh: Const[Shape]) -> CInt {
    when sh {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty { 0 }
    }
}

// Ptr[T] scrutinee (auto-deref).
area_ptr(p: Ptr[Shape]) -> CInt {
    when p {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty { 0 }
    }
}

// Ptr[Const[T]] scrutinee (auto-deref + inner Const).
area_ptr_const(p: Ptr[Const[Shape]]) -> CInt {
    when p {
        c: Circle { c.r }
        s: Square { s.s }
        e: Empty { 0 }
    }
}

// --- Mutable scrutinees via `.&`: variant bindings are pointer-typed,
// so branches may mutate through `->`. ---

// T.& -> Ptr[T] scrutinee.
bump_value(sh: Ptr[Shape]) {
    when sh.& {
        c: Circle { c->r += 1 }
        s: Square { s->s += 1 }
        e: Empty { }
    }
}

// Ptr[T].& -> Ptr[Ptr[T]] scrutinee (double auto-deref).
bump_ptr(p: Ptr[Shape]) {
    when p.& {
        c: Circle { c->r += 1 }
        s: Square { s->s += 1 }
        e: Empty { }
    }
}

main() {
    c  := Shape.Circle(r = 7)
    sq := Shape.Square(s = 3)
    em := Shape.Empty()

    cc: Const[Shape] := Shape.Circle(r = 11)
    // Const[Ptr[T]]: a Const binding that holds a pointer value.
    cpp: Const[Ptr[Shape]] := c.&

    error: CInt := 0

    // T
    if area_value(c) != 7 { error += 1 }
    if area_value(sq) != 3 { error += 1 }
    if area_value(em) != 0 { error += 1 }

    // Const[T]
    if area_const(cc) != 11 { error += 1 }

    // Ptr[T]
    if area_ptr(c.&) != 7 { error += 1 }
    if area_ptr(sq.&) != 3 { error += 1 }

    // Ptr[Const[T]]
    if area_ptr_const(cc.&) != 11 { error += 1 }
    if area_ptr_const(c.&) != 7 { error += 1 }

    // Const[Ptr[T]]: passing the value of a const pointer to a Ptr parameter.
    if area_ptr(cpp) != 7 { error += 1 }

    // --- Mutable-scrutinee mode: mutation propagates back to the binding. ---

    // T.& on each variant.
    m_c := Shape.Circle(r = 7)
    bump_value(m_c.&)
    if area_value(m_c) != 8 { error += 1 }

    m_sq := Shape.Square(s = 3)
    bump_value(m_sq.&)
    if area_value(m_sq) != 4 { error += 1 }

    m_em := Shape.Empty()
    bump_value(m_em.&)                  // no-op branch
    if area_value(m_em) != 0 { error += 1 }

    // Ptr[T].& — double-level scrutinee.
    m_c2: Shape := Shape.Circle(r = 20)
    bump_ptr(m_c2.&)
    if area_value(m_c2) != 21 { error += 1 }

    error
}
