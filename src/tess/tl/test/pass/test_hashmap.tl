#import <Hash.tl>

#module Point

Point: { x: Int, y: Int }

hash(p: Point) -> CSize {
    hx := p.x.hash()
    hy := p.y.hash()
    hx ^ (hy * 0x100000001b3zu)
}

eq(a: Point, b: Point) -> Bool {
    a.x == b.x && a.y == b.y
}

#module CollidingKey

CollidingKey: { v: Int }

hash(k: CollidingKey) -> CSize {
    v: CSize := k.v
    v & 3zu
}

eq(a: CollidingKey, b: CollidingKey) -> Bool {
    a.v == b.v
}

#module main
#import <HashMap.tl>
#import <Alloc.tl>
#import <Array.tl>
#import <cstdio.tl>

Point        = Point.Point
CollidingKey = CollidingKey.CollidingKey

test_create_destroy() {
    error := 0
    m     := HashMap.create[Int, Int]()
    if m.size() != 0zu { error += 1 }
    if !m.is_empty() { error += 1 }
    m.destroy()
    error
}

test_set_get() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 100)
    m.set(2, 200)
    m.set(3, 300)

    if m.size() != 3zu { error += 1 }

    p1 := m.get(1)
    p2 := m.get(2)
    p3 := m.get(3)

    if p1 == null { error += 1 }
    if p2 == null { error += 1 }
    if p3 == null { error += 1 }

    if p1 != null && p1.* != 100 { error += 1 }
    if p2 != null && p2.* != 200 { error += 1 }
    if p3 != null && p3.* != 300 { error += 1 }

    // Missing key returns null
    p4 := m.get(999)
    if p4 != null { error += 1 }

    m.destroy()
    error
}

test_get_copy() {
    error := 0
    m     := HashMap.create[Int, Int]()
    m.set(42, 84)

    opt := m.get_copy(42)
    when opt {
        s: Some { if s.v != 84 { error += 1 } }
        n: None { error += 1 }
    }

    opt2 := m.get_copy(999)
    when opt2 {
        s: Some { error += 1 }
        n: None { }
    }

    m.destroy()
    error
}

test_contains() {
    error := 0
    m     := HashMap.create[Int, Int]()
    m.set(10, 1)
    m.set(20, 2)

    if !m.contains(10) { error += 1 }
    if !m.contains(20) { error += 1 }
    if m.contains(30) { error += 1 }

    m.destroy()
    error
}

test_overwrite() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 100)
    if m.size() != 1zu { error += 1 }

    m.set(1, 200)
    if m.size() != 1zu { error += 1 }

    p := m.get(1)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    m.destroy()
    error
}

test_remove() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 10)
    m.set(2, 20)
    m.set(3, 30)

    if !m.remove(2) { error += 1 }
    if m.size() != 2zu { error += 1 }
    if m.contains(2) { error += 1 }

    // Remaining keys still accessible
    if !m.contains(1) { error += 1 }
    if !m.contains(3) { error += 1 }

    p1 := m.get(1)
    p3 := m.get(3)
    if p1 != null && p1.* != 10 { error += 1 }
    if p3 != null && p3.* != 30 { error += 1 }

    // Remove non-existent key
    if m.remove(999) { error += 1 }

    // Remove remaining keys
    if !m.remove(1) { error += 1 }
    if !m.remove(3) { error += 1 }
    if m.size() != 0zu { error += 1 }

    m.destroy()
    error
}

test_grow() {
    error := 0
    m     := HashMap.create[Int, Int]()

    // Insert enough entries to trigger multiple resizes (default cap = 16, grows at 85%)
    n := 200
    i := 0
    while i < n, i += 1 {
        m.set(i, i * 10)
    }

    if m.size() != 200zu { error += 1 }

    // Verify all entries survived growth
    i = 0
    while i < n, i += 1 {
        p := m.get(i)
        if p == null { error += 1 }
        if p != null && p.* != i * 10 { error += 1 }
    }

    m.destroy()
    error
}

test_clear() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 10)
    m.set(2, 20)
    m.set(3, 30)

    m.clear()
    if m.size() != 0zu { error += 1 }
    if !m.is_empty() { error += 1 }
    if m.contains(1) { error += 1 }

    // Can still insert after clear
    m.set(4, 40)
    if m.size() != 1zu { error += 1 }
    p := m.get(4)
    if p == null { error += 1 }
    if p != null && p.* != 40 { error += 1 }

    m.destroy()
    error
}

test_keys_values() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 10)
    m.set(2, 20)
    m.set(3, 30)

    ks := m.keys()
    vs := m.values()

    if ks.size != 3zu { error += 1 }
    if vs.size != 3zu { error += 1 }

    // Check that each key and value is present (order is not guaranteed)
    found_1  := false
    found_2  := false
    found_3  := false
    i: CSize := 0zu
    while i < ks.size, i += 1zu {
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
    i = 0zu
    while i < vs.size, i += 1zu {
        if vs.v.[i] == 10 { found_10 = true }
        if vs.v.[i] == 20 { found_20 = true }
        if vs.v.[i] == 30 { found_30 = true }
    }
    if !found_10 { error += 1 }
    if !found_20 { error += 1 }
    if !found_30 { error += 1 }

    Array.free(ks.&)
    Array.free(vs.&)
    m.destroy()
    error
}

