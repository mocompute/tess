#module main
f1() { 1 }
f2() { 2 }

Ctx[t1, t2]: {
    p1: t1, p2: t2
}

init() {
    Ctx ( p1 = f1/0, p2 = f2/0 )
}

main() {
    ctx := init()
    p   := ctx.p1                       // See test_function_pointer_in_struct_direct to avoid this temp assignment
    p() - 1
}
