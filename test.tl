let _tl_sizeof_  : a -> Int     // actually forall a. a -> int
let sizeof x     : a -> Int = _tl_sizeof_ x

let main () =
  let val = 1 in
  sizeof val
