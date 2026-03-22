#module main
#import <cstdio.tl>
#import <cstdlib.tl>
#import <Array.tl>

Point: { x: Int, y: Int }

read_point(p) {
    p.x + p.y
}

// Read-only method taking Ptr[Const[Point]]
point_sum(p: Ptr[Const[Point]]) {
    p->x + p->y
}

main() {
    // Const struct: can read fields
    p: Const[Point] := Point(x = 10, y = 20)
    c_printf(c"sum = %lld\n", read_point(p))

    // Const pointer binding: can mutate pointee, can't reassign pointer
    raw: Ptr[Int] := c_malloc(8zu)
    raw.* = 42
    cp: Const[Ptr[Int]] := raw
    cp.* = 99
    c_printf(c"val = %d\n", cp.*)

    // Combined: Const[Ptr[Const[Int]]] — can't reassign, can't mutate pointee, can read
    cpc: Const[Ptr[Const[Int]]] := raw
    c_printf(c"cpc = %d\n", cpc.*)

    c_free(raw)

    // UFCS read-only method on const binding
    arr: Const[Array[Int]] := Array.empty[Int]()
    c_printf(c"size = %zu\n", arr.size)

    0
}
