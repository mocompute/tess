// Unknown type name in a variable annotation should be rejected.
#module main

main() -> CInt {
    v: Quux := 42
    v
}
