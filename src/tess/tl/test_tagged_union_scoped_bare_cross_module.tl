#module Foo
Op: | A | B

#module main

main() {
    error := 0

    // Cross-module bare symbol: Foo.Op.A should produce Foo.Op
    x := Foo.Op.A
    r := when x {
        a: A { 1 }
        b: B { 0 }
    }
    error += if r == 1 { 0 } else { 1 }

    error
}
