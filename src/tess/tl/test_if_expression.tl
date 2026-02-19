#module main

main() {
    error := 0

    // Basic if-expression, true branch
    x := 42
    error += if x == 42 {0} else {1}

    // Basic if-expression, false branch
    error += if x == 99 {1} else {0}

    // If-expression assigned to variable
    r1 := if x > 0 {10} else {20}
    error += if r1 == 10 {0} else {1}

    r2 := if x < 0 {10} else {20}
    error += if r2 == 20 {0} else {1}

    // Chained else-if expression
    y := 2
    r3 := if y == 1 {10} else if y == 2 {20} else {30}
    error += if r3 == 20 {0} else {1}

    // Chained else-if, falls to else
    r4 := if y == 10 {10} else if y == 20 {20} else {30}
    error += if r4 == 30 {0} else {1}

    // Nested if-expression
    r5 := if true { if false {1} else {0} } else {1}
    error += r5

    // If-expression in arithmetic
    r6 := 10 + if true {5} else {100}
    error += if r6 == 15 {0} else {1}

    error
}
