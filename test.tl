// let apply f = f ()

// let main () =
//   let afun = fun () -> true end in
//   let foo = apply afun in
//   0

// let id x = x
// let main () =
//   let x1 = id 0 in
//   let x2 = id "hello" in
//   x1


let main () =
  let id = fun x -> x in
  let x1 = id 0 in
  let x2 = id "hello" in
  x1
