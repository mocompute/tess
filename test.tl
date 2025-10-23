apply_nil(f) { f() }

main() {
  a = 0
  l = () { a }
  apply_nil(l)
}


// add(l, r) { l + r }

// main() {
//   add(-1, 1)
// }
