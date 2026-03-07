// Float should NOT satisfy Mod — C '%' is undefined for floating-point types.

#module main

remainder[T: Mod](a: T, b: T) -> T {
    a % b
}

main() {
    // This should fail: Float does not satisfy Mod
    remainder(3.0, 2.0)
}
