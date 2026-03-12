#module main

accept_int(x: Int) -> Int { x }

main() {
    x: CPtrDiff := 42z
    accept_int(x)
    0
}
