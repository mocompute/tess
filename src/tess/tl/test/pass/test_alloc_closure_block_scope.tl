#module main
#import <Alloc.tl>

// Allocated closure created inside a block expression.

main() {
    error: CInt := 0
    ({
        n := 100
        f := [[alloc, capture(n)]] (x) { x + n }
        error += if f(5) == 105 { 0 } else { 1 }
        void
    })
    error
}
