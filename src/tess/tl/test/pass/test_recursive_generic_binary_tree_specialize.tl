#module main

// Binary tree: two recursive Ptr fields in the same variant, multi-specialized.
// Both fields share the same placeholder during type resolution.
Tree[T]: | Node { val: T, left: Ptr[Tree[T]], right: Ptr[Tree[T]] }
         | Leaf

count[T](p: Ptr[Tree[T]]) -> Int {
    t := p.*
    when t {
        n: Node { 1 + count(n.left) + count(n.right) }
        l: Leaf { 0 }
    }
}

main() {
    // Tree[Int]
    leaf_i: Tree[Int] := Leaf()
    node_i            := Node(val = 42, left = leaf_i.&, right = leaf_i.&)

    // Tree[Bool]
    leaf_b: Tree[Bool] := Leaf()
    node_b             := Node(val = true, left = leaf_b.&, right = leaf_b.&)

    error := 0
    error += if count(node_i.&) == 1 { 0 } else { 1 }
    error += if count(node_b.&) == 1 { 0 } else { 1 }
    error
}
