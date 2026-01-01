#module main

main() {
  n := 0.5
  // Note: this case expression should cause a type error because all conditions are not the same type.
  res := case n {
    0    { 1 }
    1    { 1 }
    0.5  { 0 }
    3    { 3 }
  }
  res
}
