#module main

// Mutually recursive generic types, each specialized at two different types.
// Tests that placeholder resolution handles cross-type deferred references
// when both types are multi-specialized.

Tree[T]:   { value: T, children: Ptr[Forest[T]] }
Forest[T]: { head: Ptr[Tree[T]], rest: Ptr[Forest[T]] }

tree_value[T](p: Ptr[Tree[T]]) -> T {
    p.*.value
}

main() {
    // Tree[Int] / Forest[Int]
    t1 := Tree[Int](value = 10, children = null)
    t2 := Tree[Int](value = 20, children = null)
    f1 := Forest[Int](head = t1.&, rest = null)

    // Tree[Bool] / Forest[Bool]
    t3 := Tree[Bool](value = true, children = null)
    f2 := Forest[Bool](head = t3.&, rest = null)

    error := 0
    error += if tree_value(t1.&) == 10 { 0 } else { 1 }
    error += if tree_value(t3.&) == true { 0 } else { 1 }
    error
}
