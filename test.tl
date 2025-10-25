c_malloc(sz : Int) -> Ptr(a)
c_free(p : Ptr(a)) -> Nil

main() {
  p : Ptr(Int) = c_malloc(64)
  p[0]
  return 0
}
