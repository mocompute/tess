let main () =
  let val = 2 in
  let f = fun x -> val end in // lambda captures val
  f 1
