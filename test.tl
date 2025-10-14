let f () =
  let val = 1 in
  &val

let main () =
  let ptr = f () in
  ptr.*
