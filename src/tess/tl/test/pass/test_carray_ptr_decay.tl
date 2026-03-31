// Test: implicit CArray-to-Ptr decay at function call sites.
// CArray[T, N] should automatically coerce to Ptr[T] when passed to a
// function expecting a Ptr parameter, matching C array decay semantics.

#module Buf
#import <cstring.tl>

// Ptr[any] parameter (void*)
zero_buf(p: Ptr[any], n: CSize) { c_memset(p, 0, n) }

// Ptr[CInt] parameter (typed pointer)
sum(p: Ptr[CInt], n: CInt) -> CInt {
    total: CInt := 0
    i: CInt := 0
    while i < n {
        total += p.[i]
        i += 1
    }
    total
}

// Ptr[Const[CInt]] parameter (read-only pointer — tests guard in auto_address_of_ptr_const_args)
sum_const(p: Ptr[Const[CInt]], n: CInt) -> CInt {
    total: CInt := 0
    i: CInt := 0
    while i < n {
        total += p.[i]
        i += 1
    }
    total
}

// UFCS receiver Ptr[CArray_wrapper] — tests guard in ufcs_rewrite_call
CArray_wrapper: { data: CArray[CInt, 4] }

fill(self: Ptr[CArray_wrapper], val: CInt) {
    i: CInt := 0
    ptr: Ptr[CInt] := self->data
    while i < 4 {
        ptr.[i] = val
        i += 1
    }
}

#module main
#import <cstring.tl>

main() {
    error := 0

    // Test 1: CArray passed to Ptr[any] parameter (c_memset via import)
    arr: CArray[CInt, 4] := void
    c_memset(arr, 0, sizeof(arr))
    if arr.[0] != 0 {
        c_printf(c"FAIL 1: c_memset with CArray did not zero\n")
        error += 1
    }

    // Test 2: CArray passed to Ptr[any] via user function
    Buf.zero_buf(arr, sizeof(arr))
    arr.[0] = 10
    arr.[1] = 20
    arr.[2] = 30
    arr.[3] = 40
    if arr.[0] != 10 {
        c_printf(c"FAIL 2: zero_buf + assign failed\n")
        error += 1
    }

    // Test 3: CArray passed to Ptr[CInt] (typed pointer, element type matches)
    if Buf.sum(arr, 4) != 100 {
        c_printf(c"FAIL 3: sum(Ptr[CInt]) = %d, expected 100\n", Buf.sum(arr, 4))
        error += 1
    }

    // Test 4: CArray passed to Ptr[Const[CInt]] (tests auto_address_of_ptr_const guard)
    if Buf.sum_const(arr, 4) != 100 {
        c_printf(c"FAIL 4: sum_const(Ptr[Const[CInt]]) failed\n")
        error += 1
    }

    // Test 5: sizeof still returns full array size after decay feature
    if sizeof(arr) != 4zu * sizeof[CInt]() {
        c_printf(c"FAIL 5: sizeof(arr) = %zu, expected %zu\n", sizeof(arr), 4zu * sizeof[CInt]())
        error += 1
    }

    error
}
