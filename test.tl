let ex2 f = f ()

let ex1 () =
  let val = true in
  let ex1_lambda = fun () -> val end in
  ex1_lambda ()

let main () =
  let foo = ex2 ex1 in
  0
