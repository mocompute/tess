// Test let-else inside a let-else's else block.

#module main

OptInt: | Val { v: Int } | Empty

eq(a: OptInt, b: OptInt) -> Bool {
    sa: Val := a else {
        nb: Empty := b else { return false }
        return true
    }
    sb: Val := b else { return false }
    sa.v == sb.v
}

main() {
    error := 0

    a := Val(10)
    b := Val(20)
    n := OptInt.Empty

    error += if eq(a, a) {0} else {1}
    error += if eq(a, b) {1} else {0}
    error += if eq(n, n) {0} else {1}
    error += if eq(a, n) {1} else {0}

    error
}
