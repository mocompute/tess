let ex1 () =
    1 ; 2; 3 ; 0

let ex2 () =
    1 ; 2; 3 ; 0


let ex3 () =
    let x1 =
    1 ; 2 ; 0
    in let x2 =
        ex1 () ; ex2 () ; 2
        in 0


let main () =
    let err = 0
    let err = err + ex1()
    let err = err + ex2()
    let err = err + ex3()
    in err
