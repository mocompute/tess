#module main

// Generics require exact type match: f(x: T, y: T) called with
// CUnsignedChar and CSize should fail — they are both narrow integers
// but in different subchains (2 vs 5), so T can't unify them.

f(x: T, y: T) -> T { x }

main() {
    a: CUnsignedChar := 1u
    b: CSize := 10zu
    f(a, b)
    0
}
