let id x = x
let f x = id x
let g x = f x
let h x = g x

let main () =
  h 0
