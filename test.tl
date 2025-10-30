id(x) { x }
// fake_id(x) { x + 1 }

apply(f, x) { f(x) }

main() {
  f1 = id
  f1 = id
  apply(f1, 0)
}


// f1() { 1 }
// f2() { 2 }

// Ctx(t1) : {
//   p1 : t1, p2 : t1
// }

// init() {
//   Ctx { p1 = f1, p2 = f2 }
// }

// main() {
//   ctx = init()
//   ctx.p1() - 1
// }


// id(x) { x }
// apply(f, x) { f(x) }

// main() {
//   f1 = id
//   apply(f1, 0)
// }



// f1() { 1 }
// f2() { 2 }

// Ctx(t1) : {
//   p1 : t1, p2 : t1
// }

// init() {
//   Ctx { p1 = f1, p2 = f2 }
// }

// main() {
//   ctx = init()
//   ctx.p1() - 1
// }
