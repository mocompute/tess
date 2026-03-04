#module main
#import <HashMap.tl>
#import <Alloc.tl>
#import <Array.tl>
#import <stdio.tl>

test_create_destroy() {
    error := 0
    m := HashMap.create[Int, Int]()
    if HashMap.size(m) != 0u { error += 1 }
    if !HashMap.is_empty(m) { error += 1 }
    HashMap.destroy(m)
    error
}

test_set_get() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 100)
    HashMap.set(m, 2, 200)
    HashMap.set(m, 3, 300)

    if HashMap.size(m) != 3u { error += 1 }

    p1 := HashMap.get(m, 1)
    p2 := HashMap.get(m, 2)
    p3 := HashMap.get(m, 3)

    if p1 == null { error += 1 }
    if p2 == null { error += 1 }
    if p3 == null { error += 1 }

    if p1 != null && p1.* != 100 { error += 1 }
    if p2 != null && p2.* != 200 { error += 1 }
    if p3 != null && p3.* != 300 { error += 1 }

    // Missing key returns null
    p4 := HashMap.get(m, 999)
    if p4 != null { error += 1 }

    HashMap.destroy(m)
    error
}

test_get_copy() {
    error := 0
    m := HashMap.create[Int, Int]()
    HashMap.set(m, 42, 84)

    opt := HashMap.get_copy(m, 42)
    when opt {
        s: Some { if s.v != 84 { error += 1 } }
        n: None { error += 1 }
    }

    opt2 := HashMap.get_copy(m, 999)
    when opt2 {
        s: Some { error += 1 }
        n: None { }
    }

    HashMap.destroy(m)
    error
}

test_contains() {
    error := 0
    m := HashMap.create[Int, Int]()
    HashMap.set(m, 10, 1)
    HashMap.set(m, 20, 2)

    if !HashMap.contains(m, 10) { error += 1 }
    if !HashMap.contains(m, 20) { error += 1 }
    if HashMap.contains(m, 30)  { error += 1 }

    HashMap.destroy(m)
    error
}

test_overwrite() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 100)
    if HashMap.size(m) != 1u { error += 1 }

    HashMap.set(m, 1, 200)
    if HashMap.size(m) != 1u { error += 1 }

    p := HashMap.get(m, 1)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    HashMap.destroy(m)
    error
}

test_remove() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 10)
    HashMap.set(m, 2, 20)
    HashMap.set(m, 3, 30)

    if !HashMap.remove(m, 2) { error += 1 }
    if HashMap.size(m) != 2u { error += 1 }
    if HashMap.contains(m, 2) { error += 1 }

    // Remaining keys still accessible
    if !HashMap.contains(m, 1) { error += 1 }
    if !HashMap.contains(m, 3) { error += 1 }

    p1 := HashMap.get(m, 1)
    p3 := HashMap.get(m, 3)
    if p1 != null && p1.* != 10 { error += 1 }
    if p3 != null && p3.* != 30 { error += 1 }

    // Remove non-existent key
    if HashMap.remove(m, 999) { error += 1 }

    // Remove remaining keys
    if !HashMap.remove(m, 1) { error += 1 }
    if !HashMap.remove(m, 3) { error += 1 }
    if HashMap.size(m) != 0u { error += 1 }

    HashMap.destroy(m)
    error
}

test_grow() {
    error := 0
    m := HashMap.create[Int, Int]()

    // Insert enough entries to trigger multiple resizes (default cap = 16, grows at 85%)
    n := 200
    i := 0
    while i < n, i += 1 {
        HashMap.set(m, i, i * 10)
    }

    if HashMap.size(m) != 200u { error += 1 }

    // Verify all entries survived growth
    i = 0
    while i < n, i += 1 {
        p := HashMap.get(m, i)
        if p == null { error += 1 }
        if p != null && p.* != i * 10 { error += 1 }
    }

    HashMap.destroy(m)
    error
}

test_clear() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 10)
    HashMap.set(m, 2, 20)
    HashMap.set(m, 3, 30)

    HashMap.clear(m)
    if HashMap.size(m) != 0u { error += 1 }
    if !HashMap.is_empty(m)  { error += 1 }
    if HashMap.contains(m, 1) { error += 1 }

    // Can still insert after clear
    HashMap.set(m, 4, 40)
    if HashMap.size(m) != 1u { error += 1 }
    p := HashMap.get(m, 4)
    if p == null { error += 1 }
    if p != null && p.* != 40 { error += 1 }

    HashMap.destroy(m)
    error
}

