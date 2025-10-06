let _tl_add_     : a -> a -> a
let _tl_bcomp_   : a -> a

let add l r : a -> a -> a = _tl_add_ l r
let bcomp x      : a -> a = _tl_bcomp_ x

let main () =
  add 1 (bcomp 2)
