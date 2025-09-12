let ex1 () = begin 1 ; 2 ; 3 ; 0 end

let ex2 () = begin 1 ; 2 ; 3 ; 0 end

let ex3 () =
    let x1 = begin 1 ; 2 ; 0 end in
    let x2 = begin ex1 () ; ex2 () ; 2 end
    in 0


let main () =
    let err = 0 in
    let err = add err (ex1 ()) in
    let err = add err (ex2 ()) in
    let err = add err (ex3 ()) in
    err
