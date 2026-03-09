#module main
#import <Alloc.tl>

// Returning an allocated closure through a symbol alias should be allowed.
// The escape checker must follow alias chains to see that g is backed by
// an [[alloc]] lambda.

make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    g := f
    g
}

main() {
    add10 := make_adder(10)
    error := 0
    error += if add10(5) == 15 { 0 } else { 1 }
    error += if add10(0) == 10 { 0 } else { 1 }
    error
}
