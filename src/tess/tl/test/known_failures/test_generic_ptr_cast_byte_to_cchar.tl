#module Buf
#import <Array.tl>

// Minimal reproduction of the String.from_array bug.
// The key: from_array calls a sibling from_bytes that takes Ptr[CChar].
// The constraint solver unifies T with CChar before specialization,
// causing a conflict when T=Byte (CUnsignedChar).

Buf: { data: Ptr[CChar], len: CSize }

from_bytes(data: Ptr[CChar], len: CSize) -> Buf {
    Buf(data = data, len = len)
}

from_array[T](arr: Ptr[Const[Array[T]]]) -> Buf {
    elem_size := sizeof(arr->data.[0])
    data: Ptr[CChar] := arr->data
    from_bytes(data, arr->size * elem_size)
}

#module main

main() {
    arr := Array.empty[Byte]()
    b: Byte := 0x41
    arr.push(b)

    buf := Buf.from_array(arr.&)

    errors := 0
    errors += if (buf.len == 1) {0} else {1}

    arr.free()
    ret: CInt := errors
    ret
}
