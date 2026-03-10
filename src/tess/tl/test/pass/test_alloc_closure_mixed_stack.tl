#module main
#import <Alloc.tl>

// Mixing stack closures and allocated closures in the same function.
// Stack closures capture by reference, allocated closures by value.
// Verifies they don't interfere with each other.

apply(f: (Int) -> Int, x: Int) -> Int { f(x) }

main() {
    error := 0

    // Stack closure captures by reference
    n       := 10
    stack_f := (x) { x + n }
    error += if stack_f(5) == 15 { 0 } else { 1 }

    // Allocated closure captures by value
    alloc_f := [[alloc, capture(n)]] (x) { x + n }
    error += if alloc_f(5) == 15 { 0 } else { 1 }

    // Mutate n — stack closure sees it, allocated doesn't
    n = 99
    error += if stack_f(1) == 100 { 0 } else { 1 } // by ref: sees 99
    error += if alloc_f(1) == 11 { 0 } else { 1 }  // by val: still 10

    // Both can be passed to the same HOF
    error += if apply(stack_f, 1) == 100 { 0 } else { 1 }
    error += if apply(alloc_f, 1) == 11 { 0 } else { 1 }

    error
}
