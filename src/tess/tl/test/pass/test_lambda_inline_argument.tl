#module main

// applies narrowing cast from Int to CInt
apply(f: (Int) -> Int, x: Int) -> CInt { ci: CInt := f(x) ci }

main() {
    apply((x) { x + 1 }, 5) - 6
}
