let f x = let fv = foo in x

let main () =
 let y = 1 in
 let lambda = fun x -> let a = 1 in let b = 2 in y a b c end in
 // let _ = f "hello" in
 let _ = f 123 in
 let _ = f 456 in
 0
