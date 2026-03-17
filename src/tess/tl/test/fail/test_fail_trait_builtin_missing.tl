// Test that a builtin type correctly fails to satisfy a user-defined trait
// when no implementation is provided for that type.
// foo is defined for Point but not for Int, so Int should fail the Foo bound.

#module Foo
Foo[T] : {
    foo(x: T) -> CInt
}

#module Point
Point : { x: CInt }
foo(p) -> CInt { p.x }

#module main

use_foo[T: Foo](x: T) -> CInt { x.foo() }

main() {
    use_foo(42)
}
