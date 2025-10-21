let c_malloc     : Int -> Ptr a
let c_free       : Ptr a -> ()

let main () =
  let (p : Ptr Int) = c_malloc 64 in
  p
