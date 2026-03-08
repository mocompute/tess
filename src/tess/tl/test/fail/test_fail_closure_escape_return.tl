#module main

// Returning a capturing closure from a function is rejected because
// the closure context lives on the stack and would dangle.

outer(n: Int) -> (Int) -> Int {
    f := (x) { x + n }
    f
}

main() {
    g := outer(10)
    g(5)
    0
}
