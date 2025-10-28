// let main () =
//   let f = fun a b -> add a b end in
//   let x1 = f 1 2 in
//   let x2 = f 1.0 0.5 in
//   let res = f 0 0 in
//   res

main() {
  f = (a, b) { a + b }
  x1 = f(1, 2)
  x2 = f(1.0, 0.5)
  res = f(-1, 1)
  res
}
