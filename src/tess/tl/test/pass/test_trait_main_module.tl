// Trait methods defined in #module main are now discoverable by trait dispatch.
// Previously, `hash(p: Point)` and `eq(a: Point, b: Point)` had to live in a
// dedicated `#module Point` for HashMap[Point, _] to satisfy K: HashEq.
//
// CAVEAT: this only works for ONE type per trait method name. Adding a second
// type with its own `hash(v: Vec3)` in main would collide on `hash__1` (Tess
// has no overloading on parameter type) and produce `function already defined`.
// See test/fail/test_fail_duplicate_function_in_main.tl for that case. For
// programs with multiple user types, define each type's trait impls in a
// dedicated module (e.g. `#module Point`, `#module Vec3`).

#module main
#import <Hash.tl>
#import <HashMap.tl>

Point: { x: Int, y: Int }

hash(p: Point) -> CSize {
    Hash.fnv1a(p.&, sizeof(p))
}

eq(a: Point, b: Point) -> Bool {
    a.x == b.x && a.y == b.y
}

main() {
    error := 0

    // Direct UFCS dispatch — already worked before the fix.
    p := Point(x = 3, y = 4)
    q := Point(x = 3, y = 4)
    error += if p == q { 0 } else { 1 }

    // Trait-bound generic via HashMap — needs find_overload_func to discover
    // the bare-named eq/hash impls in main.
    m := HashMap.create[Point, Int]()
    m.set(p, 100)
    error += if m.contains(q) { 0 } else { 1 }
    error += if m.size() == 1zu { 0 } else { 1 }

    r := Point(x = 5, y = 6)
    error += if m.contains(r) { 1 } else { 0 }

    m.destroy()
    error
}
