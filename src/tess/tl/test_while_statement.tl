#module main

main() {
    error := 0

    // Basic while: count to 10
    i := 0
    while i < 10 {
        i = i + 1
    }
    error += if i == 10 {0} else {1}

    // While with different condition: count down
    j := 10
    while j > 0 {
        j = j - 1
    }
    error += if j == 0 {0} else {1}

    // While that never executes (false condition)
    k := 99
    while false {
        k = 0
    }
    error += if k == 99 {0} else {1}

    // Nested while loops
    sum := 0
    a := 0
    while a < 3 {
        b := 0
        while b < 3 {
            sum = sum + 1
            b = b + 1
        }
        a = a + 1
    }
    error += if sum == 9 {0} else {1}

    // While with compound condition
    m := 0
    n := 100
    while m < 5 && n > 50 {
        m = m + 1
        n = n - 10
    }
    error += if m == 5 {0} else {1}
    error += if n == 50 {0} else {1}

    error
}
