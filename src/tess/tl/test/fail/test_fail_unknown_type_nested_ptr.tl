// Unknown type name nested inside Ptr[Ptr[...]] should be rejected.
#module main
main() -> CInt {
    v: Ptr[Ptr[Quux]] := null
    0
}
