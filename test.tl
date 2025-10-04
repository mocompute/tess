let _tl_sizeof_  : a -> Int     // actually forall a. a -> int


// let id x = x
// let f x = id x
// let g x = f x

let main () =
  let val = 1 in
  let str = "hello" in
  let _ = _tl_sizeof_ str in
  _tl_sizeof_ val
