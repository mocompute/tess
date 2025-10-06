let _tl_add_     : a -> a -> a
let _tl_bcomp_   : a -> a
let _tl_sizeof_  : a -> Int

let add l r : a -> a -> a = _tl_add_ l r
let bcomp x      : a -> a = _tl_bcomp_ x
let sizeof x     : a -> Int = _tl_sizeof_ x

let main () =
  let val = 123 in
  let x1 = _tl_sizeof_ val in
  let x2 = _tl_sizeof_ "hello" in
  x2
