#module main

// A function expecting a non-const pointer
mutate(p: Ptr(Int)) {
    p.* = 10
    0
}

// Passing Ptr(Const(Int)) where Ptr(Int) is expected should fail
pass_const(p: Ptr(Const(Int))) {
    mutate(p)    // error: cannot strip const
}

main() {
    x := 42
    pass_const(x.&)
}
