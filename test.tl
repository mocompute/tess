let id x = x

let main () =
  let val = 0 in
  let _ = id "string" in
  let f1 = fun x -> id val in
  let _ = f1 "hello" in
  let _ = f1 123 in
  f1 "hello"
