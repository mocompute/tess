PtrOrNull   = Union(Ptr(a), Nil)

_tl_sizeof_ (x : a)        -> Int
_tl_fatal_  (s : String )  -> any

c_malloc    (count : Int)  -> PtrOrNull(a)
c_free      (ptr : Ptr) -> Nil

is_null     (p : PtrOrNull(a)) -> Bool
is_ptr      (p : PtrOrNull(a)) -> Bool
ptr         (p : PtrOrNull(a)) -> Ptr(a)

sizeof      (T) { _tl_sizeof_(T) }       // T may be Type (literal) or concrete

is_null(p) { if p == null { true } else { false } }
is_ptr(p)  { !is_null(p) }
ptr(p)     { if is_null(p) { _tl_fatal_("null pointer") } else { p } }
