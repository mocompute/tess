let ex1 () =
    1 ; 2; 3 ; 0

let ex2 () =
    1 ; 2; 3 ; 0


let ex3 () =
    ex2 ()

let main () =
    ex3 () - ex2 ()
