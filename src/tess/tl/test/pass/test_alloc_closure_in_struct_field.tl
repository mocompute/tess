#module main
#import <Alloc.tl>

// Stress test: structs containing multiple closure fields,
// built from allocated closures created in different scopes.

Transform: { forward: (Int) -> Int, backward: (Int) -> Int }

make_transform(offset: Int) -> Transform {
    fwd := [[alloc, capture(offset)]] (x) { x + offset }
    bwd := [[alloc, capture(offset)]] (x) { x - offset }
    Transform(forward = fwd, backward = bwd)
}

round_trip(t: Transform, x: Int) -> Int {
    t.backward(t.forward(x))
}

main() {
    error := 0

    t := make_transform(42)
    error += if t.forward(10) == 52 { 0 } else { 1 }
    error += if t.backward(52) == 10 { 0 } else { 1 }

    // Round-trip should be identity
    error += if round_trip(t, 100) == 100 { 0 } else { 1 }
    error += if round_trip(t, 0) == 0 { 0 } else { 1 }

    // Multiple transforms are independent
    t2 := make_transform(7)
    error += if t.forward(0) == 42 { 0 } else { 1 }
    error += if t2.forward(0) == 7 { 0 } else { 1 }

    error
}
