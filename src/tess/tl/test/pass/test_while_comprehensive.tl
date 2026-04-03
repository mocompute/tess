#module main

test_statement() {
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
    a   := 0
    while a < 3 {
        b := 0
        while b < 3 {
            sum = sum + 1
            b   = b + 1
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

test_break() {
    error := 0

    // Basic break: exit loop early
    i := 0
    while i < 10 {
        if i == 5 {
            break
        }
        i = i + 1
    }
    error += if i == 5 {0} else {1}

    // Conditional break at different point
    j := 0
    while j < 100 {
        j = j + 1
        if j == 3 {
            break
        }
    }
    error += if j == 3 {0} else {1}

    // Break in nested loop: only inner loop breaks
    outer       := 0
    inner_total := 0
    while outer < 3 {
        k := 0
        while k < 10 {
            if k == 2 {
                break
            }
            k = k + 1
        }
        inner_total = inner_total + k
        outer       = outer + 1
    }
    error += if outer == 3 {0} else {1}
    error += if inner_total == 6 {0} else {1}

    // Break on first iteration
    count := 0
    while true {
        break
        count = 99
    }
    error += if count == 0 {0} else {1}

    error
}

test_continue() {
    error := 0

    // Sum only even numbers from 1 to 10
    i   := 0
    sum := 0
    while i < 10 {
        i = i + 1
        if i % 2 == 1 {
            continue
        }
        sum = sum + i
    }
    error += if sum == 30 {0} else {1}

    // Skip multiples of 3
    j    := 0
    sum2 := 0
    while j < 10 {
        j = j + 1
        if j % 3 == 0 {
            continue
        }
        sum2 = sum2 + j
    }
    // 1+2+4+5+7+8+10 = 37
    error += if sum2 == 37 {0} else {1}

    // Continue with counter verification: all iterations run
    k     := 0
    count := 0
    while k < 5 {
        k     = k + 1
        count = count + 1
        if k == 3 {
            continue
        }
    }
    error += if count == 5 {0} else {1}
    error += if k == 5 {0} else {1}

    // Continue in nested loop
    outer_sum := 0
    a         := 0
    while a < 3 {
        a = a + 1
        b         := 0
        inner_sum := 0
        while b < 4 {
            b = b + 1
            if b == 2 {
                continue
            }
            inner_sum = inner_sum + b
        }
        // 1+3+4 = 8 each iteration
        outer_sum = outer_sum + inner_sum
    }
    error += if outer_sum == 24 {0} else {1}

    error
}

test_empty_body() {
    count := 1
    while count < 1024, count *= 2 {}
    count - 1024
}

test_update_statement() {
    i := 0
    while i < 10, i = i + 1 {
        0
    }
    i - 10
}

main() {
    error := 0
    error += test_statement()
    error += test_break()
    error += test_continue()
    error += test_empty_body()
    error += test_update_statement()
    error
}
