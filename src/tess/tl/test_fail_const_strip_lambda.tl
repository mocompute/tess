#module main

mutate(p: Ptr(Int)) {
    p.* = 10
    0
}

main() {
    f := (p: Ptr(Const(Int))) { mutate(p) }    // error: cannot strip const
    0
}
