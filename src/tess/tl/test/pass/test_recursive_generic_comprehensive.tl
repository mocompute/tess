#module main

// === From test_recursive_generic_binary_tree_specialize ===
// Binary tree: two recursive Ptr fields in the same variant, multi-specialized.
// Both fields share the same placeholder during type resolution.
BinTree[T]: | BinNode { val: T, left: Ptr[BinTree[T]], right: Ptr[BinTree[T]] }
            | BinLeaf

bin_count[T](p: Ptr[BinTree[T]]) -> Int {
    t := p.*
    when t {
        n: BinNode { 1 + bin_count(n.left) + bin_count(n.right) }
        l: BinLeaf { 0 }
    }
}

// === From test_recursive_generic_cross_specialize ===
// Cross-specialization: a function that takes two differently-specialized
// recursive generic types in the same call, forcing both specializations
// to coexist during inference.
CrossList[T]: | CLCons { head: T, tail: Ptr[CrossList[T]] }
              | CLNil

cross_head_int(list: CrossList[Int]) -> Int {
    when list {
        c: CLCons { c.head }
        n: CLNil { 0 }
    }
}

cross_head_bool(list: CrossList[Bool]) -> Bool {
    when list {
        c: CLCons { c.head }
        n: CLNil { false }
    }
}

// Takes both specializations simultaneously
check_both(a: CrossList[Int], b: CrossList[Bool]) -> Int {
    i     := cross_head_int(a)
    bo    := cross_head_bool(b)
    error := 0
    error += if i == 7 { 0 } else { 1 }
    error += if bo == true { 0 } else { 1 }
    error
}

// === From test_recursive_generic_map_transform ===
// Generic function that transforms a recursive generic type from one
// specialization to another: MapList[T] -> MapList[U]
MapList[T]: | MLCons { head: T, tail: Ptr[MapList[T]] }
            | MLNil

map_list[T, U](list: MapList[T], f: (T) -> U) -> Ptr[MapList[U]] {
    when list {
        c: MLCons {
            next                    := c.tail.*
            rest                    := map_list(next, f)
            result: Ptr[MapList[U]] := c_malloc(64zu)
            result.* = MLCons(head = f(c.head), tail = rest)
            result
        }
        n: MLNil {
            result: Ptr[MapList[U]] := c_malloc(64zu)
            result.* = MLNil()
            result
        }
    }
}

map_sum(list: MapList[Int]) -> Int {
    when list {
        c: MLCons { c.head + map_sum(c.tail.*) }
        n: MLNil { 0 }
    }
}

// === From test_recursive_generic_multi_param_specialize ===
// Multi-parameter recursive generic tagged union, specialized at two different type combinations
KVList[K, V]: | Entry { key: K, val: V, next: Ptr[KVList[K, V]] }
              | End

// === From test_recursive_generic_multi_specialize ===
// Recursive generic tagged union specialized at two different types
MsList[T]: | MsCons { head: T, tail: Ptr[MsList[T]] }
           | MsNil

ms_is_nil[T](p: Ptr[MsList[T]]) -> Bool {
    v := p.*
    when v {
        c: MsCons { false }
        n: MsNil { true }
    }
}

// === From test_recursive_generic_mutual_multi_specialize ===
// Mutually recursive generic types, each specialized at two different types.
// Tests that placeholder resolution handles cross-type deferred references
// when both types are multi-specialized.
MutTree[T]:   { value: T, children: Ptr[MutForest[T]]          }
MutForest[T]: { head: Ptr[MutTree[T]], rest: Ptr[MutForest[T]] }

mut_tree_value[T](p: Ptr[MutTree[T]]) -> T {
    p.*.value
}

// === From test_recursive_generic_triple_specialize ===
// Recursive generic tagged union specialized at three different types
TriList[T]: | TrCons { head: T, tail: Ptr[TriList[T]] }
            | TrNil

tri_is_nil[T](p: Ptr[TriList[T]]) -> Bool {
    v := p.*
    when v {
        c: TrCons { false }
        n: TrNil { true }
    }
}

// === From test_recursive_generic_two_independent ===
// Two independent recursive generic tagged unions in the same module,
// both multi-specialized. Tests that deferred placeholders from one
// don't interfere with the other via shared type_parse_ctx state.
ListA[T]: | ACons { head: T, tail: Ptr[ListA[T]] }
          | ANil

ListB[T]: | BCons { head: T, tail: Ptr[ListB[T]] }
          | BNil


// ============================================================
// Test functions (one per original file)
// ============================================================

