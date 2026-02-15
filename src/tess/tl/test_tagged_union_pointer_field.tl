#module main

// Tagged union variants with pointer fields
Container: | Owned { value: Int }
           | Ref   { ptr: Ptr[Int] }
           | Empty

main() {
    error := 0

    // Test variant with pointer to Int
    val := 42
    r := Ref(val.&)
    result := when r {
        o: Owned { o.value }
        r: Ref   { r.ptr.* }
        e: Empty { 0 }
    }
    error += if result == 42 {0} else {1}

    // Mutate through pointer field using mutable when
    when r.& {
        r: Ref { r->ptr.* = 99 }
        else { }
    }
    error += if val == 99 {0} else {1}

    // Test null pointer in variant
    null_ptr: Ptr[Int] := null
    null_ref := Ref(null_ptr)
    is_null := when null_ref {
        r: Ref { if r.ptr == null {1} else {0} }
        else { 0 }
    }
    error += if is_null == 1 {0} else {1}

    // Test Owned variant (no pointer)
    owned := Owned(77)
    result := when owned {
        o: Owned { o.value }
        else { 0 }
    }
    error += if result == 77 {0} else {1}

    // Test Empty variant
    empty := Empty()
    result := when empty {
        e: Empty { -1 }
        else { 0 }
    }
    error += if result == -1 {0} else {1}

    error
}
