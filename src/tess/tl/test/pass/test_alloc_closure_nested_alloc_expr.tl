#module main
#import <Alloc.tl>

// Nested allocated closures where the inner one uses its own alloc_expr.
// The outer closure's free-variable collection must skip the inner closure's
// alloc_expr (via skip_alloc_expr), otherwise the allocator variable leaks
// into the outer capture set and causes a spurious capture_unlisted_var error.
//
// Here the inner [[alloc]] uses the default allocator (Alloc.context.default),
// so no additional variable is needed in the outer capture list.

apply(f: (Int) -> Int, x: Int) -> Int {
    f(x)
}

outer(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) {
        // The inner closure uses [[alloc]] (default allocator).
        // The default allocator reference must NOT appear as a free variable
        // of the outer closure.
        inner := [[alloc, capture(x, n)]] (y) { x + y + n }
        apply(inner, x)
    }
    f
}

main() {
    add   := outer(10)
    error := 0
    error += if add(5) == 20 { 0 } else { 1 }
    error += if add(0) == 10 { 0 } else { 1 }
    error
}