test_keys_values() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 10)
    HashMap.set(m, 2, 20)
    HashMap.set(m, 3, 30)

    ks := HashMap.keys(m)
    vs := HashMap.values(m)

    if ks.size != 3u { error += 1 }
    if vs.size != 3u { error += 1 }

    // Check that each key and value is present (order is not guaranteed)
    found_1 := false
    found_2 := false
    found_3 := false
    i: UInt := 0u
    while i < ks.size, i += 1u {
        if ks.v.[i] == 1 { found_1 = true }
        if ks.v.[i] == 2 { found_2 = true }
        if ks.v.[i] == 3 { found_3 = true }
    }
    if !found_1 { error += 1 }
    if !found_2 { error += 1 }
    if !found_3 { error += 1 }

    found_10 := false
    found_20 := false
    found_30 := false
    i = 0u
    while i < vs.size, i += 1u {
        if vs.v.[i] == 10 { found_10 = true }
        if vs.v.[i] == 20 { found_20 = true }
        if vs.v.[i] == 30 { found_30 = true }
    }
    if !found_10 { error += 1 }
    if !found_20 { error += 1 }
    if !found_30 { error += 1 }

    Array.free(ks.&)
    Array.free(vs.&)
    HashMap.destroy(m)
    error
}

test_empty_map() {
    error := 0
    m := HashMap.create[Int, Int]()

    if HashMap.get(m, 1) != null { error += 1 }
    if HashMap.contains(m, 1)     { error += 1 }
    if HashMap.remove(m, 1)       { error += 1 }
    if HashMap.size(m) != 0u      { error += 1 }
    if !HashMap.is_empty(m)       { error += 1 }

    opt := HashMap.get_copy(m, 1)
    when opt {
        s: Some { error += 1 }
        n: None { }
    }

    ks := HashMap.keys(m)
    vs := HashMap.values(m)
    if ks.size != 0u { error += 1 }
    if vs.size != 0u { error += 1 }

    Array.free(ks.&)
    Array.free(vs.&)
    HashMap.destroy(m)
    error
}

// Custom hash/eq for testing
_custom_hash(key: Ptr[Int]) -> UInt {
    k: UInt := key.*
    k
}

_custom_eq(a: Ptr[Int], b: Ptr[Int]) -> Bool {
    a.* == b.*
}

test_custom_hash_eq() {
    error := 0
    opts := HashMap.HashMapOpts[Int](hash = _custom_hash/1, eq = _custom_eq/2, capacity = 8u)
    m := HashMap.create[Int, Int](Alloc.context.default, opts)

    HashMap.set(m, 5, 50)
    HashMap.set(m, 10, 100)

    if HashMap.size(m) != 2u { error += 1 }

    p := HashMap.get(m, 5)
    if p == null { error += 1 }
    if p != null && p.* != 50 { error += 1 }

    p = HashMap.get(m, 10)
    if p == null { error += 1 }
    if p != null && p.* != 100 { error += 1 }

    HashMap.destroy(m)
    error
}

Point: { x: Int, y: Int }

test_struct_keys() {
    error := 0
    m := HashMap.create[Point, Int]()

    p1 := Point(x = 1, y = 2)
    p2 := Point(x = 3, y = 4)
    p3 := Point(x = 5, y = 6)

    HashMap.set(m, p1, 10)
    HashMap.set(m, p2, 20)
    HashMap.set(m, p3, 30)

    if HashMap.size(m) != 3u { error += 1 }

    v := HashMap.get(m, Point(x = 1, y = 2))
    if v == null { error += 1 }
    if v != null && v.* != 10 { error += 1 }

    v = HashMap.get(m, Point(x = 3, y = 4))
    if v == null { error += 1 }
    if v != null && v.* != 20 { error += 1 }

    // Non-existent struct key
    v = HashMap.get(m, Point(x = 7, y = 8))
    if v != null { error += 1 }

    HashMap.destroy(m)
    error
}

