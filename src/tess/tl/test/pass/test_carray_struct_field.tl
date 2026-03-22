#module main
#import <cstring.tl>

// Struct with a CArray field
Buffer: { data: CArray[CChar, 256], len: CSize }

// Struct with multiple CArray fields
Matrix: { row0: CArray[CInt, 3], row1: CArray[CInt, 3] }

// Nested struct containing CArray field
Wrapper: { buf: Buffer, id: CInt }

main() {
    error := 0

    // Test 1: Basic CArray struct field write/read via explicit decay
    b                    := Buffer(data = void, len = 0zu)
    data_ptr: Ptr[CChar] := b.data
    data_ptr.[0] = 'H'
    data_ptr.[1] = 'i'
    data_ptr.[2] = '\0'
    error += if data_ptr.[0] == 'H' {0} else {1}
    error += if data_ptr.[1] == 'i' {0} else {1}

    // Test 2: CArray field decay to pointer (pass to C function)
    c_strcpy(data_ptr, c"Hello")
    b.len = c_strlen(data_ptr)
    error += if b.len == 5zu {0} else {1}

    // Test 3: Multiple CArray fields
    m                   := Matrix(row0 = void, row1 = void)
    row0_ptr: Ptr[CInt] := m.row0
    row1_ptr: Ptr[CInt] := m.row1
    row0_ptr.[0] = 1
    row0_ptr.[1] = 2
    row0_ptr.[2] = 3
    row1_ptr.[0] = 4
    row1_ptr.[1] = 5
    row1_ptr.[2] = 6
    error += if row0_ptr.[0] == 1 {0} else {1}
    error += if row1_ptr.[2] == 6 {0} else {1}

    // Test 4: CArray field access through pointer (the dangling pointer bug).
    // Without the lvalue fix, b2.data would copy Buffer to a temporary, then decay the CArray
    // in the temporary to a pointer — a dangling pointer once the temporary is gone.
    b2                    := Buffer(data = void, len = 0zu)
    bp                    := b2.&
    field_ptr: Ptr[CChar] := bp->data
    c_strcpy(field_ptr, c"World")
    error += if c_strlen(field_ptr) == 5zu {0} else {1}
    // Verify the write went into b2, not a dead temporary
    check_ptr: Ptr[CChar] := b2.data
    error += if c_strlen(check_ptr) == 5zu {0} else {1}

    error
}
