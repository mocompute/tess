// hash() returns Int instead of CSize — should fail trait satisfaction.
#module MyType

MyType: { x: CInt }

hash(m: MyType) -> Int { m.x }

#module main

needs_hash[T: Hash](x: T) -> CSize {
    hash(x)
}

main() {
    v := MyType.MyType(x = 42)
    needs_hash(v)
    0
}
