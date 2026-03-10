#module main

main() {
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
