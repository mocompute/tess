// Test: auto-address-of for regular function calls with Ptr[Const[T]] parameters.
// When a function expects Ptr[Const[T]] and the callsite provides T, the compiler
// should automatically take the address.

#module Point

Point: { x: Int, y: Int }

// Function taking Ptr[Const[Point]] — should accept Point values automatically.
sum(p: Ptr[Const[Point]]) -> Int {
    p->x + p->y
}

// Mixed parameters: only Ptr[Const[T]] gets auto-addressed, plain T stays as-is.
add_scaled(p: Ptr[Const[Point]], scale: Int) -> Int {
    (p->x + p->y) * scale
}

// Two Ptr[Const[T]] parameters.
dot(a: Ptr[Const[Point]], b: Ptr[Const[Point]]) -> Int {
    a->x * b->x + a->y * b->y
}

// UFCS self (Ptr[Point]) + auto-address-of on second arg (Ptr[Const[Point]]).
distance_sq(self: Ptr[Point], other: Ptr[Const[Point]]) -> Int {
    dx := self->x - other->x
    dy := self->y - other->y
    dx * dx + dy * dy
}

// Already-pointer argument should not be double-wrapped.
sum_ptr(p: Ptr[Const[Point]]) -> Int {
    sum(p)
}

#module main

// Generic function with Ptr[Const[T]] parameter.
identity_via_ptr[T](p: Ptr[Const[T]]) -> T { p.* }

main() {
    p1 := Point(x = 3, y = 4)
    p2 := Point(x = 1, y = 2)

    r := 0

    // Basic: value passed to Ptr[Const[Point]] parameter
    if Point.sum(p1) != 7 { r = r + 1 }

    // Mixed: Ptr[Const[Point]] + plain Int
    if Point.add_scaled(p1, 2) != 14 { r = r + 1 }

    // Two Ptr[Const[Point]] parameters
    if Point.dot(p1, p2) != 11 { r = r + 1 }

    // Already a pointer — should not double-wrap
    if Point.sum_ptr(p1.&) != 7 { r = r + 1 }

    // UFCS on self + auto-address-of on second argument
    if p1.distance_sq(p2) != 8 { r = r + 1 }

    // Generic function with Ptr[Const[T]]
    v := 42
    if identity_via_ptr(v) != 42 { r = r + 1 }

    r
}
