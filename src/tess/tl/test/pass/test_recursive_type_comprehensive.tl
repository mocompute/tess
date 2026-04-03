#module Foo

A: { b: Ptr[B], val: Int }
B: { c: Ptr[C]           }
C: { a: Ptr[A]           }


#module main
#import <cstdlib.tl>
#import <Array.tl>

// --- from test_recursive_type ---

Point1: { x: Int, y: Int, next: Ptr[Point1] }

make_points1(count: CSize) { Array.with_capacity[Point1](count) }

test_recursive_type() {
    points := make_points1(8zu)

    Array.push(points.&, Point1(0, 1, null))
    Array.push(points.&, Point1(2, 3, points.data.[0].&))

    res := points.data.[0].x
    if (points.data.[0].x != 0) { res = 1 }
    if (points.data.[0].y != 1) { res = 1 }
    if (points.data.[1].x != 2) { res = 1 }
    if (points.data.[1].y != 3) { res = 1 }
    if (points.data.[1].next->x != 0) { res = 1 }

    Array.free(points.&)
    result: CInt := res
    result
}

// --- from test_recursive_type_basic ---

Point2[a]: { x: a, y: a, next: Ptr[Point2[a]] }

test_recursive_type_basic() {
    p1 := Point2(x = 1, y = 2, next = null)
    p2 := Point2(x = 3, y = 4, next = p1.&)

    result: CInt := p2.next->x - 1
    result
}

// --- from test_recursive_type_cycle_3 ---

test_recursive_type_cycle_3() {
    a := Foo.A(b = null, val = 999)
    b := Foo.B(c = null)
    c := Foo.C(a = a.&)
    a.b            = b.&
    b.c            = c.&
    a.b->c->a->val = 0
    result: CInt := a.b->c->a->val
    result
}

// --- from test_recursive_type_generic ---

Point4[a]: { x: a, y: a, next: Ptr[Point4[a]] }

make_points4(count: CSize) { Array.with_capacity[Point4[CInt]](count) }

test_recursive_type_generic() {
    points := make_points4(8zu)

    Array.push(points.&, Point4(x = 0, y = 1, next = null))
    Array.push(points.&, Point4(x = 2, y = 3, next = points.data.[0].&))

    res := points.data.[0].x
    if (points.data.[1].next->x != 0) { res = 1 }

    Array.free(points.&)
    res
}

// --- from test_recursive_type_mutual ---

Tree[a]:   { value: a, children: Ptr[Forest[a]] }
Forest[a]: { trees: Array.Array[Tree[a]]        }

test_recursive_type_mutual() {
    t1     := Tree[Int](value = 1, children = null)
    forest := Forest[Int](trees = Array.with_capacity[Tree[Int]](16zu))
    t1.children = forest.&
    0
}

// --- from test_recursive_type_mutual_simple ---

MutFoo[a]: { value: a, other: Ptr[MutBar[a]] }
MutBar[a]: { value: a, other: Ptr[MutFoo[a]] }

identity[T](x: T) -> T { x }

test_recursive_type_mutual_simple() {
    f := MutFoo[Int](value = 1, other = null)
    w := identity[MutFoo[Int]](f)
    0
}

// --- main ---

main() {
    res: CInt := 0
    res = res + test_recursive_type()
    res = res + test_recursive_type_basic()
    res = res + test_recursive_type_cycle_3()
    res = res + test_recursive_type_generic()
    res = res + test_recursive_type_mutual()
    res = res + test_recursive_type_mutual_simple()
    res
}
