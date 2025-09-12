let add1 x = incr x

let foo f y = f y

let main () =
    foo add1 -1
