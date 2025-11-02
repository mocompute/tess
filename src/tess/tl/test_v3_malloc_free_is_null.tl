#module main
// This test exercises the Union type PtrOrNull


c_malloc    (count : Int)  -> PtrOrNull(a)
c_free      (ptr : Ptr) -> Nil

is_null     (p : PtrOrNull(a)) -> Bool

is_null(p) {
  if p == null { true } else { false }
}


main() {
  p := c_malloc(16)
  res := if is_null(p) { 1 } else { 0 }
  c_free(p)
  res
}
