#module main
#import <Alloc.tl>

// Stress test: deeply nested function calls each returning an allocated closure.
// Each level captures the result of calling the previous level's closure.

level0(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

level1(n: Int) -> (Int) -> Int {
    f := level0(n)
    val := f(n)
    // val = n + n = 2n
    g := [[alloc, capture(val)]] (x) { x + val }
    g
}

level2(n: Int) -> (Int) -> Int {
    f := level1(n)
    val := f(n)
    // val = n + 2n = 3n
    g := [[alloc, capture(val)]] (x) { x + val }
    g
}

level3(n: Int) -> (Int) -> Int {
    f := level2(n)
    val := f(n)
    // val = n + 3n = 4n
    g := [[alloc, capture(val)]] (x) { x + val }
    g
}

main() {
    error := 0
    f := level3(10)
    // level3(10): val = 10 + 30 = 40, so f(0) = 40
    error += if f(0) == 40 { 0 } else { 1 }
    // f(5) = 5 + 40 = 45
    error += if f(5) == 45 { 0 } else { 1 }
    error
}
