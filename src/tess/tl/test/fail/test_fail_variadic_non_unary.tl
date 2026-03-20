// A variadic trait function must be unary — a binary function must fail.

#module BinaryTrait

BinaryTrait[T]: {
    combine(a: T, b: T) -> CInt
}

#module main

bar(args: ...BinaryTrait.BinaryTrait) -> CInt { 0 }

main() {
    bar(1, 2)
    0
}
