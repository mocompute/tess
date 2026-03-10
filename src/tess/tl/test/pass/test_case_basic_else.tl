#module main

main() {
    error := 0

    // Match first arm
    r1 := case 0 {
        0 { 10 }
        1 { 20 }
        else { 99 }
    }
    error += if r1 == 10 {0} else {1}

    // Match second arm
    r2 := case 1 {
        0 { 10 }
        1 { 20 }
        else { 99 }
    }
    error += if r2 == 20 {0} else {1}

    // Fall through to else
    r3 := case 12 {
        0 { 1 }
        1 { 1 }
        2 { 2 }
        3 { 3 }
        else { 0 }
    }
    error += r3

    // Case with variable
    n  := 5
    r4 := case n {
        4 { 10 }
        5 { 20 }
        6 { 30 }
        else { 99 }
    }
    error += if r4 == 20 {0} else {1}

    // Multiple arms, matching last before else
    r5 := case 3 {
        1 { 10 }
        2 { 20 }
        3 { 30 }
        else { 99 }
    }
    error += if r5 == 30 {0} else {1}

    // All arms miss, else taken
    r6 := case 100 {
        1 { 10 }
        2 { 20 }
        3 { 30 }
        else { 42 }
    }
    error += if r6 == 42 {0} else {1}

    error
}
