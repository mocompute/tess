#module main
#import <stdio.tl>
#import <stdlib.tl>

Point: { x: Int, y: Int }

read_through_const_ptr(p: Ptr[Const[Int]]) {
    p.*
}

add_one_through_const(p: Ptr[Const[Int]]) {
    p.* + 1
}

// Struct field access through const pointer
read_point(p: Ptr[Const[Point]]) {
    p->x + p->y
}

main() {
    // Basic: const pointer to Int
    p: Ptr[Int] := c_malloc(8zu)
    p.* = 42

    // Implicit coercion: Ptr[Int] -> Ptr[Const[Int]]
    val := read_through_const_ptr(p)
    c_printf(c"val = %d\n", val)

    val2 := add_one_through_const(p)
    c_printf(c"val2 = %d\n", val2)

    c_free(p)

    // Struct: const pointer to Point
    pt: Ptr[Point] := c_malloc(sizeof[Point]())
    pt->x = 10
    pt->y = 20
    sum := read_point(pt)
    c_printf(c"sum = %d\n", sum)

    c_free(pt)
    0
}
