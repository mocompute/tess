let apply f = f ()

let main () =
  let cap = true in
  let afun = fun () -> cap end in
  let foo = apply afun in
  0
