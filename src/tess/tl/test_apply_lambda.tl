let apply f = f ()

let main () =
  let afun = fun () -> true end in
  let foo = apply afun in
  0
