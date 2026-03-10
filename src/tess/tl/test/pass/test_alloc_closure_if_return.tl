#module main
#import <Alloc.tl>

// Returning an allocated closure from an if/else expression.
// Currently fails: escape checker does not trace alloc closures through
// conditional branches — reports closure_escape.

pick(use_add: Bool, n: Int) -> (Int) -> Int {
    if use_add {
        f := [[alloc, capture(n)]] (x) { x + n }
        f
    } else {
        f := [[alloc, capture(n)]] (x) { x * n }
        f
    }
}

main() {
    add5 := pick(true, 5)
    mul5 := pick(false, 5)
    error: CInt := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error += if mul5(10) == 50 { 0 } else { 1 }
    error
}
