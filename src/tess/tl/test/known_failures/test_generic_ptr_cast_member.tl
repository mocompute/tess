#module main
#import <Array.tl>
#import <Alloc.tl>

// Minimal reproduction of the String.from_array bug: casting arr->data (Ptr[T])
// to Ptr[CChar] inside a generic function fails with "conflicting types".

get_bytes[T](arr: Ptr[Const[Array[T]]]) -> Ptr[CChar] {
    data: Ptr[CChar] := arr->data
    data
}

main() {
    arr := Array.init(0, 16)
    arr.push(72)
    arr.push(105)

    result := arr.get_bytes()

    errors := 0
    errors += if (72 == result.[0]) {0} else {1}
    errors += if (105 == result.[1]) {0} else {1}

    arr.free()
    ret: CInt := errors
    ret
}