test_remove_and_reinsert() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 10)
    HashMap.set(m, 2, 20)
    HashMap.remove(m, 1)

    // Re-insert with same key, different value
    HashMap.set(m, 1, 99)
    if HashMap.size(m) != 2u { error += 1 }

    p := HashMap.get(m, 1)
    if p == null { error += 1 }
    if p != null && p.* != 99 { error += 1 }

    HashMap.destroy(m)
    error
}

// Hash that maps everything to one of 4 buckets — forces heavy collisions,
// Robin Hood displacement, and backward-shift deletion chains.
_colliding_hash(key: Ptr[Int]) -> UInt {
    k: UInt := key.*
    k & 3u
}

test_collision_stress() {
    error := 0
    opts := HashMap.HashMapOpts[Int](hash = _colliding_hash/1, eq = _custom_eq/2, capacity = 8u)
    m := HashMap.create[Int, Int](Alloc.context.default, opts)

    // Insert 32 entries — all land in buckets 0-3, forces Robin Hood swaps
    n := 32
    i := 0
    while i < n, i += 1 {
        HashMap.set(m, i, i * 7)
    }
    if HashMap.size(m) != 32u { error += 1 }

    // Verify all entries are accessible
    i = 0
    while i < n, i += 1 {
        p := HashMap.get(m, i)
        if p == null { error += 1 }
        if p != null && p.* != i * 7 { error += 1 }
    }

    // Overwrite in collision-heavy map
    HashMap.set(m, 4, 999)
    if HashMap.size(m) != 32u { error += 1 }
    p := HashMap.get(m, 4)
    if p == null { error += 1 }
    if p != null && p.* != 999 { error += 1 }

    // Remove entries from the middle of collision chains
    i = 0
    while i < 16, i += 1 {
        if !HashMap.remove(m, i) { error += 1 }
    }
    if HashMap.size(m) != 16u { error += 1 }

    // Remaining entries (16..31) must still be accessible after backward-shift
    i = 16
    while i < n, i += 1 {
        p2 := HashMap.get(m, i)
        if p2 == null { error += 1 }
        if p2 != null && p2.* != i * 7 { error += 1 }
    }

    // Removed entries must be gone
    i = 0
    while i < 16, i += 1 {
        if HashMap.contains(m, i) { error += 1 }
    }

    HashMap.destroy(m)
    error
}

test_large_remove() {
    error := 0
    m := HashMap.create[Int, Int]()

    // Insert 200, remove all, verify empty
    n := 200
    i := 0
    while i < n, i += 1 {
        HashMap.set(m, i, i)
    }
    if HashMap.size(m) != 200u { error += 1 }

    i = 0
    while i < n, i += 1 {
        if !HashMap.remove(m, i) { error += 1 }
    }
    if HashMap.size(m) != 0u { error += 1 }
    if !HashMap.is_empty(m) { error += 1 }

    // Re-insert after mass removal
    i = 0
    while i < 50, i += 1 {
        HashMap.set(m, i, i * 3)
    }
    if HashMap.size(m) != 50u { error += 1 }
    i = 0
    while i < 50, i += 1 {
        p := HashMap.get(m, i)
        if p == null { error += 1 }
        if p != null && p.* != i * 3 { error += 1 }
    }

    HashMap.destroy(m)
    error
}

test_zero_and_negative_keys() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 0, 100)
    HashMap.set(m, -1, 200)
    HashMap.set(m, -999, 300)

    if HashMap.size(m) != 3u { error += 1 }

    p := HashMap.get(m, 0)
    if p == null { error += 1 }
    if p != null && p.* != 100 { error += 1 }

    p = HashMap.get(m, -1)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    p = HashMap.get(m, -999)
    if p == null { error += 1 }
    if p != null && p.* != 300 { error += 1 }

    // Remove key 0
    if !HashMap.remove(m, 0) { error += 1 }
    if HashMap.contains(m, 0) { error += 1 }
    if HashMap.size(m) != 2u { error += 1 }

    HashMap.destroy(m)
    error
}

