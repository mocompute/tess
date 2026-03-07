// Built-in types should NOT satisfy arbitrary user-defined traits.
// Int has no 'print' function, so it cannot conform to Printable.

#module main

Printable[T] : {
    print(a: T) -> CInt
}

show[T: Printable](x: T) -> CInt {
    print(x)
}

main() {
    // This should fail: Int does not satisfy Printable
    show(42)
}
