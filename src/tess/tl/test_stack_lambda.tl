let ex1 () =
  let val = 2 in
  let f = fun x -> add x val end in // lambda captures val
  f 1

let main () =
    if   eq (ex1 ()) 3
    then 0
    else 1
