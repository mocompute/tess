// Test that a type without a hash() implementation fails the Hash trait bound.

#import <Hash.tl>

#module Foo

Foo: { x: Int }

#module main

compute_hash[T: Hash](x: T) -> CSize {
    x.hash()
}

main() -> CInt {
    f := Foo.Foo(x = 1)
    h := compute_hash(f)
    0
}
