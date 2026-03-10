// Test that == and != can fall back to cmp when eq is not defined.
// The design says: "For == and !=: the compiler first checks for eq. If absent,
// falls back to deriving from cmp (i.e., cmp(a, b) == 0)."

#module Val

Value: { n: Int }

// Only cmp — no eq function
cmp(a: Value, b: Value) -> CInt {
    if a.n < b.n { -1 }
    else if a.n > b.n { 1 }
    else { 0 }
}

#module main

main() {
    error := 0

    a := Val.Value(n = 5)
    b := Val.Value(n = 5)
    c := Val.Value(n = 10)

    // == should derive from cmp(a, b) == 0
    error += if a == b {0} else {1}
    error += if a == c {1} else {0}

    // != should derive from cmp(a, b) != 0
    error += if a != c {0} else {1}
    error += if a != b {1} else {0}

    // < should still work
    error += if a < c {0} else {1}

    error
}