test_struct_values() {
    error := 0
    m := HashMap.create[Int, Point]()

    HashMap.set(m, 1, Point(x = 10, y = 20))
    HashMap.set(m, 2, Point(x = 30, y = 40))
    HashMap.set(m, 3, Point(x = 50, y = 60))

    if HashMap.size(m) != 3u { error += 1 }

    p := HashMap.get(m, 1)
    if p == null { error += 1 }
    if p != null && (p->x != 10 || p->y != 20) { error += 1 }

    p = HashMap.get(m, 2)
    if p == null { error += 1 }
    if p != null && (p->x != 30 || p->y != 40) { error += 1 }

    // Overwrite with different struct value
    HashMap.set(m, 1, Point(x = 99, y = 88))
    if HashMap.size(m) != 3u { error += 1 }
    p = HashMap.get(m, 1)
    if p == null { error += 1 }
    if p != null && (p->x != 99 || p->y != 88) { error += 1 }

    HashMap.destroy(m)
    error
}

test_create_with_alloc() {
    error := 0
    m := HashMap.create[Int, Int](Alloc.context.default)

    HashMap.set(m, 1, 10)
    HashMap.set(m, 2, 20)

    if HashMap.size(m) != 2u { error += 1 }
    p := HashMap.get(m, 1)
    if p == null { error += 1 }
    if p != null && p.* != 10 { error += 1 }

    HashMap.destroy(m)
    error
}

test_keys_values_with_alloc() {
    error := 0
    m := HashMap.create[Int, Int]()

    HashMap.set(m, 1, 10)
    HashMap.set(m, 2, 20)

    alloc := Alloc.context.default
    ks := HashMap.keys(m, alloc)
    vs := HashMap.values(m, alloc)

    if ks.size != 2u { error += 1 }
    if vs.size != 2u { error += 1 }

    found_1 := false
    found_2 := false
    i: UInt := 0u
    while i < ks.size, i += 1u {
        if ks.v.[i] == 1 { found_1 = true }
        if ks.v.[i] == 2 { found_2 = true }
    }
    if !found_1 { error += 1 }
    if !found_2 { error += 1 }

    found_10 := false
    found_20 := false
    i = 0u
    while i < vs.size, i += 1u {
        if vs.v.[i] == 10 { found_10 = true }
        if vs.v.[i] == 20 { found_20 = true }
    }
    if !found_10 { error += 1 }
    if !found_20 { error += 1 }

    Array.free(ks.&)
    Array.free(vs.&)
    HashMap.destroy(m)
    error
}

test_remove_then_grow() {
    error := 0
    m := HashMap.create[Int, Int]()

    // Fill up, remove half, then insert enough to trigger growth
    i := 0
    while i < 100, i += 1 {
        HashMap.set(m, i, i)
    }

    i = 0
    while i < 50, i += 1 {
        HashMap.remove(m, i)
    }
    if HashMap.size(m) != 50u { error += 1 }

    // Insert 200 more to trigger growth from a partially-removed state
    i = 1000
    while i < 1200, i += 1 {
        HashMap.set(m, i, i * 2)
    }
    if HashMap.size(m) != 250u { error += 1 }

    // Verify surviving original entries
    i = 50
    while i < 100, i += 1 {
        p := HashMap.get(m, i)
        if p == null { error += 1 }
        if p != null && p.* != i { error += 1 }
    }

    // Verify new entries
    i = 1000
    while i < 1200, i += 1 {
        p := HashMap.get(m, i)
        if p == null { error += 1 }
        if p != null && p.* != i * 2 { error += 1 }
    }

    // Removed entries still gone
    i = 0
    while i < 50, i += 1 {
        if HashMap.contains(m, i) { error += 1 }
    }

    HashMap.destroy(m)
    error
}

main() {
    error := 0
    error += test_create_destroy()
    error += test_set_get()
    error += test_get_copy()
    error += test_contains()
    error += test_overwrite()
    error += test_remove()
    error += test_grow()
    error += test_clear()
    error += test_keys_values()
    error += test_empty_map()
    error += test_custom_hash_eq()
    error += test_struct_keys()
    error += test_remove_and_reinsert()
    error += test_collision_stress()
    error += test_large_remove()
    error += test_zero_and_negative_keys()
    error += test_struct_values()
    error += test_create_with_alloc()
    error += test_keys_values_with_alloc()
    error += test_remove_then_grow()
    error
}
