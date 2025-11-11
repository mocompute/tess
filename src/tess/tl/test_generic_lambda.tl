#module main
main() {
  f := (a, b) { a + b }
  x1 := f(1, 2)
  x2 := f(1.0, 0.5)
  res := f(-1, 1)
  res
}
