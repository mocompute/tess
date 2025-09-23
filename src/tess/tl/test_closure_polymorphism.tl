let f g = if g () then true else false

let main () =
  let x1 = true in
  let x2 = false in
  let f1 = fun () -> x1 end in
  let f2 = fun () -> x2 end in
  let ff1 = f f1 in
  let ff2 = f f2 in
  0
