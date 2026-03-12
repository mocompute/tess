#module main

accept_csize(x: CSize) -> CSize { x }

main() {
    x: UInt := 42u
    accept_csize(x)
    0
}
