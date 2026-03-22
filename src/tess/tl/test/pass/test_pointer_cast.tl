#module main
#import <cstdio.tl>

main() {
    data                 := 0x0102030405060708
    arr: CArray[Int, 16] := void
    arr.[0] = data

    // explicit decay to pointer, then cast to a different type
    p: Ptr[Int]           := arr
    b: Ptr[CUnsignedChar] := p
    c_printf(c"b.[0] = 0x%x\n", b.[0])
    c_printf(c"b.[1] = 0x%x\n", b.[1])
    c_printf(c"b.[2] = 0x%x\n", b.[2])

    pp := p.&

    // double dereference
    val   := pp.*.*
    error := val - data

    // Note: assumes little-endian
    error += if (0x08u == b.[0]) {0} else {1}
    error += if (0x07u == b.[1]) {0} else {1}
    error += if (0x06u == b.[2]) {0} else {1}
    result: CInt := error
    result
}
