let ex1 () =
  let val = 2 in
  let f = fun x -> add x val end in // lambda captures val
  f 1

let main () =
    if   eq (ex1 ()) 3
    then 0
    else 1


// let ex1 () =
//   let val = 2 in
//   let f = fun x -> val end in // lambda captures val
//   let _ = f 1 in
//   true

// let main () =
//     if ex1 () then 0 else 1


// let main () =
//   let val = 0 in
//   let f = fun x -> val end in // lambda captures val
//   f 1
