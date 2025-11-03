#module main
PtrOrNull = Union(Ptr(a), Nil)
c_malloc    (count : Int)  -> PtrOrNull(a)
c_free      (ptr : Ptr) -> Nil

main() {
  p := c_malloc(64)
  c_free(p)
  0
}
