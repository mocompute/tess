let id x = x

let main () =
  let val = 0 in
  let _ = id "string" in
  let f1 = fun x -> id val in

  f1 "hello"
