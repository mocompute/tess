// Test: auto-address-of must NOT apply to Ptr[T] (mutable pointer) parameters.
// Only Ptr[Const[T]] gets implicit address-of.

#module Foo

Foo: { v: Int }

mutate(p: Ptr[Foo]) {
    p->v = 99
}

#module main

main() {
    f := Foo(v = 1)
    Foo.mutate(f)
    0
}