test_empty_map() {
    error := 0
    m     := HashMap.create[Int, Int]()

    if m.get(1) != null { error += 1 }
    if m.contains(1) { error += 1 }
    if m.remove(1) { error += 1 }
    if m.size() != 0zu { error += 1 }
    if !m.is_empty() { error += 1 }

    opt := m.get_copy(1)
    when opt {
        s: Some { error += 1 }
        n: None { }
    }

    ks := m.keys()
    vs := m.values()
    if ks.size != 0zu { error += 1 }
    if vs.size != 0zu { error += 1 }

    Array.free(ks.&)
    Array.free(vs.&)
    m.destroy()
    error
}

test_struct_keys() {
    error := 0
    m     := HashMap.create[Point, Int]()

    p1 := Point(x = 1, y = 2)
    p2 := Point(x = 3, y = 4)
    p3 := Point(x = 5, y = 6)

    m.set(p1, 10)
    m.set(p2, 20)
    m.set(p3, 30)

    if m.size() != 3zu { error += 1 }

    v := m.get(Point(x = 1, y = 2))
    if v == null { error += 1 }
    if v != null && v.* != 10 { error += 1 }

    v = m.get(Point(x = 3, y = 4))
    if v == null { error += 1 }
    if v != null && v.* != 20 { error += 1 }

    // Non-existent struct key
    v = m.get(Point(x = 7, y = 8))
    if v != null { error += 1 }

    m.destroy()
    error
}

test_remove_and_reinsert() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 10)
    m.set(2, 20)
    m.remove(1)

    // Re-insert with same key, different value
    m.set(1, 99)
    if m.size() != 2zu { error += 1 }

    p := m.get(1)
    if p == null { error += 1 }
    if p != null && p.* != 99 { error += 1 }

    m.destroy()
    error
}

test_collision_stress() {
    error := 0
    opts  := HashMap.HashMapOpts(capacity = 8zu)
    m     := HashMap.create[CollidingKey, Int](Alloc.context.default, opts)

    // Insert 32 entries — all land in buckets 0-3, forces Robin Hood swaps
    n := 32
    i := 0
    while i < n, i += 1 {
        m.set(CollidingKey(v = i), i * 7)
    }
    if m.size() != 32zu { error += 1 }

    // Verify all entries are accessible
    i = 0
    while i < n, i += 1 {
        p := m.get(CollidingKey(v = i))
        if p == null { error += 1 }
        if p != null && p.* != i * 7 { error += 1 }
    }

    // Overwrite in collision-heavy map
    m.set(CollidingKey(v = 4), 999)
    if m.size() != 32zu { error += 1 }
    p := m.get(CollidingKey(v = 4))
    if p == null { error += 1 }
    if p != null && p.* != 999 { error += 1 }

    // Remove entries from the middle of collision chains
    i = 0
    while i < 16, i += 1 {
        if !m.remove(CollidingKey(v = i)) { error += 1 }
    }
    if m.size() != 16zu { error += 1 }

    // Remaining entries (16..31) must still be accessible after backward-shift
    i = 16
    while i < n, i += 1 {
        p2 := m.get(CollidingKey(v = i))
        if p2 == null { error += 1 }
        if p2 != null && p2.* != i * 7 { error += 1 }
    }

    // Removed entries must be gone
    i = 0
    while i < 16, i += 1 {
        if m.contains(CollidingKey(v = i)) { error += 1 }
    }

    m.destroy()
    error
}

test_large_remove() {
    error := 0
    m     := HashMap.create[Int, Int]()

    // Insert 200, remove all, verify empty
    n := 200
    i := 0
    while i < n, i += 1 {
        m.set(i, i)
    }
    if m.size() != 200zu { error += 1 }

    i = 0
    while i < n, i += 1 {
        if !m.remove(i) { error += 1 }
    }
    if m.size() != 0zu { error += 1 }
    if !m.is_empty() { error += 1 }

    // Re-insert after mass removal
    i = 0
    while i < 50, i += 1 {
        m.set(i, i * 3)
    }
    if m.size() != 50zu { error += 1 }
    i = 0
    while i < 50, i += 1 {
        p := m.get(i)
        if p == null { error += 1 }
        if p != null && p.* != i * 3 { error += 1 }
    }

    m.destroy()
    error
}

test_zero_and_negative_keys() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(0, 100)
    m.set(-1, 200)
    m.set(-999, 300)

    if m.size() != 3zu { error += 1 }

    p := m.get(0)
    if p == null { error += 1 }
    if p != null && p.* != 100 { error += 1 }

    p = m.get(-1)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    p = m.get(-999)
    if p == null { error += 1 }
    if p != null && p.* != 300 { error += 1 }

    // Remove key 0
    if !m.remove(0) { error += 1 }
    if m.contains(0) { error += 1 }
    if m.size() != 2zu { error += 1 }

    m.destroy()
    error
}

