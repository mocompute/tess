// User-defined trait: stringify should return CString, but impl returns CInt.
#module main

Stringify[T]: {
    stringify(x: T) -> CString
}

#module Foo

Foo: { x: CInt }

stringify(f: Foo) -> CInt { f.x }

#module main

show[T: Stringify](x: T) -> CInt { 0 }

main() {
    f := Foo.Foo(x = 42)
    show(f)
}
