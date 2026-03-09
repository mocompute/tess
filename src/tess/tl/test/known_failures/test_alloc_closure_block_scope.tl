#module main
#import <Alloc.tl>

// Allocated closure created inside a block expression.
// Currently fails: codegen produces undeclared identifier for the closure
// function pointer when the alloc closure is inside a ({ ... }) block.

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
