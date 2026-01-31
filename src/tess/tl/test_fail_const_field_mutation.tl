#module main

Point: { x: Int, y: Int }

mutate_field(p: Ptr(Const(Point))) {
    p->x = 10    // error: const violation
    0
}

main() {
    p := Point(x = 1, y = 2)
    mutate_field(p.&)
}
