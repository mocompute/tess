#module main

t_int() {
  x := 1
  x :: Int
}

t_float() {
  x := 1.5
  x :: Float
}

main() {
  t_int()
  t_float()
  0
}
