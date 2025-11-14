#module main

main() {
  n := 0.5
  res := case n {
    0    { 1 }
    1    { 1 }
    0.5  { 0 }
    3    { 3 }
  }
  res
}
