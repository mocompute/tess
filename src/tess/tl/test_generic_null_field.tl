// Bug: generic struct constructor with null for Ptr[V] field
// leaves type variable V unresolved.
//
// Simple[V](data = null, size = 0u)  -- V is not resolved from call site
// Workaround: use a typed local: p: Ptr[V] := null; Simple(data = p, ...)
#module main

Simple[V]: {
    data: Ptr[V],
    size: UInt,
}

make[V]() -> Simple[V] {
    Simple[V](data = null, size = 0u)
}

main() {
    s := make[Int]()
    if s.size != 0u { return 1 }
    0
}
