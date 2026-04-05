#module main
#import <cstring.tl>

test_declaration_and_indexing() {
    arr: CArray[Int, 5] := void
    arr.[0] = 10
    arr.[1] = 20
    arr.[2] = 30
    arr.[3] = 40
    arr.[4] = 50

    error := 0
    error += if arr.[0] == 10 {0} else {1}
    error += if arr.[4] == 50 {0} else {1}
    error
}

test_decay_to_pointer() {
    arr: CArray[Int, 5] := void
    arr.[0] = 10
    arr.[2] = 30

    ptr: Ptr[Int] := arr
    error         := 0
    error += if ptr.[0] == 10 {0} else {1}
    error += if ptr.[2] == 30 {0} else {1}
    error
}

test_cchar_buffer() {
    buffer:  CArray[CChar, 256] := void
    buf_ptr: Ptr[CChar]         := buffer
    c_strcpy(buf_ptr, c"Hello, World!")

    len := c_strlen(buf_ptr)
    if len == 13 {0} else {1}
}

test_modification_through_pointer() {
    arr: CArray[Int, 5] := void
    arr.[1] = 20

    ptr: Ptr[Int] := arr
    ptr.[1] = 25
    if arr.[1] == 25 {0} else {1}
}

test_float_array() {
    float_arr: CArray[Float, 3] := void
    float_arr.[0] = 1.5
    float_arr.[1] = 2.5
    float_arr.[2] = 3.5
    if float_arr.[1] == 2.5 {0} else {1}
}

main() {
    error := 0
    error += test_declaration_and_indexing()
    error += test_decay_to_pointer()
    error += test_cchar_buffer()
    error += test_modification_through_pointer()
    error += test_float_array()
    error
}
