#module main

mutate_const(p: Ptr[Const[Int]]) {
    p.* = 10    // error: const violation
    0
}

main() {
    x := 42
    mutate_const(x.&)
}
