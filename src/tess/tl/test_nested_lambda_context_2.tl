let apply f = f ()
let apply2 f = apply f
let apply3 f = apply2 f

let main () =
  let val = true in
  let afun = fun () -> val end in
  let foo = apply3 afun in
  0
