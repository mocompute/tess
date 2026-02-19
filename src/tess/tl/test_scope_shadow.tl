#module main

main() {
    error := 0

    // Basic shadow: inner scope does not affect outer
    val := 1
    if true {
        val := 2
    }
    error += if val == 1 {0} else {1}

    // Two-level nesting
    x := 10
    if true {
        x := 20
        if true {
            x := 30
        }
        error += if x == 20 {0} else {1}
    }
    error += if x == 10 {0} else {1}

    // Shadow in while body
    y := 5
    i := 0
    while i < 3 {
        y := i
        i = i + 1
    }
    error += if y == 5 {0} else {1}

    // Shadow with different types (both Int, different values)
    z := 100
    if true {
        z := 0
        error += if z == 0 {0} else {1}
    }
    error += if z == 100 {0} else {1}

    // Shadow does not leak across sequential blocks
    a := 1
    if true {
        a := 2
    }
    if true {
        a := 3
    }
    error += if a == 1 {0} else {1}

    error
}
