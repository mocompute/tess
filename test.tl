let _tl_sizeof_  : a -> Int     // actually forall a. a -> int
let sizeof x     : a -> Int = _tl_sizeof_ x

// let main () =
//   let val = 1 in
//   let sz = sizeof val in
//   let f1 = fun x -> x in
//   let foo = f1 42 in
//   sizeof val


let f x = x

let foo ()  = let val = f 1 in val
let bar ()  = foo ()
let baz ()  = bar ()
let main () = baz ()

let tl_main () = main ()
