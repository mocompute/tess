f1() { 1 }
f2() { 2 }

Ctx(t1, t2) : {
  p1 : t1, p2 : t2
}

init() {
  Ctx { p1 = f1, p2 = f2 }
}

main() {
  ctx = init()
  p = ctx.p1  // FIXME: currently must extract function ptr before applying it
  p() - 1
}
