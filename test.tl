let apply f a b = f a b
let foo a b : String -> Int -> Nil = std_dbg a b

let main () =
  let _ = apply foo "hello, world: the answer is %i" 42 in
  0
