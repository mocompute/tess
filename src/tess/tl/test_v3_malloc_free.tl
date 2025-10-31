c_malloc(sz : Int) -> Ptr(a)
c_free(p : Ptr(a)) -> Nil

main() {
  p := c_malloc(64)
  c_free(p)
  0
}
