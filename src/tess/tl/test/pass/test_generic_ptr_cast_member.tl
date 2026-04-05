#module main
#import <Array.tl>
#import <Alloc.tl>

// Test: casting arr->data (Ptr[T]) to Ptr[CChar] inside a generic function.
// Uses T=CUnsignedChar (distinct from CChar) so the cast is non-trivial,
// while single-byte elements keep indexing portable (no endianness issues).

get_bytes[T](arr: Ptr[Const[Array[T]]]) -> Ptr[CChar] {
    data: Ptr[CChar] := arr->data
    data
}

main() {
    arr              := Array.with_capacity[CUnsignedChar](16)
    a: CUnsignedChar := 72
    b: CUnsignedChar := 105
    arr.push(a)
    arr.push(b)

    result := arr.get_bytes()

    errors := 0
    errors += if (72 == result.[0]) {0} else {1}
    errors += if (105 == result.[1]) {0} else {1}

    arr.free()
    errors
}
