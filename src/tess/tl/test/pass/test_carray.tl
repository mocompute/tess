#module main
#import <cstring.tl>

main() {
    error := 0

    // Test CArray declaration and initialization
    arr: CArray[Int, 5] := void
    arr.[0] = 10
    arr.[1] = 20
    arr.[2] = 30
    arr.[3] = 40
    arr.[4] = 50

    // Test indexing
    error += if arr.[0] == 10 {0} else {1}
    error += if arr.[4] == 50 {0} else {1}

    // Test explicit array decay to pointer
    ptr: Ptr[Int] := arr
    error += if ptr.[0] == 10 {0} else {1}
    error += if ptr.[2] == 30 {0} else {1}

    // Test CArray of CChar (used for string buffers)
    buffer:  CArray[CChar, 256] := void
    buf_ptr: Ptr[CChar]         := buffer
    c_strcpy(buf_ptr, "Hello, World!")

    // Test that CArray decays properly to CString (Ptr[CChar])
    len := c_strlen(buf_ptr)
    error += if len == 13zu {0} else {1}

    // Test modification through pointer
    ptr.[1] = 25
    error += if arr.[1] == 25 {0} else {1}

    // Test with different element types
    float_arr: CArray[Float, 3] := void
    float_arr.[0] = 1.5
    float_arr.[1] = 2.5
    float_arr.[2] = 3.5
    error += if float_arr.[1] == 2.5 {0} else {1}

    error
}
