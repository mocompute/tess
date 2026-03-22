// Top-level module should take precedence over implicit submodule resolution.
// Bare c"Foo" in #module Parent should resolve to the top-level Foo.Foo,
// not Parent.Foo.Foo.

#module Foo
Foo: { x: CInt }

#module Parent

#module Parent.Foo
Foo: { y: CInt }

#module Parent

// Type position: Foo should be top-level Foo.Foo (has field x)
wrap_top(v: CInt) -> Foo { Foo.Foo(x = v) }

// Expression position: Foo(...) should be top-level Foo.Foo
make_top(v: CInt) -> Foo { Foo(x = v) }

// Explicit Parent.Foo.Foo for the submodule version
make_sub(v: CInt) -> Parent.Foo.Foo { Parent.Foo.Foo(y = v) }

#module main

main() {
    t := Parent.make_top(10)
    s := Parent.make_sub(20)
    t.x + s.y - 30
}
