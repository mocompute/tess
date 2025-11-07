#module main

main() {
  s := "hello, world"

  // a special case allows conversion between String and Ptr(CChar)
  c : Ptr(CChar) := s
  c.* - 104 // 104 is 'h' in ascii
}
