// add() returns CInt instead of Self (Vec) — should fail trait satisfaction.
#module Vec

Vec: { x: CInt }

add(a: Vec, b: Vec) -> CInt { a.x + b.x }

#module main

double[T: Add](x: T) -> T { x + x }

main() {
    v := Vec.Vec(x = 3)
    double(v)
    0
}
