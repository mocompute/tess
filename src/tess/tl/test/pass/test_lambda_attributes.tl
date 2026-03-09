#module main

main() {
    error := 0

    // single attribute on lambda
    f := [[my_attr]] (x) { x + 1 }
    error += if f :: [[my_attr]] {0} else {1}
    error += if f :: [[bad]] {1} else {0}

    // multiple attributes on lambda
    g := [[attr1, attr2]] (x) { x * 2 }
    error += if g :: [[attr1]] {0} else {1}
    error += if g :: [[attr2]] {0} else {1}
    error += if g :: [[bad]] {1} else {0}

    // parameterized attribute on lambda
    n := 10
    h := [[alloc, capture(n)]] (x) { x + n }
    error += if h :: [[alloc]] {0} else {1}
    error += if h :: [[capture(n)]] {0} else {1}
    error += if h :: [[capture]] {0} else {1}

    // lambda attribute combined with parameter attribute
    k := [[my_attr]] ([[param_attr]] x) { x :: [[param_attr]] }
    error += if k :: [[my_attr]] {0} else {1}
    error += if k(42) {0} else {1}

    result: CInt := error
    result
}
