#module main
#import <Array.tl>

// Test continue statement in for loop
// Sum only values less than 5
main() {
    xs := Array.init(0, 10u)

    // Initialize array with values 0..9
    i := 0
    for x.& in xs {
        x.* = i
        i   = i + 1
    }

    // Sum values < 5, skip others
    sum := 0
    for x in xs {
        if x >= 5 {
            continue
        }
        sum = sum + x
    }
    // sum should be 0 + 1 + 2 + 3 + 4 = 10
    sum - 10
}
