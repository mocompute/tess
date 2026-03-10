#module main

// --- Generic tree type ---
Tree[T]: | Leaf { v: T }
         | Node { v: T, left: Ptr[Tree[T]], right: Ptr[Tree[T]] }

// --- Generic fold over a tree using closures ---
tree_fold[T](tree: Tree[T], f_leaf: (T) -> Int, f_node: (T, Int, Int) -> Int) -> Int {
    when tree {
        l: Leaf { f_leaf(l.v) }
        n: Node {
            left_val  := tree_fold(n.left.*, f_leaf, f_node)
            right_val := tree_fold(n.right.*, f_leaf, f_node)
            f_node(n.v, left_val, right_val)
        }
    }
}

// --- Tree sum (monomorphic, for verification) ---
tree_sum(tree: Tree[Int]) -> Int {
    when tree {
        l: Leaf { l.v }
        n: Node {
            n.v + tree_sum(n.left.*) + tree_sum(n.right.*)
        }
    }
}

// --- Tree depth (monomorphic) ---
tree_depth(tree: Tree[Int]) -> Int {
    when tree {
        l: Leaf { 1 }
        n: Node {
            ld := tree_depth(n.left.*)
            rd := tree_depth(n.right.*)
            d  := if ld > rd { ld } else { rd }
            1 + d
        }
    }
}

// --- Tree count leaves (monomorphic) ---
tree_count(tree: Tree[Int]) -> Int {
    when tree {
        l: Leaf { 1 }
        n: Node {
            tree_count(n.left.*) + tree_count(n.right.*)
        }
    }
}

// Test 1: tree_fold (generic) with closures - sum all values
test_tree_fold_sum() {
    l1   := Leaf(1)
    l2   := Leaf(2)
    root := Node(10, l1.&, l2.&)

    identity := (x: Int) { x }
    combine  := (v: Int, left_sum: Int, right_sum: Int) { v + left_sum + right_sum }
    result   := tree_fold(root, identity, combine)

    // 10 + 1 + 2 = 13
    if result == 13 {0} else {1}
}

// Test 2: tree_fold - count leaves (bigger tree)
test_tree_fold_count() {
    l1    := Leaf(10)
    l2    := Leaf(20)
    l3    := Leaf(30)
    l4    := Leaf(40)
    left  := Node(0, l1.&, l2.&)
    right := Node(0, l3.&, l4.&)
    root  := Node(0, left.&, right.&)

    one      := (x: Int) { 1 }
    sum_kids := (v: Int, a: Int, b: Int) { a + b }
    result   := tree_fold(root, one, sum_kids)

    if result == 4 {0} else {1}
}

// Test 3: Closure captures a tree node and calls monomorphic traversal
test_closure_captures_tree() {
    l1   := Leaf(5)
    l2   := Leaf(15)
    root := Node(0, l1.&, l2.&)

    get_sum := () { tree_sum(root) }

    if get_sum() == 20 {0} else {1}
}

// Test 4: tree_fold with closure that captures external state
test_tree_fold_with_capture() {
    l1    := Leaf(1)
    l2    := Leaf(2)
    l3    := Leaf(3)
    right := Node(0, l2.&, l3.&)
    root  := Node(0, l1.&, right.&)

    scale       := 10
    scaled_leaf := (x: Int) { x * scale }
    combine     := (v: Int, a: Int, b: Int) { a + b }
    result      := tree_fold(root, scaled_leaf, combine)

    // 10 + 20 + 30 = 60
    if result == 60 {0} else {1}
}

// Test 5: Tree depth with closure capture
test_tree_depth_closure() {
    l1   := Leaf(1)
    l2   := Leaf(2)
    l3   := Leaf(3)
    l4   := Leaf(4)
    deep := Node(0, l3.&, l4.&)
    mid  := Node(0, l2.&, deep.&)
    root := Node(0, l1.&, mid.&)

    get_depth := () { tree_depth(root) }

    // depth = 4 (root -> mid -> deep -> leaf)
    if get_depth() == 4 {0} else {1}
}

// Test 6: Multiple closures capturing same tree, different operations
test_multiple_closures_same_tree() {
    l1   := Leaf(10)
    l2   := Leaf(20)
    l3   := Leaf(30)
    left := Node(0, l1.&, l2.&)
    root := Node(0, left.&, l3.&)

    get_sum   := () { tree_sum(root) }
    get_depth := () { tree_depth(root) }
    get_count := () { tree_count(root) }

    error := 0
    error += if get_sum() == 60 {0} else {1}
    error += if get_depth() == 3 {0} else {1}
    error += if get_count() == 3 {0} else {1}
    error
}

// Test 7: Fold tree to find max leaf value
test_tree_fold_max() {
    l1    := Leaf(1)
    l2    := Leaf(42)
    l3    := Leaf(3)
    l4    := Leaf(7)
    left  := Node(0, l1.&, l2.&)
    right := Node(0, l3.&, l4.&)
    root  := Node(0, left.&, right.&)

    get_val  := (x: Int) { x }
    pick_max := (v: Int, a: Int, b: Int) {
        m := if a > b { a } else { b }
        if v > m { v } else { m }
    }
    result := tree_fold(root, get_val, pick_max)

    if result == 42 {0} else {1}
}

// Test 8: Closure captures tree and mutable state, called repeatedly
test_closure_mutable_with_tree() {
    l1   := Leaf(5)
    l2   := Leaf(10)
    root := Node(1, l1.&, l2.&)

    offset  := 0
    compute := () { tree_sum(root) + offset }

    error := 0
    // Before mutation: 1 + 5 + 10 = 16
    error += if compute() == 16 {0} else {1}

    offset = 100
    // After mutation: 16 + 100 = 116
    error += if compute() == 116 {0} else {1}
    error
}

main() {
    error := 0
    error += test_tree_fold_sum()
    error += test_tree_fold_count()
    error += test_closure_captures_tree()
    error += test_tree_fold_with_capture()
    error += test_tree_depth_closure()
    error += test_multiple_closures_same_tree()
    error += test_tree_fold_max()
    error += test_closure_mutable_with_tree()
    error
}
