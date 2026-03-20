// A variadic trait must have exactly one function — two functions must fail.

#module MultiFn

MultiFn[T]: {
    to_a(x: T) -> CInt
    to_b(x: T) -> CInt
}

#module main

bar(args: ...MultiFn.MultiFn) -> CInt { 0 }

main() {
    bar(1, 2)
    0
}
