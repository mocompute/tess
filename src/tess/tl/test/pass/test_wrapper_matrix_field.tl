// Wrapper-matrix coverage for struct field access.
// `.` peels one Const layer; `->` peels one Ptr layer. Ptr[Const[T]]
// is reached via `->` (transparently skipping inner Const).

#module Point

Point: { x: CInt, y: CInt }

#module main

// T: plain value.
sum_value(p: Point) -> CInt { p.x + p.y }

// Const[T]: Const peels via `.`.
sum_const(p: Const[Point]) -> CInt { p.x + p.y }

// Ptr[T]: Ptr peels via `->`.
sum_ptr(p: Ptr[Point]) -> CInt { p->x + p->y }

// Ptr[Const[T]]: Ptr + inner Const via `->`.
sum_ptr_const(p: Ptr[Const[Point]]) -> CInt { p->x + p->y }

main() {
    p                      := Point(x = 10, y = 20)
    cp:  Const[Point]      := Point(x = 100, y = 200)
    cpp: Const[Ptr[Point]] := p.&

    error: CInt := 0

    // T
    if sum_value(p) != 30 { error += 1 }

    // Const[T]
    if sum_const(cp) != 300 { error += 1 }

    // Ptr[T]
    if sum_ptr(p.&) != 30 { error += 1 }

    // Ptr[Const[T]]
    if sum_ptr_const(cp.&) != 300 { error += 1 }
    if sum_ptr_const(p.&) != 30 { error += 1 }

    // Const[Ptr[T]]: const binding of a pointer, used as a Ptr.
    if sum_ptr(cpp) != 30 { error += 1 }

    error
}
