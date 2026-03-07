#module main

// Narrowing: CDouble value passed to CFloat parameter should fail.

take_cfloat(x: CFloat) -> CFloat { x }

main() {
    a: CDouble := 3.14
    take_cfloat(a)
    0
}
