let id x = x

let main () =
  let val = 0 in
  let f1 = fun x -> id val in

  id "hello"
