// A module must not define both a same-named type and a type named T,
// because auto-collapse cannot resolve which one the bare module name
// should refer to.

#module Foo

Foo: { x: Int }

T: { y: Int }

#module main

main() -> CInt {
    a := Foo(x = 1)
    0
}
