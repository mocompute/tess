// Test that a type without a hash() implementation fails the Hash trait bound.

#module Foo

Foo: { x: Int }

#module main

compute_hash[T: Hash](x: T) -> CSize {
    hash(x)
}

main() -> CInt {
    f := Foo.Foo(x = 1)
    h := compute_hash(f)
    0
}
