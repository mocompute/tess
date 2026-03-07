#module main

main() {
    error := 0

    // Test basic logical AND
    error += if (true && true) {0} else {1}
    error += if (true && false) {1} else {0}
    error += if (false && true) {1} else {0}
    error += if (false && false) {1} else {0}

    // Test short-circuit evaluation
    // If the first operand is false, the second should NOT be evaluated
    x      := 0
    result := false && ( {x = 1 true})
    // x should still be 0 if short-circuit worked
    error += if (0 == x) {0} else {1}

    // Test with variables
    a := true
    b := false
    error += if (a && a) {0} else {1}
    error += if (a && b) {1} else {0}
    error += if (b && a) {1} else {0}

    // Test combined with || and !
    error += if (true && true || false) {0} else {1}
    error += if (false || true && true) {0} else {1}
    error += if (!false && true) {0} else {1}
    error += if (true && !false) {0} else {1}

    // Test with comparison operators
    error += if (1 < 2 && 2 < 3) {0} else {1}
    error += if (1 == 1 && 2 == 2) {0} else {1}
    error += if (1 > 2 && 2 > 1) {1} else {0}

    // Test nested AND
    error += if (true && true && true) {0} else {1}
    error += if (true && true && false) {1} else {0}

    error
}
