#module main

// Test break statement in for loop
// Loop should exit early when we find a value >= 5
main() {
    xs: Ptr[Int] := c_malloc(10u * sizeof[Int]())

    // Initialize array with values 0..9
    i := 0
    while i < 10, i += 1 {
        xs.[i] = i
    }

    // Find first value >= 5 and exit
    found := 0

    i := 0
    x := -1
    while i < 10, i += 1 {
        x = xs.[i]
        if x >= 5 {
            found = x
            break
        }
    }

    c_free(xs)

    // found should be 5
    found - 5
}
