#module main
#import <cstdlib.tl>
#import <cstdio.tl>

// --- Types from test_pointer_deref ---
Point[a]: { x: a, y: a }

// --- Types from test_pointer_deref_then_addr ---
Shape: | Circle { radius: Int }
       | Square { length: Int }

// --- Types from test_pointer_cast_struct ---
Foo: { x: Int   }
Bar: { x: Float }

// --- test_pointer_deref ---
test_deref() {
    p: Ptr[Point[Int]] := c_malloc(sizeof[Point[Int]]())
    p->x = 0
    p->y = 1

    obj := p.*
    c_free(p)
    result: CInt := obj.x
    result
}

// --- test_pointer_deref_double ---
test_deref_double() {
    p: Ptr[Int] := c_malloc(16zu)
    p.[0] = 123
    pp := p.&

    val := pp.*.*
    c_free(p)
    result: CInt := val - 123
    result
}

// --- test_pointer_deref_then_addr ---
test_deref_then_addr() {
    s := Circle(3)
    p := s.&

    // Dereference pointer, then take mutable reference for mutable when
    when p.*.& {
        c: Circle { c->radius = 99 }
        else { }
    }

    result := when s {
        c: Circle { c.radius }
        else { 0 }
    }
    ret: CInt := result - 99
    ret
}

// --- test_pointer_cast ---
test_cast() {
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

// --- test_pointer_cast_generic ---
// Cast Ptr[T] to Ptr[CUnsignedChar] inside a generic function.
cast_to_bytes[T](p: Ptr[T]) -> Ptr[CUnsignedChar] {
    b: Ptr[CUnsignedChar] := p
    b
}

test_cast_generic() {
    data                 := 0x0102030405060708
    arr: CArray[Int, 16] := void
    arr.[0] = data

    p: Ptr[Int] := arr
    b := cast_to_bytes(p)

    // Note: assumes little-endian
    errors := 0
    errors += if (0x08u == b.[0]) {0} else {1}
    errors += if (0x07u == b.[1]) {0} else {1}
    errors += if (0x06u == b.[2]) {0} else {1}
    result: CInt := errors
    result
}

// --- test_pointer_cast_generic_signedness ---
// Cast Ptr[T] to Ptr[CChar] inside a generic function, then call with T=CUnsignedChar.
cast_to_cchar[T](p: Ptr[T]) -> Ptr[CChar] {
    b: Ptr[CChar] := p
    b
}

test_cast_generic_signedness() {
    arr: CArray[CUnsignedChar, 4] := void
    arr.[0] = 72u   // 'H'
    arr.[1] = 105u  // 'i'
    arr.[2] = 33u   // '!'
    arr.[3] = 0u    // null terminator

    p: Ptr[CUnsignedChar] := arr
    cp := cast_to_cchar(p)

    errors := 0
    errors += if (72 == cp.[0]) {0} else {1}
    errors += if (105 == cp.[1]) {0} else {1}
    errors += if (33 == cp.[2]) {0} else {1}
    result: CInt := errors
    result
}

// --- test_pointer_cast_struct ---
test_cast_struct() {
    floats: CArray[Bar, 8] := void
    floats.[0].x = 0.5

    floats_ptr: Ptr[Bar] := floats
    ints:       Ptr[Foo] := floats_ptr
    c_printf(c"val = %lli\n", ints.[0].x)
    0
}

// --- test_pointer_compare_null ---
test_compare_null() {
    p: Ptr[CInt] := c_malloc(1024zu)
    if p == null { 1 } else { 0 }
}

// --- test_pointer_array ---
test_array() {
    arr: CArray[Int, 16] := void
    arr.[0] = 123456

    // explicit decay to pointer
    p: Ptr[Int] := arr

    // this gives a Int**
    pp := p.&

    // double dereference
    val          := pp.*.*
    result: CInt := val - 123456
    result
}

main() {
    errors: CInt := 0
    errors += test_deref()
    errors += test_deref_double()
    errors += test_deref_then_addr()
    errors += test_cast()
    errors += test_cast_generic()
    errors += test_cast_generic_signedness()
    errors += test_cast_struct()
    errors += test_compare_null()
    errors += test_array()
    errors
}
