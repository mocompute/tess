// A struct is not a trait — using it as a variadic bound must fail.

#module Foo

Foo: { x: CInt }

#module main

bar(args: ...Foo.Foo) -> CInt { 0 }

main() {
    bar(1, 2)
    0
}
