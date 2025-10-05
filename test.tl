let _tl_sizeof_  : a -> Int     // actually forall a. a -> int
let sizeof x     : a -> Int        = _tl_sizeof_ x


// let id x = x
// let f x = id x
// let g x = f x

let main () =
  let val = 1 in
  let str = "hello" in
  let _ = sizeof str in
  sizeof val
