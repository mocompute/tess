let apply f = f ()
let apply2 f = apply f

let main () =
  let val = true in
  let afun = fun () -> val end in
  let foo = apply2 afun in
  0
