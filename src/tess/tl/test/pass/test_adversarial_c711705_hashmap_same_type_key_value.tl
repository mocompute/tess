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

#module main
#import <HashMap.tl>
#import <Alloc.tl>

main() {
    error := 0

    // HashMap[Point, Point] — auto-collapse must resolve Point in both key and value
    m := HashMap.create[Point, Point]()

    p1 := Point(x = 1, y = 2)
    p2 := Point(x = 3, y = 4)
    p3 := Point(x = 5, y = 6)

    v1 := Point(x = 10, y = 20)
    v2 := Point(x = 30, y = 40)
    v3 := Point(x = 50, y = 60)

    m.set(p1, v1)
    m.set(p2, v2)
    m.set(p3, v3)

    // Verify size
    error += if m.size() == 3zu { 0 } else { 1 }

    // Retrieve by key and verify value fields
    r := m.get(Point(x = 1, y = 2))
    error += if r != null && r->x == 10 && r->y == 20 { 0 } else { 1 }

    r = m.get(Point(x = 3, y = 4))
    error += if r != null && r->x == 30 && r->y == 40 { 0 } else { 1 }

    r = m.get(Point(x = 5, y = 6))
    error += if r != null && r->x == 50 && r->y == 60 { 0 } else { 1 }

    // Overwrite: same key, different value
    m.set(p1, Point(x = 99, y = 88))
    error += if m.size() == 3zu { 0 } else { 1 }
    r = m.get(p1)
    error += if r != null && r->x == 99 && r->y == 88 { 0 } else { 1 }

    // Non-existent key
    r = m.get(Point(x = 0, y = 0))
    error += if r == null { 0 } else { 1 }

    // Remove and verify
    error += if m.remove(p2) { 0 } else { 1 }
    error += if m.size() == 2zu { 0 } else { 1 }
    error += if !m.contains(p2) { 0 } else { 1 }

    m.destroy()
    error
}
