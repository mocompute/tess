// Unsuffixed integer literal (weak signed) should unify with unsigned types in update expressions.
#module main

main() {
    error := 0

    // While update: UInt += unsuffixed literal
    i: UInt := 0u
    while i < 10u, i += 1 {
    }
    error += if i == 10u { 0 } else { 1 }

    // While update: CSize += unsuffixed literal
    k: CSize := 0zu
    while k < 10zu, k += 1 {
    }
    error += if k == 10zu { 0 } else { 1 }

    // Assignment add: UInt += unsuffixed literal
    j: UInt := 5u
    j += 3
    error += if j == 8u { 0 } else { 1 }

    error
}
