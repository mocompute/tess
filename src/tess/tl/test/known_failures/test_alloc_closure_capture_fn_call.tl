#module main
#import <Alloc.tl>

// Capturing a function-typed value and calling it inside the closure body.
// Currently fails: FV analysis does not count application position as a use,
// so the compiler reports capture_unused_variable for `base`.

make_base(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

add_layer(base: (Int) -> Int, extra: Int) -> (Int) -> Int {
    f := [[alloc, capture(base, extra)]] (x) { base(x) + extra }
    f
}

main() {
    base := make_base(10)
    l1   := add_layer(base, 5)
    error: CInt := 0
    error += if l1(0) == 15 { 0 } else { 1 }
    error
}
