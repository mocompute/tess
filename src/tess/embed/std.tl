_tl_sizeof_ (x : a)        -> Int

PtrOrNull = Union(Ptr(a), Nil)
c_malloc    (count : Int)  -> PtrOrNull(a)
c_free      (ptr : Ptr) -> Nil

is_null     (p : PtrOrNull(a)) -> Bool
is_ptr      (p : PtrOrNull(a)) -> Bool
ptr         (p : PtrOrNull(a)) -> Ptr(a)

sizeof      (T) { _tl_sizeof_(T) }       // T may be Type (literal) or concrete

is_null(p) {
  if p == null { true } else { false }
}
