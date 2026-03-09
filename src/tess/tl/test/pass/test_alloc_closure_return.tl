#module main
#import <Alloc.tl>

// Returning an allocated closure from a function should be allowed
// because the context is heap-allocated and won't dangle.

outer(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

main() {
    g := outer(10)
    error := 0
    error += if g(5) == 15 { 0 } else { 1 }
    error
}
