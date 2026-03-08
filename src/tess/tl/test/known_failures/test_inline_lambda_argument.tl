#module main

apply(f: (Int) -> Int, x: Int) -> Int { f(x) }

main() {
    apply((x) { x + 1 }, 5) - 6
}
