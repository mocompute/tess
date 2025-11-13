#module main
main() {
  x := 42
  res := cond { x == 1 { 1 } x == 42 { 0 } true { 9 } }
  res
}
