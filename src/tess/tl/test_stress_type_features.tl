#module main

// Tagged unions for tests
Shape: | Circle { radius: Int }
       | Square { length: Int }
       | MyNone

// Type predicate on a tagged union variant
test_type_predicate_variant() {
    s := Circle(5)
    result := when s {
        c: Circle {
            v := c.radius
            if v :: Int { v } else { -1 }
        }
        else { -1 }
    }
    if result == 5 {0} else {1}
}

// Pointer to a tagged union, dereferenced and matched with when
test_pointer_to_union() {
    s := Circle(8)
    p := s.&
    elem := p.*

    result := when elem {
        c: Circle { c.radius }
        sq: Square { sq.length }
        n: MyNone { 0 }
    }
    if result == 8 {0} else {1}
}

// CArray of tagged unions, iterated and matched
test_carray_of_unions() {
    arr: CArray[Shape, 4] := void
    arr.[0] = Circle(1)
    arr.[1] = Square(2)
    arr.[2] = Circle(3)
    arr.[3] = MyNone()

    sum := 0
    ptr: Ptr[Shape] := arr
    i := 0
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        sum = sum + when elem {
            c: Circle { c.radius }
            sq: Square { sq.length }
            n: MyNone { 0 }
        }
    }
    // 1 + 2 + 3 + 0 = 6
    if sum == 6 {0} else {1}
}

// Tagged union with a variant that has a pointer field, combined with CArray
Container: | Holds { ptr: Ptr[Int], len: Int }
           | Nothing

test_variant_with_ptr_to_carray() {
    arr: CArray[Int, 4] := void
    p: Ptr[Int] := arr
    p.[0] = 10
    p.[1] = 20
    p.[2] = 30

    c := Holds(p, 3)

    result := when c {
        h: Holds {
            sum := 0
            j := 0
            while j < h.len, j = j + 1 {
                sum = sum + h.ptr.[j]
            }
            sum
        }
        n: Nothing { 0 }
    }
    if result == 60 {0} else {1}
}

// Recursive tagged union matched multiple levels deep
IntList: | Cons { head: Int, tail: Ptr[IntList] }
         | Nil

sum_list(list: IntList) -> Int {
    when list {
        c: Cons {
            next := c.tail.*
            c.head + sum_list(next)
        }
        n: Nil { 0 }
    }
}

test_recursive_union_deep() {
    nil: IntList  := Nil()
    n1 := Cons(1, nil.&)
    n2 := Cons(2, n1.&)
    n3 := Cons(3, n2.&)
    n4 := Cons(4, n3.&)
    n5 := Cons(5, n4.&)

    result := sum_list(n5)
    // 5 + 4 + 3 + 2 + 1 = 15
    if result == 15 {0} else {1}
}

// Mutable when through pointer (mutate via mutable when, then verify)
test_mutable_when_via_ptr() {
    s := Circle(3)

    // Use mutable when to modify through the tagged union
    when s.& {
        c: Circle { c->radius = 99 }
        else { }
    }

    // Read back through pointer dereference
    p := s.&
    elem := p.*
    result := when elem {
        c: Circle { c.radius }
        else { 0 }
    }
    if result == 99 {0} else {1}
}

// sizeof on a tagged union
test_sizeof_union() {
    sz := sizeof[Shape]()
    // Just verify it's positive (actual size is platform-dependent)
    if sz > 0 {0} else {1}
}

main() {
    error := 0
    error += test_type_predicate_variant()
    error += test_pointer_to_union()
    error += test_carray_of_unions()
    error += test_variant_with_ptr_to_carray()
    error += test_recursive_union_deep()
    error += test_mutable_when_via_ptr()
    error += test_sizeof_union()
    error
}
