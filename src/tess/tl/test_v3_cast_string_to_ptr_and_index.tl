#module main

main() {
  s := "hello, world"
  c : Ptr(CChar) := s
  c[1] - 101 // 101 is 'e' in ascii
}
