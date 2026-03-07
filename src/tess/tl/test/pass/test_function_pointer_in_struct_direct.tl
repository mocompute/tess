#module main
f1() { 1 }
f2() { 2 }

Ctx[t1]: {
    p1: t1, p2: t1
}

init() {
    Ctx ( p1 = f1/0, p2 = f2/0 )
}

main() {
    ctx := init()
    ctx.p1() - 1
}
