#module main

main() {
    error := 0

    // True branch taken
    error += if true {0} else {1}

    // False branch taken
    error += if false {1} else {0}

    // else-if chain: first arm
    error += if true {0} else if true {1} else {1}

    // else-if chain: second arm
    error += if false {1} else if true {0} else {1}

    // else-if chain: else arm
    error += if false {1} else if false {1} else {0}

    // Nested if
    error += if true {
        if true {0} else {1}
    } else {1}

    // Nested if, inner false
    error += if true {
        if false {1} else {0}
    } else {1}

    // Condition with comparison
    x := 5
    error += if x > 3 {0} else {1}
    error += if x < 3 {1} else {0}
    error += if x == 5 {0} else {1}

    error
}
