#module main

accept_uint(x: UInt) -> UInt { x }

main() {
    x: CSize := 42zu
    accept_uint(x)
    0
}
