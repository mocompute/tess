// A typo in a trait bound should be a compile error, not silently accepted.
// `Eqq` does not exist — the compiler must reject this.

#module main

f[T: Eqq](x: T) -> T {
    x
}

main() -> CInt {
    f(1)
    0
}
