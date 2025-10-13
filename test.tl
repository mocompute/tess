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


// let main () =
//   let id = fun x -> x in
//   let x1 = id 0 in
//   let x2 = id "hello" in
//   x1

let _tl_add_     : a -> a -> a
let add l r      : a -> a -> a = _tl_add_ l r

let main () =
  let f = fun a b -> add a b end in
  let x1 = f 1 2 in
  let x2 = f 1.0 0.5 in
  let res = f 0 0 in
  res
