let id x0 = x0
let f x1 = id x1
let g x2 = let foo = bar in f x2

let main () =
  g 0
