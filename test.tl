// let fact x =
//     // confirmed that this will be tail call optimised by clang at -O,
//     if   lte x 1
//     then 1
//     else mul x (fact (decr x))

// let main () =
//   fact 20
//     // if   eq (fact 20) 2432902008176640000
//     // then 0
//     // else 1

// let id x = x
// let f x = id x
// let g x = f x
// let h x = g x

let main () =
  let val = 1 in
  (fun x -> x) val
