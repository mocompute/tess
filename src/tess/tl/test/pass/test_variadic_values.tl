#module main
#import <Hash.tl>

sum_hashes(args: ...Hash) -> CSize {
    total: CSize := 0
    i:     CSize := 0
    while i < args.size, i = i + 1 {
        total = total + args.v.[i]
    }
    total
}

main() {
    // hash of a single value: verify we can access the actual values
    h1 := sum_hashes(42)
    if h1 == 0 { c_exit(1) }            // hash of 42 should be non-zero

    // two values: sum should be different from each individual
    h2 := sum_hashes(42, 100)
    if h2 == h1 { c_exit(2) }           // adding more values should change the sum

    0
}
