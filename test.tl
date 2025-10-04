let main () =
  let f1 = fun x -> x in
  let a = f1 "hello" in
  let b = f1 123 in
  let c = f1 0.5 in
  f1 0
