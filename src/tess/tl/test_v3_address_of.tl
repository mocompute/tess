main() {
  obj := 1
  ptr := obj.&
  ptr.* = 0
  obj
}