test_struct_values() {
    error := 0
    m     := HashMap.create[Int, Point]()

    m.set(1, Point(x = 10, y = 20))
    m.set(2, Point(x = 30, y = 40))
    m.set(3, Point(x = 50, y = 60))

    if m.size() != 3zu { error += 1 }

    p := m.get(1)
    if p == null { error += 1 }
    if p != null && (p->x != 10 || p->y != 20) { error += 1 }

    p = m.get(2)
    if p == null { error += 1 }
    if p != null && (p->x != 30 || p->y != 40) { error += 1 }

    // Overwrite with different struct value
    m.set(1, Point(x = 99, y = 88))
    if m.size() != 3zu { error += 1 }
    p = m.get(1)
    if p == null { error += 1 }
    if p != null && (p->x != 99 || p->y != 88) { error += 1 }

    m.destroy()
    error
}

test_create_with_alloc() {
    error := 0
    m     := HashMap.create[Int, Int](Alloc.context.default)

    m.set(1, 10)
    m.set(2, 20)

    if m.size() != 2zu { error += 1 }
    p := m.get(1)
    if p == null { error += 1 }
    if p != null && p.* != 10 { error += 1 }

    m.destroy()
    error
}

test_keys_values_with_alloc() {
    error := 0
    m     := HashMap.create[Int, Int]()

    m.set(1, 10)
    m.set(2, 20)

    alloc := Alloc.context.default
    ks    := m.keys(alloc)
    vs    := m.values(alloc)

    if ks.size != 2zu { error += 1 }
    if vs.size != 2zu { error += 1 }

    found_1  := false
    found_2  := false
    i: CSize := 0zu
    while i < ks.size, i += 1zu {
        if ks.v.[i] == 1 { found_1 = true }
        if ks.v.[i] == 2 { found_2 = true }
    }
    if !found_1 { error += 1 }
    if !found_2 { error += 1 }

    found_10 := false
    found_20 := false
    i = 0zu
    while i < vs.size, i += 1zu {
        if vs.v.[i] == 10 { found_10 = true }
        if vs.v.[i] == 20 { found_20 = true }
    }
    if !found_10 { error += 1 }
    if !found_20 { error += 1 }

    Array.free(ks.&)
    Array.free(vs.&)
    m.destroy()
    error
}

test_remove_then_grow() {
    error := 0
    m     := HashMap.create[Int, Int]()

    // Fill up, remove half, then insert enough to trigger growth
    i := 0
    while i < 100, i += 1 {
        m.set(i, i)
    }

    i = 0
    while i < 50, i += 1 {
        m.remove(i)
    }
    if m.size() != 50zu { error += 1 }

    // Insert 200 more to trigger growth from a partially-removed state
    i = 1000
    while i < 1200, i += 1 {
        m.set(i, i * 2)
    }
    if m.size() != 250zu { error += 1 }

    // Verify surviving original entries
    i = 50
    while i < 100, i += 1 {
        p := m.get(i)
        if p == null { error += 1 }
        if p != null && p.* != i { error += 1 }
    }

    // Verify new entries
    i = 1000
    while i < 1200, i += 1 {
        p := m.get(i)
        if p == null { error += 1 }
        if p != null && p.* != i * 2 { error += 1 }
    }

    // Removed entries still gone
    i = 0
    while i < 50, i += 1 {
        if m.contains(i) { error += 1 }
    }

    m.destroy()
    error
}

test_default_eq() {
    error := 0

    // Use default hash/eq (exercises _default_eq via byte-level comparison)
    m := HashMap.create[Int, Int]()

    m.set(42, 100)

    // Same key bytes must match (eq returns true)
    p := m.get(42)
    if p == null { error += 1 }
    if p != null && p.* != 100 { error += 1 }

    // Different key bytes must not match (eq returns false)
    if m.get(43) != null { error += 1 }

    // Overwrite: eq must return true for identical key so size stays 1
    m.set(42, 200)
    if m.size() != 1zu { error += 1 }
    p = m.get(42)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    // Test with struct keys: byte-equal structs must match
    m2 := HashMap.create[Point, Int]()
    m2.set(Point(x = 10, y = 20), 999)

    p2 := m2.get(Point(x = 10, y = 20))
    if p2 == null { error += 1 }
    if p2 != null && p2.* != 999 { error += 1 }

    // Structs with any different field must not match
    if m2.get(Point(x = 10, y = 21)) != null { error += 1 }
    if m2.get(Point(x = 11, y = 20)) != null { error += 1 }
    if m2.get(Point(x = 0, y = 0)) != null { error += 1 }

    m.destroy()
    m2.destroy()
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
    error += test_struct_keys()
    error += test_remove_and_reinsert()
    error += test_collision_stress()
    error += test_large_remove()
    error += test_zero_and_negative_keys()
    error += test_struct_values()
    error += test_create_with_alloc()
    error += test_keys_values_with_alloc()
    error += test_remove_then_grow()
    error += test_default_eq()
    error
}