test_binary_tree_specialize() -> CInt {
    // BinTree[Int]
    leaf_i: BinTree[Int] := BinLeaf()
    node_i               := BinNode(val = 42, left = leaf_i.&, right = leaf_i.&)

    // BinTree[Bool]
    leaf_b: BinTree[Bool] := BinLeaf()
    node_b                := BinNode(val = true, left = leaf_b.&, right = leaf_b.&)

    error: CInt := 0
    error += if bin_count(node_i.&) == 1 { 0 } else { 1 }
    error += if bin_count(node_b.&) == 1 { 0 } else { 1 }
    error
}

test_cross_specialize() -> CInt {
    nil_i: CrossList[Int] := CLNil()
    list_i                := CLCons(head = 7, tail = nil_i.&)

    nil_b: CrossList[Bool] := CLNil()
    list_b                 := CLCons(head = true, tail = nil_b.&)

    result: CInt := check_both(list_i, list_b)
    result
}

test_map_transform() -> CInt {
    nil: MapList[Int] := MLNil()
    l3                := MLCons(head = 3, tail = nil.&)
    l2                := MLCons(head = 2, tail = l3.&)
    l1                := MLCons(head = 1, tail = l2.&)

    // Map Int -> Int (double each element)
    double  := (x: Int) { x * 2 }
    doubled := map_list(l1, double)

    error: CInt := 0
    error += if map_sum(doubled.*) == 12 { 0 } else { 1 }
    error
}

test_multi_param_specialize() -> CInt {
    // Specialization 1: KVList[Int, Bool]
    a_end: KVList[Int, Bool] := End()
    a                        := Entry(key = 1, val = true, next = a_end.&)

    // Specialization 2: KVList[Bool, Int]
    b_end: KVList[Bool, Int] := End()
    b                        := Entry(key = true, val = 42, next = b_end.&)

    error: CInt := 0

    when a {
        e: Entry { error += if e.key == 1 { 0 } else { 1 } }
        n: End { error += 1 }
    }

    when b {
        e: Entry { error += if e.val == 42 { 0 } else { 1 } }
        n: End { error += 1 }
    }

    error
}

test_multi_specialize() -> CInt {
    a: Ptr[MsList[Int]] := c_malloc(8zu)
    a.* = MsNil()

    b: Ptr[MsList[Bool]] := c_malloc(8zu)
    b.* = MsNil()

    error: CInt := 0
    error += if ms_is_nil(a) { 0 } else { 1 }
    error += if ms_is_nil(b) { 0 } else { 1 }

    c_free(a)
    c_free(b)
    error
}

test_mutual_multi_specialize() -> CInt {
    // MutTree[Int] / MutForest[Int]
    t1 := MutTree[Int](value = 10, children = null)
    t2 := MutTree[Int](value = 20, children = null)
    f1 := MutForest[Int](head = t1.&, rest = null)

    // MutTree[Bool] / MutForest[Bool]
    t3 := MutTree[Bool](value = true, children = null)
    f2 := MutForest[Bool](head = t3.&, rest = null)

    error: CInt := 0
    error += if mut_tree_value(t1.&) == 10 { 0 } else { 1 }
    error += if mut_tree_value(t3.&) == true { 0 } else { 1 }
    error
}

test_triple_specialize() -> CInt {
    a: Ptr[TriList[Int]] := c_malloc(8zu)
    a.* = TrNil()

    b: Ptr[TriList[Bool]] := c_malloc(8zu)
    b.* = TrNil()

    c: Ptr[TriList[CInt8]] := c_malloc(8zu)
    c.* = TrNil()

    error: CInt := 0
    error += if tri_is_nil(a) { 0 } else { 1 }
    error += if tri_is_nil(b) { 0 } else { 1 }
    error += if tri_is_nil(c) { 0 } else { 1 }

    c_free(a)
    c_free(b)
    c_free(c)
    error
}

test_two_independent() -> CInt {
    // ListA specialized at Int and Bool
    a1: ListA[Int]  := ANil()
    a2: ListA[Bool] := ANil()

    // ListB specialized at Int and Bool
    b1: ListB[Int]  := BNil()
    b2: ListB[Bool] := BNil()

    error: CInt := 0

    when a1 { c: ACons { error += 1 } n: ANil { } }
    when a2 { c: ACons { error += 1 } n: ANil { } }
    when b1 { c: BCons { error += 1 } n: BNil { } }
    when b2 { c: BCons { error += 1 } n: BNil { } }

    error
}

main() {
    error := 0
    error += test_binary_tree_specialize()
    error += test_cross_specialize()
    error += test_map_transform()
    error += test_multi_param_specialize()
    error += test_multi_specialize()
    error += test_mutual_multi_specialize()
    error += test_triple_specialize()
    error += test_two_independent()
    error
}
