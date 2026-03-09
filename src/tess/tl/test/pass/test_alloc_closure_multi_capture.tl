#module main
#import <Alloc.tl>

// Capture multiple variables of different types (Int, Bool).

compute(n: Int, flag: Bool) -> (Int) -> Int {
    f := [[alloc, capture(n, flag)]] (x) {
        if flag { x + n } else { x - n }
    }
    f
}

main() {
    add := compute(10, true)
    sub := compute(10, false)

    error := 0
    error += if add(5) == 15 { 0 } else { 1 }
    error += if sub(5) == -5 { 0 } else { 1 }

    // capture is by value: mutating originals doesn't affect closures
    error += if add(0) == 10 { 0 } else { 1 }
    error += if sub(0) == -10 { 0 } else { 1 }
    error
}
