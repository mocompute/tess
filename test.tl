#module main
PtrOrNull2 = Union(Ptr(a), Nil)
c_malloc    (count : Int)  -> PtrOrNull2(a)
c_free      (ptr : Ptr) -> Nil

main() {
  p := c_malloc(64)
  c_free(p)
  0
}
