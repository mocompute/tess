#module Pair

Pair: { first: Int, second: Int }

eq(a: Pair, b: Pair) -> Bool {
    a.first == b.first && a.second == b.second
}


#module main

// Type with only eq (no cmp) -- can use == and != but not < > <= >=

main() {
    error := 0

    a := Pair.Pair(first = 1, second = 2)
    b := Pair.Pair(first = 1, second = 2)
    c := Pair.Pair(first = 3, second = 4)

    // == works
    error += if a == b {0} else {1}
    error += if a == c {1} else {0}

    // != works (derived from eq)
    error += if a != c {0} else {1}
    error += if a != b {1} else {0}

    // Self-equality
    error += if a == a {0} else {1}
    error += if a != a {1} else {0}

    error
}
