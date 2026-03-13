// Trait bounds on forward declarations should parse but currently fail with expected_comma.
#module main

foo[T: Hash](x: T) -> CSize
foo[T: Hash](x: T) -> CSize { hash(x) }

main() -> CInt {
    h := foo(42)
    if h == 0zu { 1 } else { 0 }
}
