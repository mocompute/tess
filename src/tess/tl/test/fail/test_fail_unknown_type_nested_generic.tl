// Unknown type name nested inside a generic type argument should be rejected.
#module main
Pair[a, b]: { first: a, second: b }
main() -> CInt {
    v: Pair[Ptr[Quux], CInt] := null
    0
}
