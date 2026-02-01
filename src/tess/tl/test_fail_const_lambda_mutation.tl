#module main

main() {
    g := (p: Ptr(Const(Int))) { p.* = 10 0 }    // error: const violation
    0
}
