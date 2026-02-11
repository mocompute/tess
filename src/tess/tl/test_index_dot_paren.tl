#module main

main() {
    error := 0

    // Ptr indexing via c_malloc
    a: Ptr[Int] := c_malloc(4 * sizeof(Int))
    a.(0) = 10
    a.(1) = 20
    a.(2) = 30
    a.(3) = 40

    error += if a.(0) == 10 {0} else {1}
    error += if a.(3) == 40 {0} else {1}

    // Expression inside parens
    i := 1
    error += if a.(i) == 20 {0} else {1}
    error += if a.(i + 1) == 30 {0} else {1}
    error += if a.(2 * i + 1) == 40 {0} else {1}

    // CArray indexing
    arr: CArray[Int, 5] := void
    arr.(0) = 100
    arr.(1) = 200
    arr.(2) = 300
    arr.(3) = 400
    arr.(4) = 500
    error += if arr.(0) == 100 {0} else {1}
    error += if arr.(4) == 500 {0} else {1}

    // CArray decay to pointer, then index
    ptr: Ptr[Int] := arr
    error += if ptr.(0) == 100 {0} else {1}
    error += if ptr.(2) == 300 {0} else {1}

    // Lvalue indexing through pointer
    ptr.(1) = 250
    error += if arr.(1) == 250 {0} else {1}

    // Chained deref + index: pp.*.(1)
    pp: Ptr[Ptr[Int]] := c_malloc(sizeof(Ptr[Int]))
    pp.* = a
    error += if pp.*.(1) == 20 {0} else {1}
    error += if pp.*.(3) == 40 {0} else {1}
    c_free(pp)

    c_free(a)
    error
}
