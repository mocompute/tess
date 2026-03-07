#module main

main() {
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
    outer := 0
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
        outer = outer + 1
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
