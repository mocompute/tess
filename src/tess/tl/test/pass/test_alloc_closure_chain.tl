#module main
#import <Alloc.tl>

// Chain of allocated closures: each captures the result of calling
// the previous one, building up accumulated state.

make_base(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

add_layer(base: (Int) -> Int, extra: Int) -> (Int) -> Int {
    // Evaluate base at a fixed point and capture the accumulated offset
    offset := base(0) + extra
    f := [[alloc, capture(offset)]] (x) { x + offset }
    f
}

main() {
    error := 0

    base := make_base(10)       // base(x) = x + 10
    l1   := add_layer(base, 5)  // offset=15, l1(x) = x + 15
    l2   := add_layer(l1, 3)    // offset=18, l2(x) = x + 18
    l3   := add_layer(l2, 2)    // offset=20, l3(x) = x + 20

    error += if base(0) == 10 { 0 } else { 1 }
    error += if l1(0)   == 15 { 0 } else { 1 }
    error += if l2(0)   == 18 { 0 } else { 1 }
    error += if l3(0)   == 20 { 0 } else { 1 }
    error += if l3(100) == 120 { 0 } else { 1 }

    error
}
