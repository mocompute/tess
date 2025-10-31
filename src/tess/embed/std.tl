_tl_sizeof_ (x : a)        -> Int

c_malloc    (count : Int)  -> PtrOrNull(a)
c_free      (ptr : Ptr) -> Nil

is_null     (p : PtrOrNull(a)) -> Bool
is_ptr      (p : PtrOrNull(a)) -> Bool
ptr         (p : PtrOrNull(a)) -> Ptr(a)

sizeof      (T : Type) { _tl_sizeof_(T) }
