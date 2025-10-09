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

let id x1 = x1
let f x2 = id x2
let g x3 = f x3
let h x4 = g x4



let main () =
  h 0
