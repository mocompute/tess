#module main
#import <Alloc.tl>

// Basic allocated closure: create, call, verify capture by value.
// Uses Int annotation to avoid weak int defaulting complication.

main() {
    n: Int := 10
    f      :=[[alloc, capture(n)]](x) { x + n }

    error: CInt := 0
    error += if f(5) == 15 { 0 } else { 1 }
    error += if f(32) == 42 { 0 } else { 1 }

    // capture is by value: mutating n doesn't affect f
    n = 99
    error += if f(5) == 15 { 0 } else { 1 }
    error
}
